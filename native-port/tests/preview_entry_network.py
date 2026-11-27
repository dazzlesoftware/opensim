"""Fresh preview accounts through CLI, XML-RPC, UDP and capabilities; no SQL writes."""

import argparse
import contextlib
import json
from pathlib import Path
import platform
import queue
import socket
import sqlite3
import struct
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client


def check(value, message):
    if not value:
        raise AssertionError(message)


def llsd(value):
    if isinstance(value, dict):
        return '<map>' + ''.join('<key>' + key + '</key>' + llsd(item) for key, item in value.items()) + '</map>'
    if isinstance(value, list):
        return '<array>' + ''.join(map(llsd, value)) + '</array>'
    if isinstance(value, uuid.UUID):
        return '<uuid>' + str(value) + '</uuid>'
    if isinstance(value, bool):
        return '<boolean>' + str(int(value)) + '</boolean>'
    if isinstance(value, int):
        return '<integer>' + str(value) + '</integer>'
    node = ET.Element('string')
    node.text = value
    return ET.tostring(node, encoding='unicode')


def decode(node):
    children = list(node)
    if node.tag == 'llsd':
        return decode(children[0])
    if node.tag == 'map':
        return {children[i].text: decode(children[i + 1]) for i in range(0, len(children), 2)}
    if node.tag == 'array':
        return list(map(decode, children))
    if node.tag == 'integer':
        return int(node.text or 0)
    return node.text or ''


def post(url, value):
    request = urllib.request.Request(url, ('<llsd>' + llsd(value) + '</llsd>').encode(),
                                     {'Content-Type': 'application/llsd+xml'})
    with urllib.request.urlopen(request, timeout=10) as response:
        return decode(ET.fromstring(response.read()))


class PreviewServer:
    def __init__(self, command, root, log_path):
        self.command = command
        self.root = root
        self.log_path = log_path
        self.log = []
        self.process = None
        self.reader = None

    def start(self):
        check(self.process is None, 'Preview server is already running')
        output = queue.Queue()
        process = subprocess.Popen(
            self.command + ['--console'], cwd=self.root, text=True,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.process = process

        def read():
            for line in process.stdout:
                self.log.append(line)
                output.put(line)
            output.put(None)

        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        deadline = time.monotonic() + 40
        while True:
            line = output.get(timeout=max(.01, deadline - time.monotonic()))
            check(line is not None, ''.join(self.log))
            if line.startswith('LISTENING '):
                return line.split(' ', 1)[1].strip().replace('://0.0.0.0:', '://127.0.0.1:')

    def stop(self):
        if self.process is None:
            return
        process = self.process
        try:
            if process.poll() is None:
                process.stdin.write('quit\n')
                process.stdin.flush()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise AssertionError('Preview server failed to stop cleanly')
            check(process.returncode == 0, 'Preview server exited abnormally')
        finally:
            self.reader.join(timeout=3)
            self.log_path.write_text(''.join(self.log), encoding='utf-8')
            self.process = None


class Viewer:
    def __init__(self, login):
        self.login = login
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(0.5)
        self.endpoint = (login['sim_ip'], login['sim_port'])
        self.sequence = 0
        self.received = []
        self.acknowledged_sequences = set()

    def send(self, payload):
        self.sequence += 1
        self.socket.sendto(struct.pack('>BIB', 0x40, self.sequence, 0) + payload, self.endpoint)

    def wait(self, message, contains=b'', predicate=lambda data: True, timeout=10, drop=None):
        for data in self.received:
            if data.startswith(message) and contains in data and predicate(data):
                return data
        deadline = time.monotonic() + timeout
        seen = set()
        while time.monotonic() < deadline:
            try:
                packet, _ = self.socket.recvfrom(65536)
            except socket.timeout:
                continue
            if packet[0] & 0x10:
                count = packet[-1]
                offset = len(packet) - 1 - 4 * count
                check(offset >= 6, 'Malformed appended acknowledgement trailer')
                for i in range(count):
                    self.acknowledged_sequences.add(struct.unpack_from('>I', packet, offset + 4 * i)[0])
                packet = packet[:offset]
            data = packet[6:]
            if packet[0] & 0x80:
                expanded = bytearray()
                i = 0
                while i < len(data):
                    byte = data[i]
                    i += 1
                    if byte == 0:
                        expanded.extend(bytes(data[i]))
                        i += 1
                    else:
                        expanded.append(byte)
                data = bytes(expanded)
            if drop and drop(packet, data):
                continue
            if data.startswith(bytes.fromhex('fffffffb')):
                count = data[4]
                check(len(data) >= 5 + count * 4, 'Malformed acknowledgement packet')
                for i in range(count):
                    self.acknowledged_sequences.add(struct.unpack_from('<I', data, 5 + i * 4)[0])
            if packet[0] & 0x40:
                self.sequence += 1
                ack = bytes.fromhex('fffffffb') + b'\x01' + struct.pack('<I', struct.unpack('>I', packet[1:5])[0])
                self.socket.sendto(struct.pack('>BIB', 0, self.sequence, 0) + ack, self.endpoint)
            seen.add(data[:4].hex())
            self.received.append(data)
            if data.startswith(message) and contains in data and predicate(data):
                return data
        raise AssertionError(f'Preview UDP timeout waiting for {message.hex()}; received {sorted(seen)}')

    def connect(self, circuit_repeats=1, movement_first=True, handshake_drops=0):
        agent = uuid.UUID(self.login['agent_id']).bytes
        session = uuid.UUID(self.login['session_id']).bytes
        code = struct.pack('<I', int(self.login['circuit_code']))
        for _ in range(circuit_repeats):
            self.send(bytes.fromhex('ffff0003') + code + session + agent)
        if movement_first:
            self.send(bytes.fromhex('ffff00f9') + agent + session + code)
        observations = []

        def drop_handshake(packet, data):
            if not data.startswith(bytes.fromhex('ffff0094')):
                return False
            observations.append(dict(sequence=struct.unpack_from('>I', packet, 1)[0], flags=packet[0]))
            return len(observations) <= handshake_drops

        self.wait(bytes.fromhex('ffff0094'), timeout=30 if handshake_drops else 10,
                  drop=drop_handshake if handshake_drops else None)
        if handshake_drops:
            check(len(observations) == handshake_drops + 1
                  and len({row['sequence'] for row in observations}) == 1
                  and all(row['flags'] & 0x40 for row in observations)
                  and all(row['flags'] & 0x20 for row in observations[1:]),
                  f'Handshake loss must recover through reliable resends of the same packet: {observations}')
        self.handshake_loss_observations = observations
        self.send(bytes.fromhex('ffff0095') + agent + session + struct.pack('<I', 0))
        if not movement_first:
            self.send(bytes.fromhex('ffff00f9') + agent + session + code)
        self.wait(bytes.fromhex('ffff00fa'))

    def credentials(self):
        return uuid.UUID(self.login['agent_id']).bytes + uuid.UUID(self.login['session_id']).bytes

    @staticmethod
    def visual_parameters(data):
        texture_size = struct.unpack_from('<H', data, 21)[0]
        count_offset = 23 + texture_size
        count = data[count_offset]
        return data[count_offset + 1:count_offset + 1 + count]

    def change_appearance(self):
        previous = self.wait(bytes.fromhex('ffff009e'), uuid.UUID(self.login['agent_id']).bytes)
        parameters = bytearray(self.visual_parameters(previous))
        check(bool(parameters), 'Initial avatar appearance has no visual parameters')
        parameters[0] = (parameters[0] + 1) % 256
        body = self.credentials() + struct.pack('<I3f', 73, .45, .6, 1.9)
        body += b'\x00' + struct.pack('<H', 0) + bytes([len(parameters)]) + parameters
        self.send(bytes.fromhex('ffff0054') + body)
        return bytes(parameters)

    def create_box(self):
        # ObjectAdd: normal cube geometry and an intersecting viewer rez ray.
        shape = struct.pack('<BBIBBHH', 9, 3, 2, 16, 1, 0, 0)
        shape += bytes([100, 100, 0, 0])
        shape += bytes(7)  # Twist, radius, taper, revolutions and skew.
        shape += struct.pack('<HHH', 0, 0, 0)
        shape += b'\x01' + struct.pack('<6f', 128, 128, 70, 130, 129, 25)
        shape += bytes(16) + b'\x01'
        shape += struct.pack('<6fB', .5, .5, .5, 0, 0, 0, 0)
        self.send(bytes.fromhex('ff01') + self.credentials() + bytes(16) + shape)
        update = self.wait(b'\x0c', predicate=lambda data: len(data) > 53 and data[37] == 9)
        local = struct.unpack_from('<I', update, 12)[0]
        identity = uuid.UUID(bytes=update[17:33])
        check(identity.int != 0, 'Rezzed prim has an empty UUID')
        return local, identity

    def edit_box(self, local, identity):
        # The viewer sends supported position/scale and position/rotation forms.
        for kind, transform in ((5, (132, 131, 30, .75, 1, 1.25)),
                                (3, (132, 131, 30, 0, 0, .5))):
            values = struct.pack('<6f', *transform)
            payload = self.credentials() + b'\x01' + struct.pack('<IBB', local, kind, len(values)) + values
            self.send(bytes.fromhex('ff02') + payload)
        self.wait_for_box(identity)

    def wait_for_box(self, identity):
        def edited(data):
            return (len(data) > 101 and data[17:33] == identity.bytes
                    and struct.unpack_from('<3f', data, 40) == (.75, 1, 1.25)
                    and struct.unpack_from('<3f', data, 53) == (132, 131, 30)
                    and struct.unpack_from('<f', data, 97)[0] == .5)
        return self.wait(b'\x0c', predicate=edited)

    def take_copy(self, local):
        body = self.credentials() + bytes(16) + b'\x01' + bytes(16) + uuid.uuid4().bytes
        body += b'\x01\x00\x01' + struct.pack('<I', local)
        self.send(bytes.fromhex('ffff0123') + body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--viewer', type=Path)
    parser.add_argument('--login-cycles', type=int, default=5)
    parser.add_argument('--endpoint-recovery-cycles', type=int, default=0)
    parser.add_argument('--handshake-drops', type=int, default=0)
    args = parser.parse_args()
    check(1 <= args.login_cycles <= 200, 'Login cycles must be between 1 and 200')
    check(0 <= args.endpoint_recovery_cycles <= 200, 'Endpoint recovery cycles must be between 0 and 200')
    check(0 <= args.handshake_drops <= 2, 'Handshake drops must be between 0 and 2')
    args.native = args.native.resolve()
    if args.viewer:
        args.viewer = args.viewer.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    result_path = args.work / 'results.json'
    result_path.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work, prefix='fresh-preview-') as temporary:
        root = Path(temporary)
        config = root / 'region.ini'
        config.write_text('''[NativeServer]
ListenAddress=0.0.0.0
EnableLocalRegion=true
EnableRegionAssets=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port=0
[NativeRegion]
ID=11111111-1111-4111-8111-111111111111
Name=Fresh Preview
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
[Appearance]
DelayBeforeAppearanceSave=30
''', encoding='utf-8')
        control = args.native.parent / 'share/opensim-native/assets/AssetSets.xml'
        if not control.is_file():
            control = args.native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
        check(control.is_file(), 'The tested native runtime must include its bundled asset set')
        with config.open('a', encoding='utf-8') as settings:
            settings.write(f'''\n[Modules]
AssetServices=LocalAssetServicesConnector
AssetCaching=FlotsamAssetCache
[AssetCache]
FileCacheEnabled=false
MemoryCacheEnabled=true
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{root / 'assets.db'};version=3;Pooling=false
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs={control}
[ClientStack.LindenCaps]
Cap_GetAsset=localhost
Cap_GetTexture=localhost
Cap_UploadBakedTexture=localhost
''')
        command = [str(args.native), '--config', str(config)]
        setup = [(['--init-db'], None),
                 (['--create-user', 'Preview', 'First'], 'preview-password\n'),
                 (['--create-user', 'Preview', 'Second'], 'preview-password\n')]
        if args.viewer:
            setup += [(['--create-user', 'Codec', last], 'preview-password\n') for last in ('First', 'Second')]
        for options, password in setup:
            result = subprocess.run(command + options, cwd=root, input=password, text=True,
                                    capture_output=True, timeout=40)
            check(result.returncode == 0, result.stdout + result.stderr)
        server = PreviewServer(command, root, args.work / 'server.log')
        viewers = []
        checks = []
        try:
            url = server.start()
            with urllib.request.urlopen(url + 'get_grid_info', timeout=10) as response:
                grid = ET.fromstring(response.read())
                check(grid.findtext('login', '').rstrip('/') == url.rstrip('/'),
                      f'Grid discovery must advertise a reachable login URI: {ET.tostring(grid)!r} versus {url}')
            check(not any('Startup map generation failed' in line for line in server.log),
                  'Fresh startup must generate its map without missing region settings')
            with contextlib.closing(sqlite3.connect(f'file:{root / "region.db"}?mode=ro', uri=True)) as database:
                settings = database.execute('SELECT agent_limit,object_bonus,allow_land_resell,'
                                            'allow_land_join_divide,water_height,terrain_raise_limit,'
                                            'terrain_lower_limit,cacheID,map_tile_ID FROM regionsettings').fetchall()
                check(len(settings) == 1 and settings[0][:7] == (40, 1, 1, 1, 20, 100, -100),
                      'First region load must persist source-compatible default settings')
                check(uuid.UUID(settings[0][7]).int and uuid.UUID(settings[0][8]).int,
                      'Fresh region must persist a cache ID and generated terrain map asset ID')
            checks.append('Wildcard binding advertises a reachable login URI; fresh region settings and map persist')
            proxy = xmlrpc.client.ServerProxy(url)
            sessions = []
            for last in ('First', 'Second'):
                login = proxy.login_to_simulator(dict(first='Preview', last=last, passwd='preview-password',
                                                      start='home', channel='Preview acceptance', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewers.append(viewer)
                viewer.connect(handshake_drops=args.handshake_drops)
                sessions.append(login)
                checks.append(f'{last}: fresh CLI account, login and UDP arrival')
            handshake_loss = [viewer.handshake_loss_observations for viewer in viewers]
            if args.handshake_drops:
                checks.append(f'Both accounts recover after discarding {args.handshake_drops} reliable handshake deliveries without acknowledging them')
            check(sessions[0]['agent_id'] != sessions[1]['agent_id'], 'Accounts share an agent ID')
            for viewer, peer in zip(viewers, reversed(sessions)):
                viewer.wait(b'\x0c', uuid.UUID(peer['agent_id']).bytes)
            checks.append('Both connected accounts receive the other avatar ObjectUpdate')
            for viewer in viewers:
                viewer.send(bytes.fromhex('ffff017d') + uuid.UUID(viewer.login['agent_id']).bytes
                            + uuid.UUID(viewer.login['session_id']).bytes)
            with contextlib.closing(sqlite3.connect(f'file:{root / "inventory.db"}?mode=ro', uri=True)) as database:
                for login in sessions:
                    owner = login['agent_id']
                    folders = database.execute('SELECT folderID,type FROM inventoryfolders WHERE agentID=?', (owner,)).fetchall()
                    types = {row[1] for row in folders}
                    check({8, 5, 13, 46}.issubset(types), f'Missing required folders: {types}')
                    items = database.execute('SELECT inventoryID,assetType,parentFolderID FROM inventoryitems WHERE avatarID=?', (owner,)).fetchall()
                    check(sum(row[1] == 24 for row in items) == 6 and len(items) == 12, 'Missing default wearables/outfit links')
                    check(all(row[2] in {folder[0] for folder in folders} for row in items),
                          f'Cross-account inventory parent: folders={folders}, items={items}')
                    wearable_packet = viewers[sessions.index(login)].wait(bytes.fromhex('ffff017e'))
                    count = wearable_packet[40]
                    advertised = {str(uuid.UUID(bytes=wearable_packet[41 + index * 33:57 + index * 33]))
                                  for index in range(count)}
                    check(advertised == {row[0] for row in items if row[1] != 24},
                          'UDP wearables do not match the account inventory')
                    asset_caps = post(login['seed_capability'], ['ViewerAsset'])
                    assets = database.execute('SELECT assetID,assetType FROM inventoryitems WHERE avatarID=? AND assetType!=24', (owner,)).fetchall()
                    for asset, kind in assets:
                        key = 'bodypart_id' if kind == 13 else 'clothing_id'
                        with urllib.request.urlopen(asset_caps['ViewerAsset'] + '?' + key + '=' + asset, timeout=10) as response:
                            check(response.status == 200 and response.read().startswith(b'LLWearable'),
                                  'Default wearable asset is unavailable')
                    checks.append(f'{owner}: six default wearables and six own current-outfit links')
            checks.append('Both accounts receive their inventory wearable IDs and can fetch all six wearable assets')
            bake_caps = [post(login['seed_capability'], ['UploadBakedTexture', 'GetTexture']) for login in sessions]
            upload = post(bake_caps[0]['UploadBakedTexture'], {})
            check(upload.get('state') == 'upload', 'Bake capability did not issue an uploader')
            texture_file = control.parent / 'TexturesAssetSet/default_avatar.jp2'
            texture_bytes = texture_file.read_bytes()
            request = urllib.request.Request(upload['uploader'], texture_bytes,
                                             {'Content-Type': 'application/octet-stream'})
            with urllib.request.urlopen(request, timeout=10) as response:
                uploaded = decode(ET.fromstring(response.read()))
            check(uploaded.get('state') == 'complete', 'Baked texture upload did not complete')
            with urllib.request.urlopen(bake_caps[1]['GetTexture'] + '?texture_id=' + uploaded['new_asset'],
                                        timeout=10) as response:
                check(response.status == 200 and response.read() == texture_bytes,
                      'The second account cannot retrieve the uploaded baked texture')
            checks.append('One account uploads a baked texture and the other retrieves its exact bytes')
            caps = [post(login['seed_capability'], ['CreateInventoryCategory']) for login in sessions]
            roots = [login['inventory-root'][0]['folder_id'] for login in sessions]
            folder = uuid.uuid4()
            created = post(caps[0]['CreateInventoryCategory'], dict(folder_id=folder,
                           parent_id=uuid.UUID(roots[0]), name='Preview folder', type=-1))
            check(created['folder_id'] == str(folder), 'CreateInventoryCategory did not create folder')
            try:
                post(caps[1]['CreateInventoryCategory'], dict(folder_id=uuid.uuid4(),
                     parent_id=uuid.UUID(roots[0]), name='Forbidden folder', type=-1))
            except urllib.error.HTTPError as error:
                check(error.code == 400, f'Unexpected cross-account status {error.code}')
            else:
                raise AssertionError('Cross-account folder creation accepted')
            checks.append('CreateInventoryCategory creates own folder and rejects other account parent')
            if args.viewer:
                for last in ('First', 'Second'):
                    login = proxy.login_to_simulator(dict(first='Codec', last=last, passwd='preview-password',
                        start='home', channel='Native preview acceptance', version='1.0'))
                    check(login.get('login') == 'true', str(login))
                    result = subprocess.run([str(args.viewer), login['agent_id'], login['session_id'],
                        str(login['circuit_code']), login['sim_ip'], str(login['sim_port']),
                        '--check-scripting-enabled'], capture_output=True, text=True, timeout=20)
                    check(result.returncode == 0, 'Native viewer handshake: ' + result.stdout + result.stderr)
                checks.append('Two fresh accounts complete native codec/socket viewer process handshakes')
            local, object_id = viewers[0].create_box()
            viewers[0].edit_box(local, object_id)
            viewers[1].wait_for_box(object_id)
            checks.append('Viewer rezzes and edits a cube; both accounts receive its updated transform')
            viewers[0].take_copy(local)
            with contextlib.closing(sqlite3.connect(f'file:{root / "inventory.db"}?mode=ro', uri=True)) as database:
                deadline = time.monotonic() + 5
                while True:
                    copied = database.execute(
                        'SELECT inventoryID,assetID FROM inventoryitems WHERE avatarID=? AND assetType=6',
                        (sessions[0]['agent_id'],)).fetchall()
                    if copied or time.monotonic() >= deadline:
                        break
                    time.sleep(.05)
                check(len(copied) == 1, 'Taking a copy of a newly created prim did not create its inventory item')
            checks.append('Taking a copy stores a new object inventory item through ordinary viewer packets')
            parameters = viewers[0].change_appearance()
            viewers[1].wait(bytes.fromhex('ffff009e'), uuid.UUID(sessions[0]['agent_id']).bytes,
                            predicate=lambda data: Viewer.visual_parameters(data) == parameters)
            server.stop()
            for viewer in viewers:
                viewer.socket.close()
            viewers.clear()
            proxy = xmlrpc.client.ServerProxy(server.start())
            for last, previous in zip(('First', 'Second'), sessions):
                login = proxy.login_to_simulator(dict(
                    first='Preview', last=last, passwd='preview-password', start='last',
                    channel='Preview restart acceptance', version='1.0'))
                check(login.get('login') == 'true' and login['agent_id'] == previous['agent_id'],
                      'Clean restart did not restore the existing account')
                viewer = Viewer(login)
                viewers.append(viewer)
                viewer.connect()
                viewer.wait_for_box(object_id)
            with contextlib.closing(sqlite3.connect(f'file:{root / "inventory.db"}?mode=ro', uri=True)) as database:
                saved = database.execute('SELECT folderID,agentID,parentFolderID FROM inventoryfolders WHERE folderID=?',
                                         (str(folder),)).fetchone()
                check(saved == (str(folder), sessions[0]['agent_id'], roots[0]),
                      'Capability-created folder did not survive clean restart')
            checks.append('Clean restart restores the edited prim and capability-created inventory folder')
            with contextlib.closing(sqlite3.connect(f'file:{root / "avatars.db"}?mode=ro', uri=True)) as database:
                saved = database.execute('SELECT Value FROM Avatars WHERE PrincipalID=? AND Name=?',
                                         (sessions[0]['agent_id'], 'VisualParams')).fetchone()
                check(saved == (','.join(map(str, parameters)),),
                      'Clean restart lost an appearance update waiting for its delayed save')
            viewers[0].send(bytes.fromhex('ffff017d') + viewers[0].credentials())
            viewers[1].wait(bytes.fromhex('ffff009e'), uuid.UUID(sessions[0]['agent_id']).bytes,
                            predicate=lambda data: Viewer.visual_parameters(data) == parameters)
            checks.append('Clean restart flushes pending avatar appearance and advertises the restored parameters')
            with contextlib.closing(sqlite3.connect(f'file:{root / "region.db"}?mode=ro', uri=True)) as database:
                check(database.execute('SELECT cacheID FROM regionsettings').fetchall() == [(settings[0][7],)],
                      'Reloading a region must retain its existing settings row and cache ID')
            for cycle in range(args.endpoint_recovery_cycles):
                for index in range(2):
                    previous = viewers[index]
                    current = Viewer(previous.login)
                    viewers[index] = current
                    try:
                        # The original AddClient closes a client on another UDP
                        # endpoint before startup. Scene removal deletes its
                        # circuit, so this is not a normal fresh login.
                        current.send(bytes.fromhex('ffff0003')
                                     + struct.pack('<I', int(previous.login['circuit_code']))
                                     + uuid.UUID(previous.login['session_id']).bytes
                                     + uuid.UUID(previous.login['agent_id']).bytes)
                        previous.wait(bytes.fromhex('ffff0098'))  # DisableSimulator
                        current.wait(bytes.fromhex('fffffffb'))  # Request was ACKed, not admitted.
                        deadline = time.monotonic() + 10
                        while True:
                            try:
                                post(previous.login['seed_capability'], [])
                            except urllib.error.HTTPError as error:
                                if error.code == 404:
                                    break
                                raise
                            check(time.monotonic() < deadline, 'Replaced endpoint retained its old capability')
                            time.sleep(.025)
                        try:
                            current.wait(bytes.fromhex('ffff0094'), timeout=.5)
                        except AssertionError:
                            pass
                        else:
                            raise AssertionError('Removed circuit unexpectedly admitted the old session')
                        login = proxy.login_to_simulator(dict(
                            first='Preview', last=('First', 'Second')[index], passwd='preview-password',
                            start='last', channel='Preview endpoint recovery', version='1.0'))
                        check(login.get('login') == 'true' and login['session_id'] != previous.login['session_id'],
                              f'Fresh login after endpoint replacement failed: {login}')
                        current.socket.close()
                        current = Viewer(login)
                        viewers[index] = current
                        current.connect(circuit_repeats=3)
                    finally:
                        previous.socket.close()
            if args.endpoint_recovery_cycles:
                checks.append(f'Both accounts recover through {args.endpoint_recovery_cycles} fresh logins after source-compatible endpoint replacement rejection; repeated circuit requests complete')
            for cycle in range(args.login_cycles):
                for index, last in enumerate(('First', 'Second')):
                    previous = viewers[index]
                    previous.send(bytes.fromhex('ffff00fc') + previous.credentials())
                    previous.wait(bytes.fromhex('ffff00fd'))
                    deadline = time.monotonic() + 10
                    while True:
                        try:
                            post(previous.login['seed_capability'], [])
                        except urllib.error.HTTPError as error:
                            if error.code == 404:
                                break
                            raise
                        check(time.monotonic() < deadline, 'Logout must remove the previous seed capability')
                        time.sleep(.025)
                    login = proxy.login_to_simulator(dict(
                        first='Preview', last=last, passwd='preview-password', start='last',
                        channel='Preview repeated login', version='1.0'))
                    check(login.get('login') == 'true' and login['agent_id'] == previous.login['agent_id']
                          and login['session_id'] != previous.login['session_id'],
                          f'Relogin failed for {last} in cycle {cycle}')
                    current = Viewer(login)
                    viewers[index] = current
                    previous.socket.close()
                    current.connect()
                    viewers[1-index].wait(b'\x0c', uuid.UUID(login['agent_id']).bytes)
            checks.append(f'Both accounts complete {args.login_cycles} logout/fresh-session/UDP-handshake cycles after restart')
            result_path.write_text(json.dumps(dict(platform=platform.system(), passed=True,
                checks=checks, handshakeLoss=handshake_loss, realFirestormVerified=False), indent=2), encoding='utf-8')
            print(f'PASS: {len(checks)} fresh preview entry checks; real Firestorm remains pending')
        finally:
            for viewer in viewers:
                viewer.socket.close()
            server.stop()


if __name__ == '__main__':
    main()
