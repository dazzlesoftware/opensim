"""Real original Robust/original region/native region in an isolated grid."""
import argparse
import base64
import json
from pathlib import Path
import re
import socket
import sqlite3
import subprocess
import struct
import sys
import time
import urllib.request
import urllib.error
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from original_peer_network import copy_original_distribution
from preview_entry_network import PreviewServer, Viewer, check, post


def fetch_inventory_asset(viewer, item_id, asset_id, expected):
    """Use the viewer's authenticated inventory TransferRequest."""
    transfer = uuid.uuid4()
    params = (viewer.credentials() + uuid.UUID(viewer.login['agent_id']).bytes + bytes(16)
              + uuid.UUID(item_id).bytes + uuid.UUID(asset_id).bytes)
    viewer.send(bytes.fromhex('ffff0099') + transfer.bytes + struct.pack('<iifH', 2, 3, 1.0, len(params)) + params)
    info = viewer.wait(bytes.fromhex('ffff009a') + transfer.bytes)
    channel, target, status, size, length = struct.unpack_from('<iiiiH', info, 20)
    check((channel, status, size) == (2, 0, len(expected)),
          f'Inventory asset transfer failed: channel={channel}, status={status}, size={size}')
    check(info[38:38 + length] == params, 'Inventory transfer changed its authenticated parameters')
    chunks = {}
    final = None
    deadline = time.monotonic() + 10
    while final is None or len(chunks) < final + 1:
        check(time.monotonic() < deadline, 'Inventory asset transfer did not finish')
        packet = viewer.wait(b'\x11' + transfer.bytes,
                             predicate=lambda data: struct.unpack_from('<i', data, 21)[0] not in chunks,
                             timeout=max(.01, deadline - time.monotonic()))
        channel, index, status, length = struct.unpack_from('<iiiH', packet, 17)
        check(channel == 2 and index >= 0 and status in (0, 1), 'Invalid inventory asset chunk')
        chunks[index] = packet[31:31 + length]
        check(len(chunks[index]) == length, 'Truncated inventory asset chunk')
        if status == 1:
            final = index
    check(b''.join(chunks[index] for index in range(final + 1)) == expected,
          'Viewer received different asset bytes from the grid asset service')
    return len(chunks)


def reserve(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def check_native_local_service_rows(root):
    for filename, tables in [('users.db', ['UserAccounts']),
                             ('inventory.db', ['inventoryfolders', 'inventoryitems']),
                             ('avatars.db', ['Avatars'])]:
        with sqlite3.connect(f'file:{root / filename}?mode=ro', uri=True) as database:
            for table in tables:
                check(database.execute(f'SELECT COUNT(*) FROM {table}').fetchone() == (0,),
                      f'Remote grid service data was copied into native local table {table}')


def avatar_position(packet, local_id, agent):
    if packet[:1] == b'\x0c' and len(packet) >= 93 and packet[17:33] == agent.bytes:
        return struct.unpack_from('<3f', packet, 69)
    if packet[:1] != b'\x0f' or len(packet) < 12:
        return None
    offset = 12
    for _ in range(packet[11]):
        if offset >= len(packet):
            break
        length = packet[offset]
        offset += 1
        data = packet[offset:offset + length]
        offset += length
        if offset + 2 > len(packet):
            break
        texture_size = struct.unpack_from('<H', packet, offset)[0]
        offset += 2 + texture_size
        if length == 60 and struct.unpack_from('<I', data)[0] == local_id:
            return struct.unpack_from('<3f', data, 22)
    return None


def observe_forward_movement(mover, observer):
    agent = uuid.UUID(mover.login['agent_id'])
    frame = observer.wait(b'\x0c', predicate=lambda data: len(data) >= 93 and data[17:33] == agent.bytes)
    local_id = struct.unpack_from('<I', frame, 12)[0]
    before = avatar_position(frame, local_id, agent)

    def controls(flags):
        mover.send(b'\x04' + mover.credentials() + bytes(25) +
                   struct.pack('<13fIB', *before, 1, 0, 0, 0, 1, 0, 0, 0, 1, 64, flags, 0))

    latest = None

    def moved(packet):
        nonlocal latest
        position = avatar_position(packet, local_id, agent)
        if position is not None and position[0] > before[0] + .25:
            latest = position
            return True
        return False

    controls(1)
    try:
        observer.wait(b'', predicate=moved, timeout=5)
    finally:
        controls(0)
    return dict(agent=str(agent), before=list(before), observed=list(latest))


class OriginalServer:
    def __init__(self, dotnet, assembly, root, log_name='server.log'):
        self.root = root
        self.log_path = root / log_name
        self.log = self.log_path.open('w', encoding='utf-8')
        self.process = subprocess.Popen([dotnet, str(root / assembly), '-console=basic'], cwd=root,
                                        stdin=subprocess.PIPE, stdout=self.log,
                                        stderr=subprocess.STDOUT, text=True)

    def wait(self, predicate):
        deadline = time.monotonic() + 180
        while not predicate(self.log_path.read_text()):
            check(self.process.poll() is None, f'Original server exited: {self.log_path}')
            check(time.monotonic() < deadline, f'Original server startup timeout: {self.log_path}')
            time.sleep(.1)

    def command(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()

    def stop(self):
        try:
            if self.process.poll() is None:
                self.command('shutdown')
                try:
                    self.process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                    raise AssertionError(f'Original server shutdown timeout: {self.root}')
            check(self.process.returncode == 0, f'Original server exited {self.process.returncode}: {self.root}')
        finally:
            self.log.close()


def stop_servers(started, root_server, viewers):
    for viewer in viewers.values():
        viewer.socket.close()
    failures = []
    stop_order = list(reversed(started))
    if root_server in stop_order:
        stop_order.remove(root_server)
        stop_order.insert(0, root_server)
    for server in stop_order:
        try:
            server.stop()
        except Exception as error:
            failures.append(error)
    if failures:
        active = sys.exception()
        if active is None:
            raise failures[0]
        for failure in failures:
            active.add_note('Cleanup: ' + str(failure))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--inventory-broker', action='store_true',
                        help='Use HGInventoryBroker with the original Robust inventory as local-grid service')
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / 'results.json').unlink(missing_ok=True)
    root = args.work / ('case-' + uuid.uuid4().hex)
    root.mkdir()
    robust_root, original_root, native_root = [root / name for name in ['robust', 'original', 'native']]
    for directory in [robust_root, original_root]:
        directory.mkdir()
        copy_original_distribution(args.bin.resolve(), directory)
    native_root.mkdir()
    service_port, original_port = reserve(), reserve()
    original_udp, native_udp = reserve(socket.SOCK_DGRAM), reserve(socket.SOCK_DGRAM)
    services = f'http://127.0.0.1:{service_port}/'
    robust_config = (args.bin / 'Robust.ini.example').read_text()
    robust_config = robust_config.replace('PublicPort = "8002"', f'PublicPort = "{service_port}"')
    robust_config = robust_config.replace('PrivatePort = "8003"', f'PrivatePort = "{service_port}"')
    robust_config = re.sub(r'(?m)^\s*StorageProvider = "OpenSim.Data.MySQL.dll"',
                           '\n    StorageProvider = "OpenSim.Data.SQLite.dll"', robust_config)
    robust_config = re.sub(r'(?m)^\s*ConnectionString = "Data Source=localhost[^\n]+',
                           '\n    ConnectionString = "URI=file:services.db;UseUTF16Encoding=True"', robust_config)
    robust_config = re.sub(r'(?m)^\s*(MapAddServiceConnector|MapGetServiceConnector)\s*=.*\n', '', robust_config)
    # Match the original SQLite standalone layout: independent service files
    # avoid cross-provider transactions locking one shared database.
    for section in ['AssetService', 'InventoryService', 'AvatarService', 'AuthenticationService',
                    'UserAccountService', 'GridUserService', 'AgentPreferencesService',
                    'FriendsService', 'MuteListService']:
        robust_config = robust_config.replace('[' + section + ']', '[' + section + ']\n'
            + f'    ConnectionString = "URI=file:{section}.db;UseUTF16Encoding=True"', 1)
    # Original SQLite does not implement grid/presence stores. Use the same
    # original in-process stores as standalone mode; durable grid testing is separate.
    robust_config += '''
[GridService]
LocalServiceModule=OpenSim.Services.GridService.dll:GridService
StorageProvider=OpenSim.Data.Null.dll
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[PresenceService]
LocalServiceModule=OpenSim.Services.PresenceService.dll:PresenceService
StorageProvider=OpenSim.Data.Null.dll
[GridInfoService]
DisableStatsEndpoint=true
'''
    (robust_root / 'Robust.ini').write_text(robust_config)
    common = f'''[Const]
BaseURL=http://127.0.0.1
PrivURL=http://127.0.0.1
PublicPort={service_port}
PrivatePort={service_port}
'''
    (original_root / 'config-include/GridCommon.ini').write_text(
        common + (args.bin / 'config-include/GridCommon.ini.example').read_text())
    (original_root / 'OpenSim.ini').write_text(f'''{common}
[Startup]
physics=POS
meshing=Meshmerizer
console=basic
regionload_regionsdir=Regions
ConsoleHistoryFileEnabled=false
[Network]
http_listener_port={original_port}
[Architecture]
Include-Architecture=config-include/Grid.ini
[Estates]
DefaultEstateName=Mixed Original Estate
DefaultEstateOwnerName=Preview Mixed
DefaultEstateOwnerUUID=44444444-4444-4444-8444-444444444444
[Map]
GenerateMaptiles=false
''')
    (original_root / 'Regions/peer.ini').write_text(f'''[Original Mixed]
RegionUUID=33333333-3333-4333-8333-333333333333
Location=1002,1000
InternalAddress=127.0.0.1
InternalPort={original_udp}
ExternalHostName=127.0.0.1
''')
    native_config = native_root / 'server.ini'
    native_config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableSimulationEndpoint=true
EnableRegionAssets=true
AvatarDatabase=avatars.db
UserAccountDatabase=users.db
InventoryDatabase=inventory.db
AuthenticationProvider=Remote
[NativeRegion]
TerrainDatabase=region.db
Physics=ubODE
ID=55555555-5555-4555-8555-555555555555
Name=Native Mixed
LocationX=1003
LocationY=1000
UDPPort={native_udp}
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[Network]
http_listener_port=0
[Modules]
AssetServices=RegionAssetConnector
UserAccountServices=RemoteUserAccountServicesConnector
AvatarServices=RemoteAvatarServicesConnector
InventoryServices={'HGInventoryBroker' if args.inventory_broker else 'RemoteXInventoryServicesConnector'}
PresenceServices=RemotePresenceServicesConnector
GridUserServices=RemoteGridUserServicesConnector
[GridService]
GridServerURI={services.rstrip('/')}
[PresenceService]
PresenceServerURI={services}
[AuthenticationService]
AuthenticationServerURI={services}
[UserAccountService]
UserAccountServerURI={services}
[GridUserService]
GridUserServerURI={services}
[InventoryService]
LocalGridInventoryService=OpenSim.Region.CoreModules.dll:RemoteXInventoryServicesConnector
InventoryServerURI={services}
[AssetService]
LocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector
AssetServerURI={services}
[AvatarService]
AvatarServerURI={services.rstrip('/')}
[Appearance]
DelayBeforeAppearanceSave=0
[ClientStack.LindenCaps]
Cap_FetchInventoryDescendents2=localhost
''')
    started = []
    viewers = {}
    root_server = None
    try:
        robust = OriginalServer(args.dotnet, 'Robust.dll', robust_root)
        started.append(robust)
        robust.wait(lambda text: 'MuteListServiceConnector loaded successfully' in text)
        robust.command('create user Preview Mixed preview-password preview@example.invalid 44444444-4444-4444-8444-444444444444 Ruth')
        robust.command('create user Preview Other preview-other-password other@example.invalid 66666666-6666-4666-8666-666666666666 Ruth')
        deadline = time.monotonic() + 20
        while True:
            request = urllib.request.Request(services + 'accounts',
                                             b'METHOD=getaccount&FirstName=Preview&LastName=Mixed')
            with urllib.request.urlopen(request, timeout=5) as response:
                account = ET.fromstring(response.read())
            if account.findtext('result/PrincipalID'):
                break
            check(time.monotonic() < deadline, 'Robust did not create the fresh grid account')
            time.sleep(.1)
        deadline = time.monotonic() + 20
        while True:
            request = urllib.request.Request(services + 'accounts',
                                             b'METHOD=getaccount&FirstName=Preview&LastName=Other')
            with urllib.request.urlopen(request, timeout=5) as response:
                account = ET.fromstring(response.read())
            if account.findtext('result/PrincipalID'):
                break
            check(time.monotonic() < deadline, 'Robust did not create the second fresh grid account')
            time.sleep(.1)
        original = OriginalServer(args.dotnet, 'OpenSim.dll', original_root)
        started.append(original)
        original.wait(lambda text: 'INITIALIZATION COMPLETE' in text)
        command = [str(args.native.resolve()), '--config', str(native_config)]
        provision = subprocess.run(command + ['--init-db'], cwd=native_root, capture_output=True,
                                   text=True, timeout=40)
        check(provision.returncode == 0, provision.stdout + provision.stderr)
        native = PreviewServer(command, native_root, native_root / 'server.log')
        started.append(native)
        native_uri = native.start()
        login = xmlrpc.client.ServerProxy(services).login_to_simulator(dict(
            first='Preview', last='Mixed', passwd='preview-password',
            start='uri:Original Mixed&128&128&30', channel='Mixed original interoperability', version='1.0'))
        check(login.get('login') == 'true', str(login))
        check(login['sim_port'] == original_udp, 'Original Robust selected the wrong initial region')
        current = Viewer(login)
        viewers[original_udp] = current
        root_server = original
        current.connect()
        seed = login['seed_capability']
        inventory_root = login['inventory-root'][0]['folder_id']
        expected_categories = {folder['folder_id'] for folder in login['inventory-skeleton']
                               if folder['parent_id'] == inventory_root}
        check(bool(expected_categories), 'Original fresh account has no required inventory folders')
        body_parts = next(folder['folder_id'] for folder in login['inventory-skeleton']
                          if folder['type_default'] == 13)
        request = urllib.request.Request(services + 'xinventory',
            f'METHOD=GETFOLDERITEMS&PRINCIPAL={login["agent_id"]}&FOLDER={body_parts}'.encode())
        with urllib.request.urlopen(request, timeout=5) as response:
            original_items = ET.fromstring(response.read()).find('ITEMS')
        expected_items = {item.findtext('ID'): (item.findtext('AssetID'), item.findtext('Name'))
                          for item in original_items}
        check(bool(expected_items), 'Original fresh account has no default body-part inventory items')
        wearable_assets = {}
        clothing = next(folder['folder_id'] for folder in login['inventory-skeleton']
                        if folder['type_default'] == 5)
        for folder_id in (body_parts, clothing):
            request = urllib.request.Request(services + 'xinventory',
                f'METHOD=GETFOLDERITEMS&PRINCIPAL={login["agent_id"]}&FOLDER={folder_id}'.encode())
            with urllib.request.urlopen(request, timeout=5) as response:
                items = ET.fromstring(response.read()).find('ITEMS')
            for item in items:
                asset_id = item.findtext('AssetID')
                with urllib.request.urlopen(services + 'assets/' + asset_id, timeout=5) as response:
                    asset = ET.fromstring(response.read())
                data = base64.b64decode(asset.findtext('Data'))
                check(bool(data), 'Original default wearable asset has no payload')
                wearable_assets[item.findtext('ID')] = (asset_id, data)
        check(len(wearable_assets) == 6, 'Fresh original account is missing default body parts or clothes')
        asset_downloads = []
        event_ids = {}
        hops = []
        native_actions_completed = False
        for name, tile, udp in [('Native Mixed', 1003, native_udp), ('Original Mixed', 1002, original_udp),
                               ('Native Mixed', 1003, native_udp)]:
            event_url = post(seed, ['EventQueueGet'])['EventQueueGet']
            handle = ((tile * 256) << 32) | (1000 * 256)
            current.send(bytes.fromhex('ffff003f') + current.credentials()
                         + struct.pack('<Q6f', handle, 128, 128, 30, 1, 0, 0))
            current.wait(bytes.fromhex('ffff0049'))
            finish = None
            deadline = time.monotonic() + 20
            while finish is None and time.monotonic() < deadline:
                try:
                    batch = post(event_url, dict(ack=event_ids.get(event_url, 0), done=0))
                except urllib.error.HTTPError as error:
                    if error.code != 502:
                        raise
                    continue
                event_ids[event_url] = batch['id']
                finish = next((event['body']['Info'][0] for event in batch['events']
                               if event['message'] == 'TeleportFinish'), None)
            check(finish is not None, f'No original/native handoff to {name}')
            check(finish['SimPort'] == udp, f'Handoff selected the wrong endpoint for {name}')
            check(struct.unpack('>Q', base64.b64decode(finish['RegionHandle']))[0] == handle,
                  f'Handoff selected the wrong handle for {name}')
            seed = finish['SeedCapability']
            if udp not in viewers:
                current = Viewer(dict(login, sim_ip='127.0.0.1', sim_port=udp, seed_capability=seed))
                viewers[udp] = current
                current.connect()
            else:
                current = viewers[udp]
                current.received.clear()
                current.send(bytes.fromhex('ffff00f9') + current.credentials()
                             + struct.pack('<I', int(login['circuit_code'])))
                current.wait(bytes.fromhex('ffff00fa'),
                             predicate=lambda data: struct.unpack_from('<Q', data, 60)[0] == handle)
            capabilities = post(seed, ['FetchInventoryDescendents2', 'CreateInventoryCategory'])
            check('FetchInventoryDescendents2' in capabilities, f'No inventory capability in {name}')
            inventory_reply = post(capabilities['FetchInventoryDescendents2'], dict(folders=[dict(
                folder_id=uuid.UUID(folder_id), owner_id=uuid.UUID(login['agent_id']),
                fetch_folders=True, fetch_items=True, sort_order=0)
                for folder_id in (inventory_root, body_parts)]))
            folders = inventory_reply.get('folders', [])
            by_id = {folder['folder_id']: folder for folder in folders}
            check(inventory_root in by_id and body_parts in by_id,
                  f'Grid inventory root is unavailable in {name}: {inventory_reply}')
            check({folder['category_id'] for folder in by_id[inventory_root].get('categories', [])} == expected_categories,
                  f'Grid inventory folders changed after crossing to {name}: {inventory_reply}')
            check({item['item_id']: (item['asset_id'], item['name'])
                   for item in by_id[body_parts].get('items', [])} == expected_items,
                  f'Default body-part inventory items changed after crossing to {name}: {inventory_reply}')
            for item_id, (asset_id, data) in wearable_assets.items():
                chunks = fetch_inventory_asset(current, item_id, asset_id, data)
                asset_downloads.append(dict(region=name, item=item_id, asset=asset_id, bytes=len(data), chunks=chunks))
            if udp == native_udp and not native_actions_completed:
                changed_parameters = current.change_appearance()
                deadline = time.monotonic() + 10
                while True:
                    request = urllib.request.Request(services + 'avatar',
                        f'METHOD=getavatar&UserID={login["agent_id"]}'.encode())
                    with urllib.request.urlopen(request, timeout=5) as response:
                        stored = ET.fromstring(response.read()).findtext('result/VisualParams')
                    if stored and bytes(map(int, stored.split(','))) == changed_parameters:
                        break
                    check(time.monotonic() < deadline,
                          'Appearance changed in native region was not saved to original avatar service')
                    time.sleep(.05)
                check('CreateInventoryCategory' in capabilities, 'No category creation capability in native region')
                category = uuid.uuid4()
                created = post(capabilities['CreateInventoryCategory'], dict(
                    folder_id=category, parent_id=uuid.UUID(inventory_root),
                    name='Created across mixed grid', type=-1))
                check(created.get('folder_id') == str(category), f'Remote category was not created: {created}')
                expected_categories.add(str(category))
                native_actions_completed = True
            hops.append(dict(destination=name, passed=True))
            root_server = native if udp == native_udp else original
            region_id = ('55555555-5555-4555-8555-555555555555' if udp == native_udp
                         else '33333333-3333-4333-8333-333333333333')
            deadline = time.monotonic() + 5
            while True:
                request = urllib.request.Request(services + 'presence',
                    ('METHOD=getagent&SessionID=' + login['session_id']).encode())
                with urllib.request.urlopen(request, timeout=5) as response:
                    presence = ET.fromstring(response.read())
                if presence.findtext('result/RegionID') == region_id:
                    break
                check(time.monotonic() < deadline, f'Original presence service did not report root in {name}')
                time.sleep(.01)
            (root / 'progress.json').write_text(json.dumps(hops, indent=2) + '\n')
        current.send(bytes.fromhex('ffff00fc') + current.credentials())
        current.wait(bytes.fromhex('ffff00fd'))
        deadline = time.monotonic() + 5
        while True:
            request = urllib.request.Request(services + 'presence',
                f'METHOD=getagent&SessionID={login["session_id"]}'.encode())
            with urllib.request.urlopen(request, timeout=5) as response:
                active_session = ET.fromstring(response.read()).find('result')
            request = urllib.request.Request(services + 'griduser',
                f'METHOD=getgriduserinfo&UserID={login["agent_id"]}'.encode())
            with urllib.request.urlopen(request, timeout=5) as response:
                grid_online = ET.fromstring(response.read()).findtext('result/Online')
            if (active_session is not None and active_session.text == 'null' and not list(active_session)
                    and grid_online == 'False'):
                break
            check(time.monotonic() < deadline, 'Logout did not clear original presence and GridUser online state')
            time.sleep(.02)
        fresh_login = xmlrpc.client.ServerProxy(services).login_to_simulator(dict(
            first='Preview', last='Mixed', passwd='preview-password', start='uri:Native Mixed&128&128&30',
            channel='Mixed original appearance persistence', version='1.0'))
        check(fresh_login.get('login') == 'true' and fresh_login['sim_port'] == native_udp,
              f'Fresh grid login did not reach native region: {fresh_login}')
        fresh_viewer = Viewer(fresh_login)
        viewers['fresh'] = fresh_viewer
        root_server = native
        fresh_viewer.connect()
        appearance = fresh_viewer.wait(bytes.fromhex('ffff009e'), uuid.UUID(fresh_login['agent_id']).bytes)
        check(Viewer.visual_parameters(appearance) == changed_parameters,
              'Saved mixed-grid appearance did not survive logout and fresh login')
        object_local, object_id = fresh_viewer.create_box()
        fresh_viewer.edit_box(object_local, object_id)
        fresh_viewer.take_copy(object_local)
        objects_folder = next(folder['folder_id'] for folder in login['inventory-skeleton']
                              if folder['type_default'] == 6)
        deadline = time.monotonic() + 5
        while True:
            request = urllib.request.Request(services + 'xinventory',
                f'METHOD=GETFOLDERITEMS&PRINCIPAL={login["agent_id"]}&FOLDER={objects_folder}'.encode())
            with urllib.request.urlopen(request, timeout=5) as response:
                copied_items = ET.fromstring(response.read()).find('ITEMS')
            if copied_items is not None and len(copied_items):
                break
            check(time.monotonic() < deadline, 'Taking a native object copy did not create remote inventory')
            time.sleep(.02)
        check(len(copied_items) == 1, 'Taking one object copy created unexpected inventory items')
        copied_item = copied_items[0].findtext('ID')
        copied_asset = copied_items[0].findtext('AssetID')
        with urllib.request.urlopen(services + 'assets/' + copied_asset, timeout=5) as response:
            copied_data = base64.b64decode(ET.fromstring(response.read()).findtext('Data'))
        check(bool(copied_data), 'Native object copy did not store an asset in original Robust')
        fetch_inventory_asset(fresh_viewer, copied_item, copied_asset, copied_data)
        handle = ((1003 * 256) << 32) | (1000 * 256)
        fresh_viewer.send(bytes.fromhex('ffff003f') + fresh_viewer.credentials()
                          + struct.pack('<Q6f', handle, 140, 141, 30, 1, 0, 0))
        fresh_viewer.wait(bytes.fromhex('ffff0040'),
                          predicate=lambda data: struct.unpack_from('<3f', data, 24) == (140, 141, 30))
        check_native_local_service_rows(native_root)
        report = dict(passed=True, caseDirectory=str(root), originalRobust=True,
                      originalRegion=True, nativeRegion=True, freshGridAccount=True,
                      servicesUri=services, nativeUri=native_uri, mixedTravelVerified=True, hops=hops,
                      originalPresenceRootVerified=True, nativeLocalAccountRows=0,
                      gridInventoryFoldersVerified=True, nativeCategoryVisibleInOriginalRegion=True,
                      defaultBodyPartItemsVerified=len(expected_items),
                      authenticatedWearableAssetDownloadVerified=True, wearableAssetDownloads=asset_downloads,
                      nativeAppearanceSavedToOriginalService=True,
                      appearanceFreshLoginVerified=True, nativeLocalAvatarRows=0,
                      nativeLocalInventoryRows=0,
                      gridAndPresenceStorage='original in-memory providers',
                      appearanceInventoryBuildingRestartVerified=False, realFirestorm=False)
    finally:
        stop_servers(started, root_server, viewers)
    report['cleanShutdown'] = True
    with sqlite3.connect(f'file:{robust_root / "InventoryService.db"}?mode=ro', uri=True) as database:
        check(database.execute('SELECT folderName, agentID, parentFolderID FROM inventoryfolders WHERE folderID=?',
                               (str(category),)).fetchone() ==
              ('Created across mixed grid', login['agent_id'], inventory_root),
              'Native-created category was not retained by original Robust after clean shutdown')
    report['nativeCategoryPersistedInOriginalInventoryDatabase'] = True
    with sqlite3.connect(f'file:{robust_root / "AvatarService.db"}?mode=ro', uri=True) as database:
        check(database.execute('SELECT Value FROM Avatars WHERE PrincipalID=? AND Name=?',
                               (login['agent_id'], 'VisualParams')).fetchone() ==
              (','.join(map(str, changed_parameters)),),
              'Native appearance save was not retained by original avatar database after clean stop')
    report['appearancePersistedInOriginalAvatarDatabase'] = True
    # Start new processes over the files written by clean shutdown. In-memory
    # grid/presence registrations must be recreated by the two regions.
    started, viewers, root_server = [], {}, None
    try:
        robust = OriginalServer(args.dotnet, 'Robust.dll', robust_root, 'restart.log')
        started.append(robust)
        robust.wait(lambda text: 'MuteListServiceConnector loaded successfully' in text)
        original = OriginalServer(args.dotnet, 'OpenSim.dll', original_root, 'restart.log')
        started.append(original)
        original.wait(lambda text: 'INITIALIZATION COMPLETE' in text)
        native = PreviewServer(command, native_root, native_root / 'restart.log')
        started.append(native)
        native.start()
        restored_login = xmlrpc.client.ServerProxy(services).login_to_simulator(dict(
            first='Preview', last='Mixed', passwd='preview-password', start='last',
            channel='Mixed grid clean process restart', version='1.0'))
        check(restored_login.get('login') == 'true' and restored_login['agent_id'] == login['agent_id'] and
              restored_login['sim_port'] == native_udp,
              f'Original account/last region was not restored after restart: {restored_login}')
        restored = Viewer(restored_login)
        viewers['restored'] = restored
        root_server = native
        restored.connect()
        movement = restored.wait(bytes.fromhex('ffff00fa'))
        check(struct.unpack_from('<2f', movement, 36) == (140, 141),
              'Avatar last position did not survive mixed-grid restart')
        appearance = restored.wait(bytes.fromhex('ffff009e'), uuid.UUID(restored_login['agent_id']).bytes)
        check(Viewer.visual_parameters(appearance) == changed_parameters,
              'Appearance did not survive mixed-grid process restart')
        restored.wait_for_box(object_id)
        caps = post(restored_login['seed_capability'], ['FetchInventoryDescendents2'])
        inventory_reply = post(caps['FetchInventoryDescendents2'], dict(folders=[dict(
            folder_id=uuid.UUID(folder_id), owner_id=uuid.UUID(restored_login['agent_id']),
            fetch_folders=True, fetch_items=True, sort_order=0)
            for folder_id in (inventory_root, objects_folder)]))
        folders = {folder['folder_id']: folder for folder in inventory_reply.get('folders', [])}
        check({folder['category_id'] for folder in folders[inventory_root]['categories']} == expected_categories,
              'Remote inventory folders did not survive mixed-grid process restart')
        check({item['item_id']: item['asset_id'] for item in folders[objects_folder]['items']} ==
              {copied_item: copied_asset}, 'Native object copy did not survive remote inventory restart')
        fetch_inventory_asset(restored, copied_item, copied_asset, copied_data)
        for item_id, (asset_id, data) in wearable_assets.items():
            fetch_inventory_asset(restored, item_id, asset_id, data)
        other_login = xmlrpc.client.ServerProxy(services).login_to_simulator(dict(
            first='Preview', last='Other', passwd='preview-other-password', start='uri:Native Mixed&128&128&30',
            channel='Mixed grid simultaneous account isolation', version='1.0'))
        check(other_login.get('login') == 'true' and other_login['sim_port'] == native_udp, str(other_login))
        check(other_login['agent_id'] != restored_login['agent_id'] and
              other_login['session_id'] != restored_login['session_id'], 'Accounts shared credentials')
        other = Viewer(other_login)
        viewers['other'] = other
        other.connect()
        other.wait_for_box(object_id)
        request = urllib.request.Request(services + 'avatar',
            f'METHOD=getavatar&UserID={other_login["agent_id"]}'.encode())
        with urllib.request.urlopen(request, timeout=5) as response:
            other_parameters = bytes(map(int, ET.fromstring(response.read()).findtext('result/VisualParams').split(',')))
        check(other_parameters != changed_parameters, 'First appearance update leaked into the other account')
        first_seen = restored.wait(bytes.fromhex('ffff009e'), uuid.UUID(other_login['agent_id']).bytes)
        second_seen = other.wait(bytes.fromhex('ffff009e'), uuid.UUID(restored_login['agent_id']).bytes)
        check(Viewer.visual_parameters(first_seen) == other_parameters and
              Viewer.visual_parameters(second_seen) == changed_parameters, 'Mutual avatar appearance differs')
        other_root = other_login['inventory-root'][0]['folder_id']
        check(other_root != inventory_root, 'Accounts shared inventory roots')
        other_objects = next(folder['folder_id'] for folder in other_login['inventory-skeleton']
                             if folder['type_default'] == 6)
        other_caps = post(other_login['seed_capability'], ['FetchInventoryDescendents2', 'CreateInventoryCategory'])
        other_reply = post(other_caps['FetchInventoryDescendents2'], dict(folders=[dict(
            folder_id=uuid.UUID(folder_id), owner_id=uuid.UUID(other_login['agent_id']),
            fetch_folders=True, fetch_items=True, sort_order=0) for folder_id in (other_root, other_objects)]))
        other_folders = {folder['folder_id']: folder for folder in other_reply['folders']}
        check(all(folder['owner_id'] == other_login['agent_id'] for folder in other_reply['folders']) and
              not other_folders[other_objects]['items'] and
              str(category) not in {folder['category_id'] for folder in other_folders[other_root]['categories']},
              'First account inventory appeared in the other account')
        try:
            post(other_caps['CreateInventoryCategory'], dict(folder_id=uuid.uuid4(),
                parent_id=uuid.UUID(inventory_root), name='Forbidden mixed-grid category', type=-1))
        except urllib.error.HTTPError as error:
            check(error.code == 400, f'Unexpected cross-account category status {error.code}')
        else:
            raise AssertionError('Other account could create a category under the first account root')
        other_category = uuid.uuid4()
        created = post(other_caps['CreateInventoryCategory'], dict(folder_id=other_category,
            parent_id=uuid.UUID(other_root), name="Other's own & folder", type=-1))
        check(created.get('folder_id') == str(other_category), 'Other account could not create its own category')
        first_root_reply = post(caps['FetchInventoryDescendents2'], dict(folders=[dict(
            folder_id=uuid.UUID(inventory_root), owner_id=uuid.UUID(restored_login['agent_id']),
            fetch_folders=True, fetch_items=True, sort_order=0)]))
        check({folder['category_id'] for folder in first_root_reply['folders'][0]['categories']} == expected_categories,
              'Other account category creation changed the first account inventory')
        report['mutualMovementObservations'] = [observe_forward_movement(restored, other),
                                                observe_forward_movement(other, restored)]
        report['twoAccountsSimultaneousAndVisible'] = True
        report['remoteAccountInventoryAppearanceIsolation'] = True
        report['otherOwnCategoryCreatedWithoutCrossAccountWrite'] = str(other_category)
        check_native_local_service_rows(native_root)
        report['nativeLocalServiceRowsAfterRestart'] = 0
        report['appearanceInventoryBuildingRestartVerified'] = True
        report['allThreeProcessesRestarted'] = True
        report['avatarLastPositionRestored'] = [140, 141]
        report['editedRegionObjectRestored'] = str(object_id)
        report['objectInventoryCopyAndAssetRestored'] = dict(item=copied_item, asset=copied_asset, bytes=len(copied_data))
    finally:
        stop_servers(started, root_server, viewers)
    report['restartCleanShutdown'] = True
    (args.work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: original/native grid travel, inventory, assets, appearance and object state across clean process restart')


if __name__ == '__main__':
    main()
