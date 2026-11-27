"""Real executable coverage of original local startup composition and login."""
import argparse
import base64
import gzip
import json
import os
import socket
import sqlite3
import struct
from contextlib import closing
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs
from uuid import UUID
from xml.etree import ElementTree
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time
import urllib.request
import xmlrpc.client
from simulator_features_network import decode
from legacy_material_restart_network import viewer_channel
from preview_entry_network import post


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--install', nargs=3, metavar=('CMAKE', 'BUILD_DIR', 'CONFIG'))
    args = parser.parse_args()
    native = str(args.native.resolve())
    args.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)

    with tempfile.TemporaryDirectory(dir=args.work) as temporary:
        root = Path(temporary).resolve()
        sample = Path(__file__).resolve().parents[1] / 'native-region.ini.example'
        if args.install:
            cmake, build_dir, configuration = args.install
            prefix = root / 'install with spaces'
            command = [cmake, '--install', build_dir, '--prefix', str(prefix)]
            if configuration:
                command.extend(['--config', configuration])
            install = subprocess.run(command, capture_output=True, text=True,
                                     encoding='utf-8', errors='replace', timeout=300)
            check(install.returncode == 0,
                  'Install native runtime: ' + install.stderr + install.stdout[-4000:])
            native = str(prefix / 'bin' / args.native.name)
            data = prefix / 'share/opensim-native'
            sample = data / 'native-region.ini.example'
            for resource in (
                sample, data / 'avataranimations.xml', data / 'inventory/Libraries.xml',
                data / 'assets/AssetSets.xml', data / 'assets/AnimationsAssetSet/place_marker.dat',
            ):
                check(resource.is_file() and resource.stat().st_size > 0,
                      f'Installed runtime includes {resource.relative_to(prefix)}')
            # A neighboring source-style layout must not shadow the installed libraries.
            source_libraries = prefix / 'bin/lib64'
            source_libraries.mkdir()
            libraries = list((prefix / 'lib/opensim-native').iterdir())
            for family in ('ubode', 'openjpeg-dotnet'):
                check(any(family in library.name.lower() and library.is_file() and library.stat().st_size > 0
                          for library in libraries), f'Installed runtime includes the {family} library')
            for library in libraries:
                (source_libraries / library.name).write_bytes(b'invalid native library')
            if os.name != 'nt':
                # Launch through a link outside the install tree, as a PATH entry might.
                launcher = root / 'external launcher' / args.native.name
                launcher.parent.mkdir()
                launcher.symlink_to(os.path.relpath(native, launcher.parent))
                native = str(launcher)
        (root / 'parts').mkdir()
        (root / 'config').mkdir()
        (root / 'config/override.INI').write_text('[NativeRegion]\nLocationY=4321\n', encoding='utf-8')
        config = root / 'startup.ini'
        config.write_text('''[Startup]
Include-services=parts/services.ini
[Environment]
OPENSIM_NATIVE_STARTUP_PORT=ignored
OPENSIM_NATIVE_STARTUP_REGION=ignored
OPENSIM_NATIVE_STARTUP_X=ignored
[Network]
http_listener_port=${Environment|OPENSIM_NATIVE_STARTUP_PORT}
''', encoding='utf-8')
        (root / 'parts/services.ini').write_text('''[Startup]
Include-region=region.ini
Include-cycle=startup.ini
[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableAssetEndpoint=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
PresenceDatabase=presence.db
[AssetService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:assets.db;version=3
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs=assets/AssetSets.xml
''', encoding='utf-8')
        # Original AddIncludes resolves every relative include against configDir,
        # including a reference discovered in a file in another directory.
        (root / 'region.ini').write_text('''[NativeRegion]
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
Name=${Environment|OPENSIM_NATIVE_STARTUP_REGION}
LocationX=${Environment|OPENSIM_NATIVE_STARTUP_X}
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
''', encoding='utf-8')
        env = dict(os.environ, OPENSIM_NATIVE_STARTUP_PORT='0',
                   OPENSIM_NATIVE_STARTUP_REGION='Included Startup Region',
                   OPENSIM_NATIVE_STARTUP_X='6789')
        command = [native, '--config', str(config)]
        for options, password in [(['--init-db'], None),
                                  (['--create-user', 'Startup', 'Viewer'], 'startup-password\n'),
                                  (['--create-user', 'Startup', 'Recipient'], 'startup-password\n')]:
            result = subprocess.run(command + options, input=password,
                                    capture_output=True, text=True, env=env,
                                    cwd=root, timeout=45)
            check(result.returncode == 0, options[0] + ': ' + result.stderr)
        check(all((root / name).is_file() for name in
                  ['users.db', 'inventory.db', 'gridusers.db']),
              'Included service databases resolve against main config directory')

        def login(extra, process_env, label, coordinates=(6789, 4321), check_asset_endpoint=True,
                  check_texture_decoder=False, check_neighbour=False, start='home', expected_regions=1,
                  offline_store=None, instant_messages_enabled=True, inbound_land_enabled=False,
                  verify_physics=False, presence_cleanup_sessions=None):
            process = subprocess.Popen(command + extra + ['--console'], env=process_env,
                                       cwd=root, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding='utf-8')
            lines = queue.Queue()
            errors = []
            output_lines = []
            def output():
                for line in process.stdout:
                    output_lines.append(line)
                    lines.put(line)
                lines.put(None)
            def error():
                errors.extend(process.stderr)
            threads = [threading.Thread(target=output, daemon=True),
                       threading.Thread(target=error, daemon=True)]
            for thread in threads:
                thread.start()
            try:
                deadline = time.monotonic() + 30
                url = None
                while time.monotonic() < deadline:
                    line = lines.get(timeout=max(.01, deadline-time.monotonic()))
                    if line is None:
                        raise AssertionError(label + ' startup: ' + ''.join(errors))
                    if line.startswith('LISTENING '):
                        url = line.split(' ', 1)[1].strip()
                        break
                check(bool(url), label + ' starts listener')
                if presence_cleanup_sessions is not None:
                    with closing(sqlite3.connect(root / 'presence.db')) as database:
                        sessions = database.execute(
                            'SELECT SessionID FROM Presence WHERE SessionID IN (?,?) ORDER BY SessionID',
                            presence_cleanup_sessions,
                        ).fetchall()
                    check(sessions == [(presence_cleanup_sessions[1],)],
                          label + ' clears stale local presence only for the hosted region')
                udp_endpoints = [line.strip() for line in output_lines if line.startswith('REGION_UDP ')]
                check(len(set(udp_endpoints)) == expected_regions, label + ' starts every region UDP listener')
                if expected_regions > 1:
                    with urllib.request.urlopen(url + '?method=collector', timeout=5) as reply:
                        snapshot = ElementTree.fromstring(reply.read())
                    check(
                        {node.text for node in snapshot.findall('region/info/name')}
                        == {'Discovered Startup Region', 'Second Startup Region'},
                        label + ' collects snapshots for both regions',
                    )
                    process.stdin.write('change region Second Startup Region\nshow regions\nshow ratings\nterrain stats\n')
                    process.stdin.flush()
                    deadline = time.monotonic() + 5
                    selected = False
                    region_rows = {}
                    ratings = set()
                    while time.monotonic() < deadline:
                        line = lines.get(timeout=max(.01, deadline - time.monotonic()))
                        if line is None:
                            break
                        if 'Currently selected region is Second Startup Region' in line:
                            selected = True
                        for name in ('Discovered Startup Region', 'Second Startup Region'):
                            if line.startswith(name):
                                region_rows[name] = line[len(name):].split()
                        if line.startswith('Region Name: '):
                            ratings.add(line.strip())
                        if selected and 'Channel 512x512' in line:
                            break
                    check(selected and line is not None and 'Channel 512x512' in line,
                          label + ' runs terrain commands against the selected region')
                    check(
                        region_rows.get('Discovered Startup Region', [])[:3] ==
                        ['33333333-3333-4333-8333-333333333333', '1002,1003', '256x256']
                        and region_rows.get('Second Startup Region', [])[:3] ==
                        ['44444444-4444-4444-8444-444444444444', '1010,1011', '512x512']
                        and {row[3] for row in region_rows.values()} ==
                        {endpoint.rsplit(':', 1)[1] for endpoint in udp_endpoints}
                        and region_rows['Discovered Startup Region'][4:] == ['Yes', 'My', 'Estate']
                        and region_rows['Second Startup Region'][4:] == ['Yes', 'Independent', 'Estate'],
                        label + ' lists both regions and their live ports while one region is selected',
                    )
                    check(ratings == {
                        'Region Name: Discovered Startup Region, Region Rating PG',
                        'Region Name: Second Startup Region, Region Rating PG',
                    }, label + ' lists both region ratings while one region is selected')
                    process.stdin.write('region restart notice "Fixture restart" 300\nregion restart abort\n'
                                        'change region root\nregion restart notice "Root must ignore this" 0\n'
                                        'terrain fill 37\nterrain show 1,1\n'
                                        'wind ConfigurableWind avgStrength 3\n'
                                        'debug scene set scripting false\n')
                    process.stdin.flush()
                    deadline = time.monotonic() + 5
                    updated = set()
                    filled = 0
                    wind_updates = 0
                    restart_messages = []
                    while time.monotonic() < deadline:
                        line = lines.get(timeout=max(.01, deadline - time.monotonic()))
                        if line is None or 'Scene scripting disabled' in line:
                            break
                        if 'debug scene scripting = false' in line:
                            updated.add(line.strip())
                        if 'Terrain height at <1, 1> is 37' in line:
                            filled += 1
                        if 'avgStrength set to 3' in line:
                            wind_updates += 1
                        if 'scheduled for restart' in line or 'Region restart aborted' in line:
                            restart_messages.append(line.strip())
                    check(filled == 2, label + ' changes and reads both terrains from the root console')
                    check(wind_updates == 2, label + ' updates wind in both regions from the root console')
                    check(restart_messages == [
                        'Region Second Startup Region scheduled for restart in 300 seconds',
                        'Region restart aborted',
                    ], label + ' schedules and aborts a restart only with a region selected')
                    check(updated == {
                        'Set Discovered Startup Region debug scene scripting = false',
                        'Set Second Startup Region debug scene scripting = false',
                    }, label + ' changes scripting in both regions from the root console')
                with xmlrpc.client.ServerProxy(url) as proxy:
                    credentials = dict(first='Startup', last='Viewer',
                                       passwd='startup-password', start=start,
                                       channel='Native startup test', version='1.0')
                    check(proxy.login_to_simulator({**credentials, 'passwd':'wrong'})['login'] == 'false',
                          label + ' rejects incorrect password')
                    if check_neighbour:
                        login_xml = xmlrpc.client.dumps((credentials,), methodname='login_to_simulator').encode()
                        legacy_login = urllib.request.Request(
                            url, gzip.compress(login_xml),
                            {'Content-Type': 'text/xml', 'X-Content-Encoding': 'gzip'},
                        )
                        with urllib.request.urlopen(legacy_login, timeout=5) as reply:
                            response = xmlrpc.client.loads(reply.read())[0][0]
                    else:
                        response = proxy.login_to_simulator(credentials)
                    check(response.get('login') == 'true', label + ' authenticates and joins included region: ' + str(response))
                    check(response['seed_capability'].startswith(url), label + ' advertises actual listener')
                    check(len(response['inventory-skel-lib']) > 1,
                          label + ' loads bundled library folders from outside the source tree')
                    asset = '8ceccb12-5aee-3584-e4a5-66973a844296'
                    if check_asset_endpoint:
                        asset_url = url + f'assets/{asset}/data'
                    else:
                        seed = urllib.request.Request(
                            response['seed_capability'],
                            b'<llsd><array><string>ViewerAsset</string></array></llsd>',
                            {'Content-Type': 'application/llsd+xml'},
                        )
                        with urllib.request.urlopen(seed, timeout=5) as reply:
                            asset_url = decode(reply.read())['ViewerAsset'] + '?animatn_id=' + asset
                    with urllib.request.urlopen(asset_url, timeout=5) as reply:
                        expected = Path(__file__).resolve().parents[2] / 'bin/assets/AnimationsAssetSet/place_marker.dat'
                        check(reply.read() == expected.read_bytes(),
                              label + ' serves the bundled animation asset bytes')
                    check((response['region_x'], response['region_y']) ==
                          tuple(coordinate * 256 for coordinate in coordinates),
                          label + ' uses the configured region coordinates')
                    if expected_regions == 2:
                        if inbound_land_enabled:
                            origin = ((1010 * 256) << 32) | (1011 * 256)
                            interior = ((1011 * 256) << 32) | (1012 * 256)
                            origin_parcel = proxy.land_data(dict(region_handle=str(origin), x='300', y='320'))
                            interior_parcel = proxy.land_data(dict(region_handle=str(interior), x='44', y='64'))
                            check(bool(origin_parcel.get('GlobalID')) and origin_parcel == interior_parcel,
                                  label + ' resolves equivalent world coordinates through inbound land data')
                            check(proxy.land_data(dict(region_handle=str(interior), x=44.5, y=63.5))
                                  == interior_parcel, label + ' accepts numeric XML-RPC offsets')
                            check(proxy.land_data(dict(region_handle=str(origin)))
                                  == proxy.land_data(dict(region_handle=str(origin), x='0', y='0')),
                                  label + ' treats omitted offsets as zero')
                            try:
                                proxy.land_data(dict(region_handle=str(origin), x='4294967296', y='0'))
                            except xmlrpc.client.Fault as fault:
                                check(fault.faultCode == -32603, label + ' rejects overflowing offsets')
                            else:
                                raise AssertionError(label + ' accepted an overflowing land offset')
                        else:
                            try:
                                proxy.land_data({})
                            except xmlrpc.client.Fault as fault:
                                check(fault.faultCode == -32601, label + ' leaves the inbound land service disabled')
                            else:
                                raise AssertionError(label + ' unexpectedly exposes the land service')
                        source = response['sim_ip'], response['sim_port']
                        agent = UUID(response['agent_id']).bytes
                        session = UUID(response['session_id']).bytes
                        code = struct.pack('<I', response['circuit_code'] & 0xffffffff)
                        handle = ((1002 * 256) << 32) | (1003 * 256)
                        recipient = proxy.login_to_simulator(dict(
                            credentials, last='Recipient', start='uri:Discovered Startup Region&128&128&25',
                        ))
                        check(recipient.get('login') == 'true', label + ' logs in a second viewer')
                        recipient_agent = UUID(recipient['agent_id']).bytes
                        recipient_session = UUID(recipient['session_id']).bytes
                        recipient_code = struct.pack('<I', recipient['circuit_code'] & 0xffffffff)
                        with (socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer,
                              socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as recipient_viewer):
                            send, receive_with_peer = viewer_channel(viewer, source, receive_peer=True)
                            packet_peer = None

                            def receive():
                                nonlocal packet_peer
                                packet, packet_peer = receive_with_peer()
                                return packet
                            send_recipient, receive_recipient = viewer_channel(
                                recipient_viewer, (recipient['sim_ip'], recipient['sim_port']),
                            )

                            def wait_packet(packet_id, read=receive):
                                prefix = bytes.fromhex(packet_id)
                                deadline = time.monotonic() + 8
                                while time.monotonic() < deadline:
                                    packet = read()
                                    if packet.startswith(bytes.fromhex('ffff004a')):
                                        raise AssertionError(f'Local teleport was rejected: {packet!r}')
                                    if packet.startswith(prefix):
                                        return packet
                                raise AssertionError(f'Timed out waiting for packet {packet_id}')

                            send('ffff0003', code + session + agent)
                            send('ffff00f9', agent + session + code)
                            wait_packet('ffff0094')
                            wait_packet('ffff00fa')
                            if verify_physics:
                                # Only this region runs ubODE. A stationary viewer falls from its elevated start.
                                deadline = time.monotonic() + 8
                                while time.monotonic() < deadline:
                                    motion = wait_packet('0f')
                                    offset = 12  # Message ID, region data, and block count.
                                    for _ in range(motion[11]):
                                        size = motion[offset]
                                        offset += 1
                                        data = motion[offset:offset + size]
                                        offset += size
                                        texture_size = struct.unpack_from('<H', motion, offset)[0]
                                        offset += 2 + texture_size
                                        if size == 60 and data[5] == 1 and struct.unpack_from('<f', data, 30)[0] < 55:
                                            break
                                    else:
                                        continue
                                    break
                                else:
                                    raise AssertionError(label + ' did not observe falling avatar motion')
                            parcel_id = struct.pack('<QHHHH', handle, 12, 0, 28, 0)
                            send('ffff0036', agent + session + parcel_id)
                            parcel = wait_packet('ffff0037')
                            check(parcel[4:20] == agent and parcel[20:36] == parcel_id
                                  and b'Discovered Startup Region' in parcel,
                                  label + ' resolves parcel info in the other region without a land HTTP endpoint')
                            send_recipient('ffff0003', recipient_code + recipient_session + recipient_agent)
                            send_recipient('ffff00f9', recipient_agent + recipient_session + recipient_code)
                            wait_packet('ffff0094', receive_recipient)
                            wait_packet('ffff00fa', receive_recipient)
                            interior_handle = ((1011 * 256) << 32) | (1012 * 256)
                            # The handle identifies the containing region. The offsets are region-relative.
                            interior_parcel = struct.pack('<QHHHH', interior_handle, 300, 0, 320, 0)
                            send_recipient('ffff0036', recipient_agent + recipient_session + interior_parcel)
                            interior_reply = wait_packet('ffff0037', receive_recipient)
                            offset = 52
                            for _ in range(2):
                                offset += 1 + interior_reply[offset]
                            world_x, world_y = struct.unpack_from('<2f', interior_reply, offset + 9)
                            check(interior_reply[20:36] == interior_parcel
                                  and b'Second Startup Region' in interior_reply
                                  and (world_x, world_y) == (1010 * 256 + 300, 1011 * 256 + 320),
                                  label + ' resolves an interior tile using region-relative parcel offsets')
                            check(interior_reply[offset + 8] & 0x04 and
                                  interior_reply[36:52] == UUID('55555555-5555-4555-8555-555555555555').bytes,
                                  label + ' preserves the group-owned parcel flag and owner over UDP')
                            send('ffff012d', agent + session + recipient_agent + bytes(16))
                            card = wait_packet('ffff012d', receive_recipient)
                            check(card[4:20] == agent and card[36:52] == recipient_agent,
                                  label + ' delivers the calling card to the viewer in the other region')
                            card_id = str(UUID(bytes=bytes(card[52:68])))
                            with closing(sqlite3.connect(root / 'inventory.db')) as database:
                                item = database.execute(
                                    'SELECT avatarID, creatorID, invType FROM inventoryitems WHERE inventoryID=?',
                                    (card_id,),
                                ).fetchone()
                            check(item == (recipient['agent_id'], response['agent_id'], 2),
                                  label + ' persists the calling card in the recipient inventory')
                            sender_name = b'Startup Viewer\0'
                            def send_message(recipient_id, dialog, message):
                                payload = (
                                    agent + session + b'\0' + recipient_id + bytes(32)
                                    + bytes([1, dialog]) + bytes(20)
                                    + bytes([len(sender_name)]) + sender_name
                                    + struct.pack('<H', len(message)) + message + bytes(2)
                                )
                                send('ffff00fe', payload)

                            if not instant_messages_enabled:
                                send_message(recipient_agent, 0, b'Disabled instant message\0')
                                grid_message = dict(
                                    from_agent_id=response['agent_id'], to_agent_id=recipient['agent_id'],
                                    im_session_id=str(UUID(int=0)), region_id=str(UUID(int=0)),
                                    timestamp='0', from_agent_name='Startup Viewer', message='Disabled grid message',
                                    dialog='AA==', from_group='FALSE', offline='AA==', parent_estate_id='0',
                                    position_x='0', position_y='0', position_z='0', binary_bucket='',
                                )
                                check(proxy.grid_instant_message(grid_message)['success'] == 'TRUE',
                                      label + ' acknowledges a grid message to a logged-in recipient')
                                recipient_viewer.settimeout(0.2)
                                deadline = time.monotonic() + 1
                                while time.monotonic() < deadline:
                                    try:
                                        packet = receive_recipient()
                                    except socket.timeout:
                                        continue
                                    check(not packet.startswith(bytes.fromhex('ffff00fe')),
                                          label + ' suppresses viewer and incoming grid instant messages')
                                recipient_viewer.settimeout(5)
                            else:
                                for dialog, message in ((41, b''), (0, b'Cross-region message\0'), (42, b'')):
                                    send_message(recipient_agent, dialog, message)
                                    received = wait_packet('ffff00fe', receive_recipient)
                                    name_length = received[107]
                                    text_offset = 108 + name_length
                                    text_length = struct.unpack_from('<H', received, text_offset)[0]
                                    check(
                                        received[4:20] == agent and received[37:53] == recipient_agent
                                        and received[85:87] == bytes([0, dialog])
                                        and received[text_offset + 2:text_offset + 2 + text_length].rstrip(b'\0')
                                        == message.rstrip(b'\0'),
                                        label + f' delivers instant-message dialog {dialog} to the other region',
                                    )
                                offline_agent = UUID('88888888-8888-4888-8888-888888888888').bytes
                                if offline_store:
                                    send_message(bytes(16), 0, b'No recipient\0')
                                send_message(offline_agent, 0, b'Undeliverable message\0')
                                if offline_store:
                                    pending, complete = offline_store
                                    fields = pending.get(timeout=5)
                                    check(fields.get('Message') == ['Undeliverable message']
                                          and fields.get('ToAgentID') == [str(UUID(bytes=offline_agent))],
                                          label + ' stores the addressed message and ignores the zero recipient')
                                else:
                                    failure = wait_packet('ffff00fe')
                                    check(
                                        failure[4:20] == agent and failure[37:53] == offline_agent and failure[86] == 20
                                        and b'Unable to send instant message. User is not logged in.' in failure,
                                        label + ' returns the managed failure dialog to the sending viewer',
                                    )
                            event_url = post(response['seed_capability'], ['EventQueueGet'])['EventQueueGet']
                            send('ffff003f', agent + session + struct.pack('<Q6f', handle, 128, 128, 45, 1, 0, 0))
                            event_id = 0
                            finish = None
                            deadline = time.monotonic() + 15
                            while finish is None and time.monotonic() < deadline:
                                batch = post(event_url, dict(ack=event_id, done=0))
                                event_id = batch['id']
                                finish = next((event['body']['Info'][0] for event in batch['events']
                                               if event['message'] == 'TeleportFinish'), None)
                            check(finish is not None, label + ' receives the EventQueue teleport handoff')
                            target = socket.inet_ntoa(base64.b64decode(finish['SimIP'])), finish['SimPort']
                            check(target != source and struct.unpack('>Q', base64.b64decode(finish['RegionHandle']))[0] == handle,
                                  label + ' advertises the other local region during teleport')
                            destination_seed = finish['SeedCapability']
                            check(destination_seed.startswith(url) and destination_seed != response['seed_capability'],
                                  label + ' advertises a distinct destination seed on the shared HTTP listener')
                            send('ffff0003', code + session + agent, target=target)
                            send('ffff00f9', agent + session + code, target=target)
                            wait_packet('ffff0094')
                            arrived = wait_packet('ffff00fa')
                            check(struct.unpack_from('<Q', arrived, 60)[0] == handle,
                                  label + ' completes movement in the other local region')
                            seed_request = urllib.request.Request(
                                destination_seed, b'<llsd><array><string>EventQueueGet</string></array></llsd>',
                                {'Content-Type': 'application/llsd+xml'},
                            )
                            with urllib.request.urlopen(seed_request, timeout=5) as reply:
                                check(decode(reply.read())['EventQueueGet'].startswith(url),
                                      label + ' serves the destination event queue capability')
                            if offline_store:
                                complete.set()
                                confirmation = wait_packet('ffff00fe')
                                check(
                                    packet_peer == target
                                    and confirmation[4:20] == offline_agent and confirmation[37:53] == agent
                                    and b'User is not logged in. Message saved.' in confirmation,
                                    label + ' delivers offline storage confirmation after teleport',
                                )
                    if check_neighbour:
                        endpoint = url + 'region/11111111-1111-4111-8111-111111111111'
                        handle = (response['region_x'] << 32) | response['region_y']
                        for destination, expected in ((handle, True), (handle + 256, False)):
                            message = dict(destination_handle=str(destination),
                                           internal_ep_address='127.0.0.1', internal_ep_port='9000')
                            request = urllib.request.Request(
                                endpoint, json.dumps(message).encode(), {'Content-Type': 'application/json'},
                            )
                            with urllib.request.urlopen(request, timeout=5) as reply:
                                check(json.load(reply) == {'success': expected},
                                      'Neighbour endpoint resolves the destination region handle')
                        message['destination_handle'] = str(handle)
                        compressed = gzip.compress(json.dumps(message).encode())
                        encodings = [
                            {'Content-Type': 'application/x-gzip'},
                            {'Content-Type': 'application/json', 'X-Content-Encoding': 'gzip'},
                        ]
                        features = json.loads(subprocess.check_output([command[0], '--features'], text=True))
                        if features['gzip']:
                            encodings.extend([
                                {'Content-Type': 'application/json', 'Content-Encoding': 'gzip'},
                                {'Content-Type': 'application/x-gzip', 'Content-Encoding': 'gzip'},
                            ])
                        for headers in encodings:
                            request = urllib.request.Request(endpoint, compressed, headers)
                            with urllib.request.urlopen(request, timeout=5) as reply:
                                check(json.load(reply) == {'success': True},
                                      f'Neighbour endpoint accepts compressed JSON with {headers}')
            finally:
                if process.poll() is None:
                    try:
                        process.stdin.write('quit\n')
                        process.stdin.flush()
                    except (OSError, BrokenPipeError):
                        pass
                    try:
                        process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
                for thread in threads:
                    thread.join(timeout=3)
            check(process.returncode == 0, label + ' exits cleanly: ' + ''.join(errors))
            if check_texture_decoder:
                diagnostics = ''.join(output_lines + errors)
                check('JPEG 2000 layer decoder:' not in diagnostics,
                      label + ' loads the bundled texture decoder: ' + diagnostics)

        def rejects(extra, process_env, expected, label):
            result = subprocess.run(command + extra + ['--console'], env=process_env,
                                    cwd=root, capture_output=True, text=True, timeout=30)
            check(result.returncode != 0 and expected in result.stderr and
                  'LISTENING ' not in result.stdout,
                  label + ': ' + result.stderr)

        login([], env, 'Local includes and environment')
        # argv merge must happen before NetworkServersInfo's typed read. A later
        # optional.value_or(GetInt(...)) still evaluates and rejects this value.
        login(['--port', '0', '--host', '127.0.0.1'],
              dict(env, OPENSIM_NATIVE_STARTUP_PORT='invalid-port'),
              'Command-line port precedence')
        # Environment expansion occurs before argv merge in the original. An
        # reference absent from Environment fails during that expansion;
        # a later --port must not hide this earlier failure.
        missing_env = dict(env)
        missing_env.pop('OPENSIM_NATIVE_STARTUP_PORT')
        original_main = config.read_text(encoding='utf-8')
        # Remove the declaration as well: a declared missing environment value
        # retains its file fallback when the empty Env source is merged.
        config.write_text(original_main.replace('OPENSIM_NATIVE_STARTUP_PORT=ignored\n', ''), encoding='utf-8')
        rejects(['--port', '0'], missing_env, 'OPENSIM_NATIVE_STARTUP_PORT',
                'Environment failure precedes argv merge')
        config.write_text(original_main, encoding='utf-8')
        services = root / 'parts/services.ini'
        original = services.read_text(encoding='utf-8')
        services.write_text(original.replace('EnableLocalRegion=true',
                                             'EnableLocalRegion=On'), encoding='utf-8')
        rejects([], env, 'Alias value not found: On',
                'File-backed config does not inherit argv aliases')
        services.write_text(original, encoding='utf-8')
        rejects([], dict(env, OPENSIM_NATIVE_STARTUP_X='4294967296'),
                'Region LocationX is out of range',
                'Reject grid coordinates above the managed UInt32 range')
        services.write_text(original.replace('EnableLocalRegion=true\n', ''), encoding='utf-8')
        regions = root / 'Regions'
        regions.mkdir()
        region_file = regions / ('Regions.INI' if os.name == 'nt' else 'Regions.ini')
        region_file.write_text('''[First Region]
RegionUUID=11111111-1111-4111-8111-111111111111
Location=1000,1000
InternalPort=0
[Second Region]
RegionUUID=22222222-2222-4222-8222-222222222222
Location=1001,1000
InternalPort=0
''', encoding='utf-8')
        rejects([], env, 'Duplicate local region UDP port 0',
                'Reject duplicate configured port zero before region startup')
        distinct_regions = region_file.read_text(encoding='utf-8')
        region_file.write_text(distinct_regions.replace('Location=1001,1000', 'Location=1000,1000'),
                               encoding='utf-8')
        rejects([], env, 'Duplicate local region location',
                'Check duplicate grid locations before duplicate ports')
        region_file.write_text(distinct_regions.replace('22222222-2222-4222-8222-222222222222',
                                                        '11111111-1111-4111-8111-111111111111'),
                               encoding='utf-8')
        rejects([], env, 'Duplicate local region ID', 'Check duplicate region IDs before duplicate ports')
        # The managed loader checks first/third before second/third.
        region_file.write_text(distinct_regions.replace('InternalPort=0\n[Second', 'InternalPort=9000\n[Second')
                               + '''[Third Region]
RegionUUID=33333333-3333-4333-8333-333333333333
Location=1001,1000
InternalPort=9000
''', encoding='utf-8')
        rejects([], env, 'Duplicate local region UDP port 9000',
                'Report the first conflicting pair in managed region order')
        region_file.write_text('[Missing UUID]\nLocation=1000,1000\nInternalPort=0\n',
                               encoding='utf-8')
        rejects([], env, 'Region Missing UUID requires a valid RegionUUID',
                'Discovered regions require their own identity')
        region_file.write_text('''[Discovered Startup Region]
RegionUUID=33333333-3333-4333-8333-333333333333
Location=1002,1003
ScopeID=00000000-0000-0000-0000-000000000000trailing
InternalAddress=127.1
InternalPort=0
''', encoding='utf-8')
        config.write_text(original_main + '\n[Modules]\nNeighbourServiceInConnector=true\n', encoding='utf-8')
        stale_session = '12345678-0000-4000-8000-000000000001'
        other_session = '12345678-0000-4000-8000-000000000002'
        with closing(sqlite3.connect(root / 'presence.db')) as database, database:
            database.executemany(
                'INSERT INTO Presence (SessionID,UserID,RegionID,SecureSessionID) VALUES (?,?,?,?)',
                [(session, '99999999-9999-4999-8999-999999999999', region_id,
                  '00000000-0000-0000-0000-000000000000')
                 for session, region_id in [
                     (stale_session, '33333333-3333-4333-8333-333333333333'),
                     (other_session, '99999999-9999-4999-8999-999999999999'),
                 ]],
            )
        login([], env, 'Default Regions directory discovery with SDK scope ID prefix',
              coordinates=(1002, 1003), check_neighbour=True,
              presence_cleanup_sessions=(stale_session, other_session))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_probe:
            port_probe.bind(('127.0.0.1', 0))
            second_port = port_probe.getsockname()[1]
        with region_file.open('a', encoding='utf-8') as regions_file:
            regions_file.write(f'''[Second Startup Region]
RegionUUID=44444444-4444-4444-8444-444444444444
Location=1010,1011
InternalAddress=127.0.0.1
InternalPort={second_port}
SizeX=512
SizeY=512
TerrainDatabase=terrain.db
EstateDatabase=second-estate.db
''')
        with closing(sqlite3.connect(root / 'second-estate.db')) as database, database:
            database.executescript(
                'CREATE TABLE estate_settings (EstateID INTEGER PRIMARY KEY, EstateOwner TEXT, '
                'BillableFactor REAL, ParentEstateID INTEGER, PricePerMeter INTEGER, RedirectGridX INTEGER, '
                'RedirectGridY INTEGER, EstateName TEXT, PublicAccess INTEGER);'
                'CREATE TABLE estate_map (RegionID TEXT PRIMARY KEY, EstateID INTEGER);'
                "INSERT INTO estate_settings VALUES (100,'00000000-0000-0000-0000-000000000000',"
                "1,1,1,0,0,'Independent Estate',1);"
                "INSERT INTO estate_map VALUES ('44444444-4444-4444-8444-444444444444',100);"
            )
        with config.open('a', encoding='utf-8') as main_file:
            main_file.write('\n[DataSnapshot]\nindex_sims=true\n'
                            '\n[Wind]\nenabled=true\nwind_plugin=ConfigurableWind\n')
        with closing(sqlite3.connect(root / 'terrain.db')) as database, database:
            database.execute(
                'CREATE TABLE land (UUID TEXT PRIMARY KEY, RegionUUID TEXT, LocalLandID INTEGER, OwnerUUID TEXT, '
                'Bitmap BLOB, LandFlags TEXT, Name TEXT, Desc TEXT, GroupUUID TEXT, ClaimDate INTEGER, '
                'LandingType INTEGER, UserLocationX REAL, UserLocationY REAL, UserLocationZ REAL, '
                'UserLookAtX REAL, UserLookAtY REAL, UserLookAtZ REAL, SeeAVs INTEGER, Dwell INTEGER, '
                'IsGroupOwned TEXT)'
            )
            group = '55555555-5555-4555-8555-555555555555'
            database.execute(
                'INSERT INTO land VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
                ('66666666-6666-4666-8666-666666666666', '44444444-4444-4444-8444-444444444444', 1,
                 group, b'\xff' * 2048, '66', 'Group parcel fixture', '', group, 0, 0,
                 0, 0, 0, 0, 0, 0, 1, 0, 'True'),
            )
        login([], env, 'Two regions in one native host', coordinates=(1010, 1011),
              start='uri:Second Startup Region&128&128&25', expected_regions=2, check_neighbour=True)
        with closing(sqlite3.connect(root / 'terrain.db')) as database:
            check(database.execute('SELECT DISTINCT RegionUUID FROM terrain').fetchall()
                  == [('44444444-4444-4444-8444-444444444444',)],
                  'Only the region with a TerrainDatabase override persists terrain there')
        region_definitions = region_file.read_text(encoding='utf-8')
        region_file.write_text(
            region_definitions + 'Physics=ubODE\nPhysicsLibrary=missing-startup-fixture-ubode\n', encoding='utf-8',
        )
        rejects([], env, 'missing-startup-fixture-ubode',
                'Second discovered region uses its own physics library override')
        region_file.write_text(region_definitions + 'Physics=ubODE\n', encoding='utf-8')
        login([], env, 'Two regions with mixed physics', coordinates=(1010, 1011),
              start='uri:Second Startup Region&128&128&60', expected_regions=2, verify_physics=True)
        region_file.write_text(region_definitions, encoding='utf-8')
        enabled_settings = config.read_text(encoding='utf-8')
        config.write_text(enabled_settings + '\n[Messaging]\nInstantMessageModule=Disabled\n', encoding='utf-8')
        login([], env, 'Disabled viewer instant messaging', coordinates=(1010, 1011),
              start='uri:Second Startup Region&128&128&25', expected_regions=2, instant_messages_enabled=False)
        config.write_text(enabled_settings, encoding='utf-8')
        pending = queue.Queue()
        complete = threading.Event()

        class OfflineStore(BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers['Content-Length'])
                fields = parse_qs(self.rfile.read(length).decode())
                if self.path != '/offlineim' or fields.get('METHOD') != ['STORE']:
                    self.send_error(400)
                    return
                pending.put(fields)
                if not complete.wait(12):
                    self.send_error(504)
                    return
                result = b'<ServerResponse><RESULT>True</RESULT></ServerResponse>'
                self.send_response(200)
                self.send_header('Content-Type', 'text/xml')
                self.send_header('Content-Length', str(len(result)))
                self.end_headers()
                self.wfile.write(result)

        with ThreadingHTTPServer(('127.0.0.1', 0), OfflineStore) as offline:
            thread = threading.Thread(target=offline.serve_forever, daemon=True)
            thread.start()
            try:
                with config.open('a', encoding='utf-8') as main_file:
                    main_file.write('\n[Messaging]\nOfflineMessageModule=Offline Message Module V2\n'
                                    f'OfflineMessageURL=http://127.0.0.1:{offline.server_port}\n')
                config.write_text(config.read_text(encoding='utf-8').replace(
                    'NeighbourServiceInConnector=true\n',
                    'NeighbourServiceInConnector=true\nLandServiceInConnector=true\n', 1), encoding='utf-8')
                login([], env, 'Offline storage during local teleport', coordinates=(1010, 1011),
                      start='uri:Second Startup Region&128&128&25', expected_regions=2,
                      offline_store=(pending, complete), inbound_land_enabled=True)
            finally:
                complete.set()
                offline.shutdown()
                thread.join()
        config.write_text(sample.read_text(encoding='utf-8')
                          .replace('assets/AssetSets.xml', './assets/AssetSets.xml')
                          .replace('http_listener_port = 9000', 'http_listener_port = 0')
                          .replace('UDPPort = 9000', 'UDPPort = 0'), encoding='utf-8')
        login([], env, 'Bundled region sample with ubODE and local assets',
              coordinates=(1000, 4321), check_asset_endpoint=False, check_texture_decoder=True)
    report = dict(passed=True, checks=checks, productionRuntime='native C++',
                  scope='Real CLI/database initialization/user creation/HTTP login through local include, nested constant-root include, cycle suppression, process environment and pre-validation argv precedence')
    (args.work / 'native-startup-network-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS:', len(checks), 'actual native startup/network checks')

if __name__ == '__main__':
    main()
