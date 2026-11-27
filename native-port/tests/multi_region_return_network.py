"""Actual native region runtimes and viewer UDP/capability round trips."""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import time
import uuid
import urllib.error
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, post


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--arrival-dwell', type=float, default=0,
                        help='Diagnostic dwell after root arrival; the default rapid-return check remains zero')
    parser.add_argument('--hops', type=int, default=8)
    parser.add_argument('--separate-processes', action='store_true')
    parser.add_argument('--movement-first', action='store_true', default=True,
                        help='Send CompleteAgentMovement before awaiting RegionHandshake, as original login requires')
    args = parser.parse_args()
    if args.hops < 1 or args.arrival_dwell < 0:
        parser.error('hops must be positive and arrival dwell must be nonnegative')
    args.native = args.native.resolve()
    os.environ['OPENSIM_TRACE_CIRCUITS'] = '1'
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / 'results.json').unlink(missing_ok=True)
    root = args.work / ('case-' + uuid.uuid4().hex)
    root.mkdir()
    regions = root / 'Regions'
    regions.mkdir()
    reservations = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for unused in range(2)]
    for reservation in reservations:
        reservation.bind(('127.0.0.1', 0))
    ports = [reservation.getsockname()[1] for reservation in reservations]
    identities = ['11111111-1111-4111-8111-111111111111', '22222222-2222-4222-8222-222222222222']
    names = ['Return Source', 'Return Destination']
    for n in range(2):
        directory = regions / str(n) if args.separate_processes else regions
        if args.separate_processes:
            directory.mkdir()
        (directory / f'{n}.ini').write_text(f'''[{names[n]}]
RegionUUID={identities[n]}
Location={1000+n},1000
InternalAddress=127.0.0.1
InternalPort={ports[n]}
ExternalHostName=127.0.0.1
TerrainDatabase=region-{n}.db
''')
    configs = []
    for node in range(2 if args.separate_processes else 1):
        config = root / f'server-{node}.ini'
        directory = regions / str(node) if args.separate_processes else regions
        network_settings = (f'GridRegionProvider=SQLite\nGridRegionDatabase={root / "grid.db"}\n'
                            'EnableSimulationEndpoint=true\n') if args.separate_processes else ''
        config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase={root / 'users.db'}
AuthenticationDatabase={root / 'users.db'}
InventoryDatabase={root / 'inventory.db'}
GridUserDatabase={root / 'gridusers.db'}
PresenceDatabase={root / 'presence.db'}
{network_settings}
[Startup]
regionload_regionsdir={directory}
[Network]
http_listener_port=0
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
''')
        configs.append(config)
    commands = [[str(args.native), '--config', str(config)] for config in configs]
    # These are shared grid services: initialize them once, before either simulator starts.
    result = subprocess.run(commands[0] + ['--init-db'], cwd=root, text=True,
                            capture_output=True, timeout=40)
    check(result.returncode == 0, result.stdout + result.stderr)
    result = subprocess.run(commands[0] + ['--create-user', 'Preview', 'Traveler'], cwd=root,
                            input='preview-password\n', text=True, capture_output=True, timeout=40)
    check(result.returncode == 0, result.stdout + result.stderr)
    for reservation in reservations:
        reservation.close()
    servers = [PreviewServer(command, root, root / ('server.log' if len(commands) == 1 else f'server-{n}.log'))
               for n, command in enumerate(commands)]
    viewers = {}
    event_ids = {}
    checks = []
    completed = False
    try:
        uris = [server.start() for server in servers]
        uri = uris[0]
        login = xmlrpc.client.ServerProxy(uri).login_to_simulator(dict(
            first='Preview', last='Traveler', passwd='preview-password',
            start='uri:Return Source&128&128&30', channel='Native multi-region return', version='1.0'))
        check(login.get('login') == 'true', str(login))
        check(login['sim_port'] == ports[0], 'Fresh login did not select the requested source region')
        current = Viewer(login)
        viewers[ports[0]] = current
        current.connect(movement_first=args.movement_first)
        seed = login['seed_capability']
        for hop in range(args.hops):
            destination = (hop + 1) % 2
            caps = post(seed, ['EventQueueGet'])
            event_url = caps['EventQueueGet']
            handle = (((1000 + destination) * 256) << 32) | (1000 * 256)
            busy_retries = 0
            for attempt in range(5):
                current.received = [packet for packet in current.received
                                    if not packet.startswith((bytes.fromhex('ffff0049'), bytes.fromhex('ffff004a')))]
                current.send(bytes.fromhex('ffff003f') + current.credentials()
                             + struct.pack('<Q6f', handle, 128, 128, 30, 1, 0, 0))
                try:
                    current.wait(bytes.fromhex('ffff0049'), timeout=2)
                    break
                except AssertionError as error:
                    failures = [packet[21:21 + packet[20]].decode('utf-8', errors='replace').rstrip('\0')
                                for packet in current.received if packet.startswith(bytes.fromhex('ffff004a'))]
                    if failures and all(reason == 'Previous teleport process incomplete.  Please retry shortly.'
                                        for reason in failures) and attempt < 4:
                        busy_retries += 1
                        continue
                    raise AssertionError(f'Hop {hop}: no TeleportStart; failures={failures}; '
                                         f'busyRetries={busy_retries}') from error
            finish = None
            event_id = event_ids.get(event_url, 0)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and finish is None:
                try:
                    batch = post(event_url, dict(ack=event_id, done=0))
                except urllib.error.HTTPError as error:
                    if error.code != 502:
                        raise
                    time.sleep(.05)
                    continue
                event_id = batch['id']
                event_ids[event_url] = event_id
                for event in batch['events']:
                    if event['message'] == 'TeleportFinish':
                        info = event['body']['Info'][0]
                        if base64.b64decode(info['RegionHandle']) == struct.pack('>Q', handle):
                            finish = info
            check(finish is not None, f'Hop {hop}: native source did not produce destination handoff')
            check(finish['SimPort'] == ports[destination], f'Hop {hop}: handoff selected wrong native UDP endpoint')
            seed = finish['SeedCapability']
            retained = ports[destination] in viewers
            if not retained:
                destination_login = dict(login, sim_ip='127.0.0.1', sim_port=ports[destination],
                                         seed_capability=seed)
                current = Viewer(destination_login)
                viewers[ports[destination]] = current
                current.connect(movement_first=args.movement_first)
            else:
                current = viewers[ports[destination]]
                current.received.clear()
                current.send(bytes.fromhex('ffff00f9') + current.credentials()
                             + struct.pack('<I', int(login['circuit_code'])))
                current.wait(bytes.fromhex('ffff00fa'),
                             predicate=lambda data: struct.unpack_from('<Q', data, 60)[0] == handle)
            current.login['seed_capability'] = seed
            post(seed, ['EventQueueGet'])
            deadline = time.monotonic() + 5
            reported_region = None
            while time.monotonic() < deadline:
                with sqlite3.connect(f'file:{root / "presence.db"}?mode=ro', uri=True) as database:
                    row = database.execute('SELECT RegionID FROM Presence WHERE SessionID=?',
                                           (login['session_id'],)).fetchone()
                reported_region = row[0] if row else None
                if reported_region == identities[destination]:
                    break
                time.sleep(.01)
            check(reported_region == identities[destination],
                  f'Hop {hop}: movement reply was not followed by destination root presence; actual={reported_region}')
            checks.append(dict(hop=hop, destination=names[destination], retainedSocket=retained,
                               udpPort=ports[destination], regionHandle=str(handle), busyRetries=busy_retries))
            (root / 'progress.json').write_text(json.dumps(checks, indent=2) + '\n')
            if args.arrival_dwell > 0:
                time.sleep(args.arrival_dwell)
        report = dict(passed=True, checks=checks, realNativeRegions=2,
                      arrivalDwellSeconds=args.arrival_dwell, realFirestorm=False,
                      originalDotnetPeer=False, caseDirectory=str(root),
                      nativeProcesses=len(servers), serverUris=uris,
                      movementBeforeHandshakeWait=args.movement_first,
                      serverProcessIds=[server.process.pid for server in servers],
                      serverLogs=[str(server.log_path) for server in servers],
                      simulatorTransport='HTTP' if args.separate_processes else 'local runtime calls')
        completed = True
    finally:
        for viewer in viewers.values():
            viewer.socket.close()
        errors = []
        root_node = args.hops % 2
        stop_order = [1 - root_node, root_node] if args.separate_processes and completed else list(reversed(range(len(servers))))
        for node in stop_order:
            try:
                servers[node].stop()
                if args.separate_processes and completed and node != root_node:
                    with sqlite3.connect(f'file:{root / "presence.db"}?mode=ro', uri=True) as database:
                        row = database.execute('SELECT RegionID FROM Presence WHERE SessionID=?',
                                               (login['session_id'],)).fetchone()
                    check(row and row[0] == identities[root_node],
                          'Stopping a child simulator removed the session owned by the root simulator')
            except Exception as error:
                errors.append(error)
        if errors:
            raise errors[0]
    report['cleanShutdown'] = True
    with sqlite3.connect(f'file:{root / "presence.db"}?mode=ro', uri=True) as database:
        row = database.execute('SELECT RegionID FROM Presence WHERE SessionID=?', (login['session_id'],)).fetchone()
    check(row is None, 'Clean root simulator shutdown retained the session presence')
    report['rootSessionRemovedOnShutdown'] = True
    (args.work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(checks)} actual native multi-region teleport/retained-return hops')


if __name__ == '__main__':
    main()
