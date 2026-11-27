"""Actual INI, grid/simulator HTTP negotiation and viewer UDP transfer entry."""
import argparse
import base64
import gzip
import http.server
import json
from pathlib import Path
import struct
import sqlite3
import contextlib
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, post


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    args.native = args.native.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    checks = []
    for version, setting, closes, nearby in [(.1, 'false', True, False), (.1, 'true', False, False),
            (.1, None, False, False), (.2, 'true', True, False), (.8, None, True, False),
            (.2, 'true', False, True), (.8, None, False, True)]:
        name = f'v{version}-wait-{setting}-near-{nearby}'
        updated = threading.Event()
        release_update = threading.Event()
        update_bodies = []
        creation_bodies = []
        errors = []
        negotiated = threading.Event()
        requests = []
        destination = '22222222-2222-4222-8222-222222222222'
        destination_x, destination_y = 1001 * 256, 1000 * 256

        class Remote(http.server.BaseHTTPRequestHandler):
            def log_message(self, *unused):
                pass

            def reply(self, body):
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_QUERYACCESS(self):
                requests.append((self.command, self.path))
                self.rfile.read(int(self.headers.get('Content-Length', '0')))
                negotiated.set()
                self.reply(json.dumps(dict(success=True, negotiated_inbound_version=version,
                                           negotiated_outbound_version=version)).encode())

            def do_PUT(self):
                requests.append((self.command, self.path))
                body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                if body.startswith(b'\x1f\x8b'):
                    body = gzip.decompress(body)
                update_bodies.append(json.loads(body))
                updated.set()
                if not release_update.wait(6):
                    errors.append('Destination update reply was not released by the viewer handshake')
                self.reply(b'{"success":true}')

            def do_DELETE(self):
                self.reply(b'{}')

            def do_POST(self):
                requests.append((self.command, self.path))
                body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                if self.path.startswith(('/agent/', '/region/')):
                    if self.path.startswith('/agent/'):
                        if body.startswith(b'\x1f\x8b'):
                            body = gzip.decompress(body)
                        creation_bodies.append(json.loads(body))
                    self.reply(b'{"success":true}')
                    return
                fields = urllib.parse.parse_qs(body.decode())
                method = fields.get('METHOD', [''])[0]
                if method in ('register', 'deregister'):
                    response = '<ServerResponse><Result>Success</Result></ServerResponse>'
                elif method == 'get_region_by_position' and fields.get('X') == [str(destination_x)]:
                    response = (f'<ServerResponse><result type="List"><uuid>{destination}</uuid>'
                                f'<locX>{destination_x}</locX><locY>{destination_y}</locY><sizeX>256</sizeX><sizeY>256</sizeY>'
                                '<regionName>Callback Destination</regionName><serverIP>127.0.0.1</serverIP>'
                                f'<serverURI>http://127.0.0.1:{self.server.server_port}/</serverURI>'
                                f'<serverHttpPort>{self.server.server_port}</serverHttpPort>'
                                '<serverPort>19000</serverPort></result></ServerResponse>')
                else:
                    response = '<ServerResponse><result>null</result></ServerResponse>'
                self.reply(response.encode())

        with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Remote) as remote:
            thread = threading.Thread(target=remote.serve_forever, daemon=True)
            thread.start()
            try:
                with tempfile.TemporaryDirectory(dir=args.work, prefix='callback-') as temporary:
                    root = Path(temporary)
                    config = root / 'region.ini'
                    config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
PresenceDatabase=presence.db
EnableFriendsService=true
EnableFriendsEndpoint=true
FriendsDatabase=friends.db
[Network]
http_listener_port=0
[NativeRegion]
ID=11111111-1111-4111-8111-111111111111
Name=Callback Source
LocationX={1000 if nearby else 500}
LocationY={1000 if nearby else 500}
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
GridServerURI=http://127.0.0.1:{remote.server_port}
[LoginService]
RequireInventory=true
''' + (f'[EntityTransfer]\nwait_for_callback={setting}\n' if setting is not None else ''), encoding='utf-8')
                    command = [str(args.native), '--config', str(config)]
                    for options, password in [(['--init-db'], None),
                                              (['--create-user', 'Preview', 'First'], 'preview-password\n')]:
                        setup = subprocess.run(command + options, cwd=root, input=password, text=True,
                                               capture_output=True, timeout=40)
                        check(setup.returncode == 0, setup.stdout + setup.stderr)
                    server = PreviewServer(command, root, args.work / f'{name}.log')
                    viewer = None
                    try:
                        server_uri = server.start()
                        login = xmlrpc.client.ServerProxy(server_uri).login_to_simulator(dict(
                            first='Preview', last='First', passwd='preview-password', start='home',
                            channel='Callback network test', version='1.0'))
                        check(login.get('login') == 'true', str(login))
                        viewer = Viewer(login)
                        viewer.connect()
                        retained_friend = '33333333-3333-4333-8333-333333333333'
                        removed_friend = '44444444-4444-4444-8444-444444444444'
                        for friend, online in [(retained_friend, True), (retained_friend, True),
                                               (removed_friend, True), (removed_friend, False)]:
                            body = urllib.parse.urlencode(dict(METHOD='status', FromID=friend,
                                                               ToID=login['agent_id'], Online=str(online))).encode()
                            with urllib.request.urlopen(urllib.request.Request(
                                    server_uri.rstrip('/') + '/friends', data=body), timeout=3) as response:
                                check(b'<RESULT>True</RESULT>' in response.read(),
                                      f'{name}: friend status did not reach the root viewer')
                        # Distinct head and body rotations exercise the actual
                        # viewer input -> production AgentData sender path.
                        viewer.send(b'\x04' + viewer.credentials()
                                    + struct.pack('<6fB13fIB', 0, 0, 0, 0, .6, 0, 0,
                                                  128, 128, 30, 1, 0, 0, 0, 1, 0, 0, 0, 1, 255, 0, 0))
                        caps = post(login['seed_capability'], ['EventQueueGet'])
                        event_url = caps.get('EventQueueGet')
                        check(event_url, f'{name}: EventQueueGet was not negotiated')
                        sent_at = time.monotonic()
                        viewer.send(bytes.fromhex('ffff003f') + viewer.credentials()
                                    + struct.pack('<Q6f', (destination_x << 32) | destination_y, 128, 128, 30, 1, 0, 0))
                        try:
                            if event_url:
                                event_id = 0
                                finish = None
                                notifications = []
                                for unused in range(8):
                                    batch = post(event_url, dict(ack=event_id, done=0))
                                    event_id = batch['id']
                                    for event in batch['events']:
                                        message = event['message']
                                        if message in ('EnableSimulator', 'EstablishAgentCommunication'):
                                            notifications.append(message)
                                            if version < .2 and message == 'EstablishAgentCommunication':
                                                check(notifications == ['EnableSimulator', 'EstablishAgentCommunication'],
                                                      f'{name}: incorrect V1 notification order')
                                                release_update.set()
                                    finish = next((event for event in batch['events']
                                                   if event['message'] == 'TeleportFinish'), None)
                                    if finish:
                                        break
                                check(finish, f'{name}: no EventQueue TeleportFinish')
                                if version < .2:
                                    check(notifications == ['EnableSimulator', 'EstablishAgentCommunication']
                                          and release_update.is_set(), f'{name}: V1 finish preceded its notifications')
                                info = finish['body']['Info'][0]
                                check(info['AgentID'] == login['agent_id'] and info['LocationID'] == base64.b64encode(struct.pack('>I', 4)).decode(),
                                      f'{name}: incorrect event identity: {info!r}')
                                check(info['SimIP'] == base64.b64encode(bytes([127, 0, 0, 1])).decode()
                                      and info['SimPort'] == 19000, f'{name}: incorrect event endpoint')
                                check(info['RegionHandle'] == base64.b64encode(struct.pack('>Q', (destination_x << 32) | destination_y)).decode(),
                                      f'{name}: incorrect event handle')
                                check(info['SeedCapability'].startswith(f'http://127.0.0.1:{remote.server_port}/CAPS/')
                                      and not info['SeedCapability'].endswith('\0'), f'{name}: incorrect event seed')
                                check(info['SimAccess'] == 13 and info['TeleportFlags'] == base64.b64encode(struct.pack('>I', 16)).decode()
                                      and info['RegionSizeX'] == base64.b64encode(struct.pack('>I', 256)).decode()
                                      and info['RegionSizeY'] == base64.b64encode(struct.pack('>I', 256)).decode(),
                                      f'{name}: incorrect event access, flags or dimensions')
                            else:
                                viewer.wait(bytes.fromhex('ffff0045'))  # V1 UDP TeleportFinish
                        except AssertionError as error:
                            failures = [packet[21:21 + packet[20]].decode('utf-8', errors='replace')
                                        for packet in viewer.received if packet.startswith(bytes.fromhex('ffff004a'))]
                            raise AssertionError(f'{name}: {failures}; negotiated={negotiated.is_set()} updated={updated.is_set()}; requests={requests}') from error
                        if version >= .2:
                            check(updated.wait(3), f'{name}: V2 UpdateAgent did not start')
                            check(update_bodies[0].get('wait_for_root') is True,
                                  f'{name}: V2 omitted SenderWantsToWaitForRoot')
                            check(not release_update.is_set() and not errors,
                                  f'{name}: TeleportFinish was not delivered during blocked UpdateAgent')
                            release_update.set()
                        check(negotiated.is_set() and updated.is_set(), f'{name}: remote protocol was not exercised')
                        check(creation_bodies and all(body.get('child') is True for body in creation_bodies),
                              f'{name}: sender did not reserve original-style child circuits')
                        check(update_bodies[0].get('cfonline') == [retained_friend],
                              f'{name}: production transfer omitted or corrupted the friend-online cache')
                        check('active_group_title' not in update_bodies[0]
                              and update_bodies[0].get('active_group_name', '') == ''
                              and update_bodies[0].get('active_group_id', '00000000-0000-0000-0000-000000000000')
                                  == '00000000-0000-0000-0000-000000000000',
                              f'{name}: ordinary teleport group metadata: '
                              f'{[(key, value) for key, value in update_bodies[0].items() if "group" in key.lower()]}')
                        head = [float(part) for part in update_bodies[0]['head_rotation'].strip('<>').split(',')]
                        body = [float(part) for part in update_bodies[0]['body_rotation'].strip('<>').split(',')]
                        check(len(head) == 4 and abs(head[1] - .6) < .0001 and abs(head[3] - .8) < .0001
                              and abs(body[1]) < .0001 and abs(body[3] - 1) < .0001,
                              f'{name}: production transfer lost independent head rotation: {head}, {body}')
                        if version >= .2 and nearby:
                            check(not update_bodies[0].get('cb_uri') and not update_bodies[0].get('callback_uri'),
                                  f'{name}: nearby V2 transfer requested a source release callback')
                        if version >= .2 and not nearby:
                            callback = update_bodies[0].get('cb_uri')
                            check(callback, f'{name}: missing source release callback')
                            with urllib.request.urlopen(urllib.request.Request(callback, method='DELETE'), timeout=3) as reply:
                                check(reply.status == 200, f'{name}: source callback rejected')
                        deadline = time.monotonic() + (5 if closes else .25)
                        closed = False
                        while True:
                            try:
                                post(login['seed_capability'], [])
                            except urllib.error.HTTPError as error:
                                check(error.code == 404, str(error))
                                closed = True
                                break
                            if time.monotonic() >= deadline:
                                break
                            time.sleep(.025)
                        check(closed == closes, f'{name}: source capability lifetime did not match INI policy')
                        if closes:
                            if version < .2:
                                check(time.monotonic() - sent_at >= 2, 'Source closed before the original two-second delay')
                            with contextlib.closing(sqlite3.connect(f'file:{root / "presence.db"}?mode=ro', uri=True)) as db:
                                check(db.execute('SELECT COUNT(*) FROM Presence WHERE SessionID=?',
                                                 (login['session_id'],)).fetchone()[0] == 1,
                                      'Source departure deleted the travelling presence')
                            with contextlib.closing(sqlite3.connect(f'file:{root / "gridusers.db"}?mode=ro', uri=True)) as db:
                                online = db.execute('SELECT Online FROM GridUser WHERE UserID=?',
                                                    (login['agent_id'],)).fetchone()
                                check(online and online[0].lower() == 'true', 'Source departure logged the account out')
                        check(not errors, str(errors))
                        checks.append(dict(version=version, setting=setting, nearby=nearby, sourceClosed=closed,
                                           finishBeforeUpdateReply=version >= .2, eventQueue=bool(event_url), v1Notifications=version < .2, passed=True))
                    finally:
                        release_update.set()
                        if viewer:
                            viewer.socket.close()
                        server.stop()
            finally:
                remote.shutdown()
                thread.join(timeout=3)
    (args.work / 'results.json').write_text(json.dumps(dict(passed=True, cases=checks,
        realRemoteSimulator=False, realFirestorm=False), indent=2) + '\n')
    print(f'PASS: {len(checks)} executable callback configuration/HTTP/UDP cases')


if __name__ == '__main__':
    main()
