"""Resume a nested LSL function across clean native application processes.

Uses fresh CLI databases/accounts, authenticated viewer UDP rez/script creation,
task-inventory download and ordinary script-editor capabilities. SQLite is read
only after clean shutdown. Every run retains its own evidence directory.
"""

import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import platform
import sqlite3
import struct
import subprocess
import time
import urllib.request
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, decode, post
from seated_crossing_network import create_script, wait_script_chat


SOURCE = b'''integer calls;
integer inner(integer value) {
    ++calls;
    llSetObjectDesc("sleeping:" + (string)calls);
    llOwnerSay("function sleeping");
    llSleep(10.0);
    return value + 1;
}
integer outer(integer value) { return inner(value) + 5; }
default {
    state_entry() {
        integer result = outer(5);
        llSetObjectDesc((string)result + ":" + (string)calls);
        llOwnerSay("function completed");
    }
}'''

STACK_SOURCE = b'''integer recur(integer n) {
    if (n <= 0) return 0;
    return 1 + recur(n - 1);
}
default {
    state_entry() {
        llOwnerSay("stack boundary " + (string)recur(118));
        llOwnerSay("stack overflow missing " + (string)recur(119));
    }
}'''


def properties_description(viewer, identity):
    viewer.received.clear()
    viewer.send(bytes.fromhex('ff05') + viewer.credentials() + struct.pack('<I', 0) + identity.bytes)
    reply = viewer.wait(bytes.fromhex('ff0a'), predicate=lambda data:
                        len(data) > 104 and data[6:22] == identity.bytes)
    # ObjectPropertiesFamily single block through LastOwnerID, then two
    # variable-1 strings: Name and Description.
    offset = 104 + reply[103]
    check(offset < len(reply), 'Malformed object-properties name length')
    size = reply[offset]
    check(offset + 1 + size <= len(reply), 'Malformed object-properties description length')
    return reply[offset + 1:offset + 1 + size].rstrip(b'\0').decode('utf-8')


def saved_state(path, region, identity, item):
    with contextlib.closing(sqlite3.connect(f'file:{path.as_posix()}?mode=ro', uri=True)) as database:
        prim = database.execute('SELECT Description FROM prims WHERE UUID=?', (str(identity),)).fetchone()
        checkpoint = database.execute(
            'SELECT asset_id,checkpoint FROM native_script_states WHERE region_id=? AND part_id=? AND item_id=?',
            (str(region), str(identity), str(item))).fetchone()
    check(prim is not None and checkpoint is not None, 'Clean shutdown omitted the prim or native script checkpoint')
    payload = checkpoint[1]
    check(isinstance(payload, bytes) and payload.startswith(b'LSC'), 'Saved script checkpoint is not native binary state')
    return dict(description=prim[0], asset=str(checkpoint[0]), checkpointBytes=len(payload),
                checkpointSha256=hashlib.sha256(payload).hexdigest(),
                containsInnerFrame=b'inner' in payload, containsOuterFrame=b'outer' in payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native = args.native.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex
    root = work / ('user-function-' + run_id)
    root.mkdir()
    region = uuid.uuid4()
    control = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Native runtime must contain its bundled default assets')
    config = root / 'region.ini'
    config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
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
ID={region}
Name=User Function Restart
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
[Terrain]
InitialTerrain=flat
[YEngine]
ScriptStackSize=16
[Modules]
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
''', encoding='utf-8')
    (root / 'script.lsl').write_bytes(SOURCE)
    (root / 'stack-boundary.lsl').write_bytes(STACK_SOURCE)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, runId=run_id, platform=platform.system(), caseDirectory=str(root),
                  nativeSha256=hashlib.sha256(native.read_bytes()).hexdigest(),
                  testSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  sourceSha256=hashlib.sha256(SOURCE).hexdigest(),
                  stackProbeSourceSha256=hashlib.sha256(STACK_SOURCE).hexdigest(),
                  helperSha256={name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                for name in ('preview_entry_network.py', 'seated_crossing_network.py')},
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Function', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True,
                                   capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(2):
            server = PreviewServer(command, root, root / f'server-{cycle}.log')
            viewer = None
            observation = dict(cycle=cycle)
            try:
                url = server.start()
                observation['pid'] = server.process.pid
                login = xmlrpc.client.ServerProxy(url).login_to_simulator(dict(
                    first='Function', last='Viewer', passwd='fixture-password', start='home',
                    channel='User function restart ' + run_id, version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    boundary_upload = post(caps['UpdateScriptTask'],
                                           dict(item_id=item, task_id=identity, is_script_running=True))
                    check(boundary_upload.get('state') == 'upload', str(boundary_upload))
                    boundary_request = urllib.request.Request(boundary_upload['uploader'], STACK_SOURCE,
                                                              {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(boundary_request, timeout=10) as response:
                        boundary_complete = decode(ET.fromstring(response.read()))
                    check(boundary_complete.get('state') == 'complete' and
                          boundary_complete.get('compiled') in ('1', 'true', True),
                          'Configured stack probe did not compile: ' + str(boundary_complete))
                    wait_script_chat(viewer, identity, 'stack boundary 118')
                    boundary_deadline = time.monotonic() + 10
                    while True:
                        failure = next((line.strip() for line in server.log
                                        if str(item) in line and 'OutOfStackException' in line), None)
                        if failure:
                            break
                        check(time.monotonic() < boundary_deadline,
                              'Configured 16 KiB stack did not reject recur(119) for the tested script')
                        time.sleep(.05)
                    report['configuredStackProof'] = dict(kibibytes=16, passingDepth=118, failingDepth=119,
                                                         item=str(item), serverError=failure)
                    check(not any(b'stack overflow missing' in packet for packet in viewer.received),
                          'Stack-overflow expression incorrectly completed its owner-chat call')
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], SOURCE,
                                                     {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Nested function script did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'function sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'sleeping:1', 'Script was not suspended inside its first inner call: ' + description)
                else:
                    deadline = time.monotonic() + 30
                    while True:
                        description = properties_description(viewer, identity)
                        if description == '11:1':
                            break
                        check(description == 'sleeping:1', 'Restored nested call replayed or produced wrong result: ' + description)
                        check(time.monotonic() < deadline, 'Restored nested script never finished its sleeping call')
                        time.sleep(.1)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            observation['saved'] = saved_state(root / 'region.db', region, identity, item)
            if cycle == 0:
                check(observation['saved']['description'] == 'sleeping:1' and
                      observation['saved']['containsInnerFrame'] and observation['saved']['containsOuterFrame'],
                      'First process did not persist the suspended inner/outer call frames')
            else:
                check(observation['saved']['description'] == '11:1', 'Second clean shutdown lost the resumed script result')
            report['processes'].append(observation)
        check(report['processes'][0]['saved']['asset'] == report['processes'][1]['saved']['asset'],
              'Restart replaced the script asset instead of restoring the compiled script state')
        report['passed'] = True
        report['object'] = str(identity)
        report['item'] = str(item)
        report['checks'] = [
            'Fresh CLI databases/account and authenticated viewer UDP arrival without SQL writes',
            'Viewer rezzed a cube, created/downloaded task inventory and compiled nested functions through UpdateScriptTask',
            'Actual YEngine ScriptStackSize=16 INI admits recur(118), rejects recur(119), and a normal script upload resets the fault',
            'Clean first shutdown persisted sleeping:1 and both suspended call frames in SQLite',
            'A separate application process resumed the same script to viewer-observed description 11:1 without replay',
            'Second clean shutdown retained the result and the original script asset']
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: nested function resumes across native application processes; evidence: ' + str(root / 'results.json'))


if __name__ == '__main__':
    main()
