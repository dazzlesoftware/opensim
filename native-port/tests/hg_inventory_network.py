"""Foreign-agent admission, home inventory routing and known-offline profile lookup."""
import argparse
import http.client
import json
from pathlib import Path
import socket
from socketserver import ThreadingMixIn
import sqlite3
import struct
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client
from xmlrpc.server import SimpleXMLRPCRequestHandler, SimpleXMLRPCServer

from preview_entry_network import PreviewServer, Viewer, check, post


class HomeServer(ThreadingMixIn, SimpleXMLRPCServer):
    daemon_threads = True


class HomeHandler(SimpleXMLRPCRequestHandler):
    rpc_paths = ('/',)

    def log_message(self, *args):
        pass

    def do_POST(self):
        if self.headers.get('Content-Type', '').startswith('application/json'):
            request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            self.server.profile_calls.append(request)
            value = dict(request.get('params', {}))
            value['AboutText'] = 'Offline foreign profile from home service'
            body = json.dumps(dict(jsonrpc='2.0', id=request['id'], result=value)).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json-rpc')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path != '/xinventory':
            return super().do_POST()
        form = urllib.parse.parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
        method = form['METHOD'][0]
        self.server.calls.append((method, form))
        reply = ET.Element('ServerResponse')
        if method == 'ADDFOLDER':
            self.server.folders[form['ID'][0]] = {
                key: form[key][0] for key in ['ID', 'Owner', 'ParentID', 'Type', 'Version', 'Name']}
            ET.SubElement(reply, 'RESULT').text = 'True'
        elif method == 'PURGEFOLDER':
            ET.SubElement(reply, 'RESULT').text = 'True'
            self.server.purge_received.set()
        elif method == 'GETFOLDER':
            folder = self.server.folders.get(form['ID'][0])
            if folder:
                node = ET.SubElement(reply, 'folder', type='List')
                for key, value in folder.items():
                    ET.SubElement(node, key).text = value
            else:
                ET.SubElement(reply, 'RESULT').text = 'False'
        elif method == 'GETFOLDERCONTENT':
            ET.SubElement(reply, 'FOLDERS', type='List')
            ET.SubElement(reply, 'ITEMS', type='List')
        else:
            ET.SubElement(reply, 'RESULT').text = 'False'
        body = ET.tostring(reply)
        self.send_response(200)
        self.send_header('Content-Type', 'application/xml')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def request(uri, method, path, value):
    target = urllib.parse.urlsplit(uri)
    connection = http.client.HTTPConnection(target.hostname, target.port, timeout=10)
    try:
        connection.request(method, path, json.dumps(value), {'Content-Type': 'application/json'})
        response = connection.getresponse()
        body = response.read()
        check(response.status == 200, (response.status, body))
        return body
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--offline', action='store_true', help='Read known visitor inventory after its circuit closes')
    parser.add_argument('--foreign-profiles', action='store_true',
                        help='Read a logged-out visitor profile with local inventory and no HG broker')
    parser.add_argument('--shared-profiles', action='store_true',
                        help='Read offline inventory and profile through one shared home directory')
    args = parser.parse_args()
    args.offline = args.offline or args.foreign_profiles or args.shared_profiles
    profiles_enabled = args.foreign_profiles or args.shared_profiles
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'results.json').unlink(missing_ok=True)
    root = work / ('case-' + uuid.uuid4().hex)
    root.mkdir()
    agent, session, inventory_root = uuid.uuid4(), uuid.uuid4(), uuid.uuid4()
    region = '11111111-1111-4111-8111-111111111111'
    with HomeServer(('127.0.0.1', 0), requestHandler=HomeHandler, logRequests=False) as home:
        home.calls = []
        home.directory_calls = []
        home.profile_calls = []
        home.purge_received = threading.Event()
        home.folders = {str(inventory_root): dict(ID=str(inventory_root), Owner=str(agent),
            ParentID=str(uuid.UUID(int=0)), Type='8', Version='1', Name='Visitor home root')}
        for method in ['verify_agent', 'verify_client', 'logout_agent']:
            home.register_function(lambda fields: {'result': 'True'}, method)
        thread = threading.Thread(target=home.serve_forever, daemon=True)
        thread.start()
        home_uri = f'http://127.0.0.1:{home.server_address[1]}/'
        def directory(fields):
            home.directory_calls.append(fields)
            return {'SRV_InventoryServerURI': home_uri, 'SRV_ProfileServerURI': home_uri, 'SRV_HomeURI': home_uri}
        home.register_function(directory, 'get_server_urls')
        home.register_function(lambda fields: dict(user_flags=0, user_created=1234567890), 'get_user_info')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            udp = reservation.getsockname()[1]
        config = root / 'server.ini'
        config.write_text(f'''[NativeServer]
EnableLocalRegion=true
EnableSimulationEndpoint=true
EnableGatekeeperDiscovery=true
EnableForeignAgentAdmission=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
[Startup]
GatekeeperURI=http://127.0.0.1:8002/
[Network]
http_listener_port=0
[NativeRegion]
UDPPort={udp}
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[Modules]
InventoryServices={'LocalInventoryServicesConnector' if args.foreign_profiles else 'HGInventoryBroker'}
[InventoryService]
LocalGridInventoryService=OpenSim.Services.InventoryService.dll:XInventoryService
[ClientStack.LindenCaps]
Cap_FetchInventoryDescendents2=localhost
''')
        if profiles_enabled:
            config.write_text(config.read_text() + f'\n[UserProfiles]\nProfileServiceURL={home_uri}\n')
        command = [str(args.native.resolve()), '--config', str(config)]
        initialized = subprocess.run(command + ['--init-db'], cwd=root, capture_output=True, text=True, timeout=40)
        check(initialized.returncode == 0, initialized.stdout + initialized.stderr)
        if args.offline:
            created = subprocess.run(command + ['--create-user', 'Preview', 'Resident'], cwd=root,
                input='preview-password\n', capture_output=True, text=True, timeout=40)
            check(created.returncode == 0, created.stdout + created.stderr)
        server = PreviewServer(command, root, root / 'server.log')
        viewer = resident = None
        try:
            uri = server.start()
            payload = dict(agent_id=str(agent), session_id=str(session), secure_session_id=str(uuid.uuid4()),
                circuit_code='91474', viewer='Fixture Viewer', first_name='Inventory', last_name='Visitor',
                caps_path='hg-inventory', start_pos='<128,128,70>', destination_uuid=region,
                service_session_id='http://127.0.0.1:8002/;fixture', client_ip='127.0.0.1',
                serviceurls=dict(HomeURI=home_uri, InventoryServerURI=home_uri))
            admitted = json.loads(request(uri, 'POST', f'/foreignagent/{agent}/', payload))
            check(admitted.get('success') is True, admitted)
            viewer = Viewer(dict(agent_id=str(agent), session_id=str(session), circuit_code='91474',
                                 sim_ip='127.0.0.1', sim_port=udp))
            # Full AgentData waits for the scene presence admitted by the
            # viewer's UseCircuitCode, as in the original transfer sequence.
            viewer.send(bytes.fromhex('ffff0003') + struct.pack('<I', 91474) + session.bytes + agent.bytes)
            viewer.wait(bytes.fromhex('fffffffb'))
            staged = request(uri, 'PUT', f'/agent/{agent}/', dict(message_type='AgentData',
                agent_uuid=str(agent), session_uuid=str(session), destination_uuid=region,
                region_id=str(uuid.uuid4()), position='<128,128,70>', at_axis='<1,0,0>'))
            check(staged == b'True', staged)
            viewer.connect()
            if not args.foreign_profiles:
                caps = post(uri.rstrip('/') + '/CAPS/hg-inventory0000/', ['CreateInventoryCategory'])
                category = uuid.uuid4()
                saved = post(caps['CreateInventoryCategory'], dict(folder_id=category, parent_id=inventory_root,
                                                                  name="Visitor's home & folder", type=-1))
                check(saved['folder_id'] == str(category), saved)
                check(home.folders[str(category)]['Owner'] == str(agent), 'Home category owner changed')
                check([method for method, form in home.calls][-3:] == ['GETFOLDER', 'ADDFOLDER', 'GETFOLDER'], home.calls)
                viewer.send(bytes.fromhex('ffff011d') + viewer.credentials() + category.bytes)
                check(home.purge_received.wait(5), 'Visitor UDP purge did not reach home inventory')
                purge = next(form for method, form in home.calls if method == 'PURGEFOLDER')
                check(purge['ID'] == [str(category)], purge)
            if args.offline:
                seed = uri.rstrip('/') + '/CAPS/hg-inventory0000/'
                viewer.send(bytes.fromhex('ffff00fc') + viewer.credentials())
                viewer.wait(bytes.fromhex('ffff00fd'))
                deadline = time.monotonic() + 10
                while True:
                    try:
                        post(seed, [])
                    except urllib.error.HTTPError as error:
                        if error.code == 404:
                            break
                        raise
                    check(time.monotonic() < deadline, 'Visitor circuit capabilities did not close')
                    time.sleep(.025)
                with xmlrpc.client.ServerProxy(uri) as proxy:
                    login = proxy.login_to_simulator(dict(first='Preview', last='Resident', passwd='preview-password',
                        start='last', channel='Fixture Viewer', version='1.0'))
                check(login.get('login') == 'true', login)
                resident = Viewer(login)
                resident.connect()
                if not args.foreign_profiles:
                    resident_caps = post(login['seed_capability'], ['FetchInventoryDescendents2'])
                    result = post(resident_caps['FetchInventoryDescendents2'], dict(folders=[dict(
                        folder_id=inventory_root, owner_id=agent, fetch_folders=True, fetch_items=True, sort_order=0)]))
                    check(result['folders'][0]['owner_id'] == str(agent), result)
                    check(any(method == 'GETFOLDERCONTENT' for method, form in home.calls), home.calls)
                    post(resident_caps['FetchInventoryDescendents2'], dict(folders=[dict(
                        folder_id=inventory_root, owner_id=agent, fetch_folders=True, fetch_items=True, sort_order=0)]))
                if profiles_enabled:
                    resident.send(bytes.fromhex('ffff00a9') + resident.credentials() + agent.bytes)
                    resident.wait(bytes.fromhex('ffff00ab'), b'Offline foreign profile from home service')
                    check(len(home.profile_calls) == 1 and
                          home.profile_calls[0]['method'] == 'avatar_properties_request', home.profile_calls)
                    check(home.profile_calls[0]['params']['UserId'] == str(agent), home.profile_calls)
                    resident.received.clear()
                    resident.send(bytes.fromhex('ffff00a9') + resident.credentials() + agent.bytes)
                    resident.wait(bytes.fromhex('ffff00ab'), b'Offline foreign profile from home service')
                    check(len(home.profile_calls) == 1, 'Repeat profile request bypassed profile cache')
                check(home.directory_calls == [{'userID': str(agent)}], home.directory_calls)
                check(len(home.directory_calls) == 1, home.directory_calls)
        finally:
            if viewer:
                viewer.socket.close()
            if resident:
                resident.socket.close()
            server.stop()
            home.shutdown()
            thread.join(timeout=5)
        with sqlite3.connect(f'file:{root / "inventory.db"}?mode=ro', uri=True) as database:
            check(database.execute('SELECT COUNT(*) FROM inventoryfolders WHERE agentID=?', (str(agent),)).fetchone()[0] == 0,
                  'Visitor category leaked into local inventory')
        report = dict(passed=True, caseDirectory=str(root), foreignAdmissionAndViewerHandshake=True,
                      circuitInventoryUrlRouted=not args.foreign_profiles, categoryOwner=str(agent), localInventoryFolders=0,
                      viewerPurgeRoutedToHome=not args.foreign_profiles,
                      offlineKnownVisitorLookup=args.offline, directoryRequests=len(home.directory_calls),
                      foreignProfileWithoutInventoryBroker=args.foreign_profiles, profileRequests=len(home.profile_calls),
                      sharedProfileInventoryDirectory=args.shared_profiles,
                      methods=[method for method, form in home.calls], cleanShutdown=True)
        (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.foreign_profiles:
            print('PASS: offline foreign profile resolves home directory without HGInventoryBroker; repeated reply cached')
        elif args.shared_profiles:
            print('PASS: offline inventory and profile reuse one home directory request; repeated reads cached')
        else:
            print('PASS: connected foreign visitor category writes use circuit home inventory; local folders untouched')


if __name__ == '__main__':
    main()
