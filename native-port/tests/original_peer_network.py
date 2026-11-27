"""Isolated original .NET standalone baseline, without modifying installed data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.request
import uuid
import xmlrpc.client

from preview_entry_network import Viewer, check, post


def copy_original_distribution(source, root):
    # Copy program files only. Databases, runtime config, region and estate data
    # from the installed instance must never become part of this fresh fixture.
    for item in source.iterdir():
        if item.is_file() and (item.suffix.lower() in {'.dll', '.pdb', '.json', '.config'}
                               or item.name in {'OpenSimDefaults.ini', 'avatar-texture.dat',
                                                'defaultstripe.png', 'System.Drawing.Common.dll.win',
                                                'System.Drawing.Common.dll.linux'}):
            shutil.copy2(item, root / item.name)
    for name in ['config-include', 'assets', 'inventory', 'Library', 'openmetaverse_data', 'lib64']:
        if (source / name).is_dir():
            shutil.copytree(source / name, root / name)
    (root / 'data').mkdir()
    shutil.copy2(source / 'data/avataranimations.xml', root / 'data/avataranimations.xml')
    (root / 'config').mkdir()
    (root / 'Regions').mkdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    source = args.bin.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / 'results.json').unlink(missing_ok=True)
    root = args.work / ('case-' + uuid.uuid4().hex)
    root.mkdir()
    copy_original_distribution(source, root)
    with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        tcp.bind(('127.0.0.1', 0))
        udp.bind(('127.0.0.1', 0))
        http_port, udp_port = tcp.getsockname()[1], udp.getsockname()[1]
    (root / 'Regions/peer.ini').write_text(f'''[Original Peer]
RegionUUID=33333333-3333-4333-8333-333333333333
Location=1002,1000
InternalAddress=127.0.0.1
InternalPort={udp_port}
ExternalHostName=127.0.0.1
''')
    (root / 'config-include/StandaloneCommon.ini').write_text('''[DatabaseService]
Include-Storage=config-include/storage/SQLiteStandalone.ini
''')
    (root / 'OpenSim.ini').write_text(f'''[Startup]
physics=POS
meshing=Meshmerizer
console=basic
regionload_regionsdir=Regions
ConsoleHistoryFileEnabled=false
[Network]
http_listener_port={http_port}
[Architecture]
Include-Architecture=config-include/Standalone.ini
[Estates]
DefaultEstateName=Isolated Original Peer
DefaultEstateOwnerName=Preview Original
DefaultEstateOwnerUUID=44444444-4444-4444-8444-444444444444
DefaultEstateOwnerPassword=preview-password
DefaultEstateOwnerEMail=preview@example.invalid
[Map]
GenerateMaptiles=false
[Modules]
SimulationServiceInConnector=true
[GridInfoService]
DisableStatsEndpoint=true
gridname=Isolated Original Peer
gridnick=original-peer
login=http://127.0.0.1:{http_port}/
''')
    uri = f'http://127.0.0.1:{http_port}/'
    viewer = None
    report = None
    with (root / 'server.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen([args.dotnet, str(root / 'OpenSim.dll'), '-console=basic'],
                                   cwd=root, stdin=subprocess.PIPE, stdout=log,
                                   stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 180
            while True:
                check(process.poll() is None, 'Original peer exited: ' + str(root / 'server.log'))
                try:
                    with urllib.request.urlopen(uri + 'get_grid_info', timeout=1) as response:
                        if response.status == 200:
                            break
                except (OSError, urllib.error.URLError):
                    pass
                check(time.monotonic() < deadline, 'Original peer did not start: ' + str(root / 'server.log'))
                time.sleep(.1)
            # HTTP starts before region admission is ready; wait for the original
            # application readiness message rather than masking login failures.
            while 'INITIALIZATION COMPLETE' not in (root / 'server.log').read_text():
                check(process.poll() is None and time.monotonic() < deadline,
                      'Original region initialization failed: ' + str(root / 'server.log'))
                time.sleep(.1)
            login = xmlrpc.client.ServerProxy(uri).login_to_simulator(dict(
                first='Preview', last='Original', passwd='preview-password',
                start='uri:Original Peer&128&128&30', channel='Original peer baseline', version='1.0'))
            check(login.get('login') == 'true', str(login))
            check(login['sim_port'] == udp_port, 'Original peer returned a different UDP port')
            viewer = Viewer(login)
            credentials = viewer.credentials()
            code = struct.pack('<I', int(login['circuit_code']))
            # Original login sends its handshake from CompleteMovement, unlike
            # an ordinary child connection (teleportFlags == 0).
            viewer.send(bytes.fromhex('ffff0003') + code
                        + uuid.UUID(login['session_id']).bytes + uuid.UUID(login['agent_id']).bytes)
            try:
                viewer.wait(bytes.fromhex('ffff0094'), timeout=.25)
            except AssertionError:
                pass
            else:
                raise AssertionError('Original login sent a handshake before CompleteAgentMovement')
            viewer.send(bytes.fromhex('ffff00f9') + credentials + code)
            viewer.wait(bytes.fromhex('ffff0094'))
            viewer.send(bytes.fromhex('ffff0095') + credentials + struct.pack('<I', 0))
            viewer.wait(bytes.fromhex('ffff00fa'))
            caps = post(login['seed_capability'], ['EventQueueGet'])
            check(bool(caps.get('EventQueueGet')), 'Original peer omitted EventQueueGet')
            viewer.received.clear()
            repeated_sequences = set()
            for unused in range(3):
                viewer.send(bytes.fromhex('ffff0003') + code
                            + uuid.UUID(login['session_id']).bytes + uuid.UUID(login['agent_id']).bytes)
                repeated_sequences.add(viewer.sequence)
            try:
                viewer.wait(bytes.fromhex('ffff0094'), timeout=1)
            except AssertionError:
                pass
            else:
                raise AssertionError('Original peer restarted handshake after repeated UseCircuitCode')
            check(repeated_sequences <= viewer.acknowledged_sequences,
                  'Original peer did not acknowledge all repeated circuit packets')
            report = dict(passed=True, caseDirectory=str(root), originalBinarySha256=hashlib.sha256(
                (root / 'OpenSim.dll').read_bytes()).hexdigest(), freshAccount=True,
                login=True, viewerHandshake=True, eventQueueCapability=True,
                handshakeOrder='UseCircuitCode, CompleteAgentMovement, RegionHandshake, RegionHandshakeReply, AgentMovementComplete',
                handshakeBeforeMovement=False,
                repeatedCircuitAcknowledgements=3, repeatedCircuitRestartsHandshake=False,
                mixedNativeTransferVerified=False, realFirestorm=False)
        finally:
            if viewer:
                viewer.socket.close()
            if process.poll() is None:
                process.stdin.write('shutdown\n')
                process.stdin.flush()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise AssertionError('Original peer did not shut down cleanly')
            if report is not None:
                check(process.returncode == 0, f'Original peer exited {process.returncode}: {root / "server.log"}')
    report['cleanShutdown'] = True
    (args.work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: original .NET fresh account/login/UDP/capability baseline')


if __name__ == '__main__':
    main()
