"""Resume a native event-dequeue wait through real clean application restarts.

Uses CLI account creation, authenticated viewer UDP, normal script-editor caps
and actual viewer chat. SQL is read only after each process exits cleanly.
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
from lsl_user_function_restart_network import properties_description


SOURCE = b'''integer calls;
list result;
default {
    state_entry() {
        ++calls;
        llListen(77, "", llGetOwner(), "");
        llSetObjectDesc("waiting:" + (string)calls);
        llOwnerSay("dequeue waiting");
        result = xmrEventDequeue(120.0, 32768, 0, 0, 0);
        llSetObjectDesc((string)llList2Integer(result, 0) + ":" + llList2String(result, 4) + ":" + (string)calls);
        llOwnerSay("dequeue completed");
    }
    listen(integer channel, string name, key id, string message) {}
}'''

BACKGROUND_SOURCE = b'''integer calls;
integer background_started;
integer background_completed;
list result;
default {
    state_entry() {
        ++calls;
        llListen(77, "", llGetOwner(), "");
        llSetTimerEvent(1.0);
        result = xmrEventDequeue(120.0, 32768, 0, 4, 0);
        llSetObjectDesc((string)llList2Integer(result, 0) + ":" + llList2String(result, 4) + ":" +
                        (string)calls + ":" + (string)background_started + ":" + (string)background_completed);
        llOwnerSay("dequeue completed");
    }
    timer() {
        llSetTimerEvent(0.0);
        ++background_started;
        llSetObjectDesc("background:" + (string)calls + ":" + (string)background_started);
        llOwnerSay("background sleeping");
        llSleep(3.0);
        ++background_completed;
        llSetObjectDesc("background complete:" + (string)calls + ":" + (string)background_completed);
    }
    listen(integer channel, string name, key id, string message) {}
}'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def saved_state(root, region, identity, item, probe, cycle):
    path = root / 'region.db'
    with contextlib.closing(sqlite3.connect(f'file:{path.as_posix()}?mode=ro', uri=True)) as database:
        row = database.execute('SELECT Description FROM prims WHERE UUID=?', (str(identity),)).fetchone()
        state = database.execute(
            'SELECT asset_id,checkpoint FROM native_script_states WHERE region_id=? AND part_id=? AND item_id=?',
            (str(region), str(identity), str(item))).fetchone()
    check(row is not None and state is not None, 'Clean shutdown omitted the script or prim')
    payload = state[1]
    check(isinstance(payload, bytes) and payload.startswith(b'LSC'), 'Saved state is not a native checkpoint')
    checkpoint = root / f'checkpoint-{cycle}.bin'
    checkpoint.write_bytes(payload)
    decoded = subprocess.run([str(probe), '--checkpoint', str(checkpoint)], capture_output=True, text=True, timeout=15)
    check(decoded.returncode == 0, decoded.stdout + decoded.stderr)
    return dict(description=row[0], asset=str(state[0]), checkpointVersion=payload[3],
                checkpointSha256=sha(checkpoint), checkpointBytes=len(payload), decoded=json.loads(decoded.stdout))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--background', action='store_true', help='Restart while a background timer handler sleeps')
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    source = BACKGROUND_SOURCE if args.background else SOURCE
    initial_description = 'background:1:1' if args.background else 'waiting:1'
    completed_description = '15:hello:1:1:1' if args.background else '15:hello:1'
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('event-wait-' + uuid.uuid4().hex)
    root.mkdir()
    region = uuid.uuid4()
    control = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Bundled default assets missing')
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
Name=Event Wait Restart
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
    (root / 'source.lsl').write_bytes(source)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root), nativeSha256=sha(native),
                  probeSha256=sha(probe), testSha256=sha(Path(__file__)), sourceSha256=sha(root / 'source.lsl'),
                  helperSha256={name: sha(Path(__file__).with_name(name)) for name in
                                ('preview_entry_network.py', 'seated_crossing_network.py', 'lsl_user_function_restart_network.py')},
                  databaseWritesByFixture=False, realFirestormVerified=False, background=args.background, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Dequeue', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True, capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(3):
            server = PreviewServer(command, root, root / f'server-{cycle}.log')
            viewer = None
            observation = dict(cycle=cycle)
            try:
                url = server.start()
                observation['pid'] = server.process.pid
                login = xmlrpc.client.ServerProxy(url).login_to_simulator(dict(
                    first='Dequeue', last='Viewer', passwd='fixture-password', start='home',
                    channel='Event dequeue restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], source, {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Event-wait script did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'background sleeping' if args.background else 'dequeue waiting')
                    description = properties_description(viewer, identity)
                    check(description == initial_description, 'First process did not reach the intended suspended context: ' + description)
                else:
                    if cycle == 1:
                        if args.background:
                            deadline = time.monotonic() + 10
                            while True:
                                description = properties_description(viewer, identity)
                                if description == 'background complete:1:1':
                                    break
                                check(description == initial_description, 'Background handler replayed or lost its outer context: ' + description)
                                check(time.monotonic() < deadline, 'Restored background handler did not finish its saved sleep')
                                time.sleep(.05)
                        else:
                            check(properties_description(viewer, identity) == initial_description,
                                  'Restored wait finished before the viewer supplied a matching event')
                        message = b'hello\0'
                        viewer.send(bytes.fromhex('ffff0050') + viewer.credentials() +
                                    struct.pack('<H', len(message)) + message + struct.pack('<Bi', 1, 77))
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == completed_description:
                            break
                        expected_pending = 'background complete:1:1' if args.background else initial_description
                        check(description == expected_pending, 'Wait replayed side effects or returned wrong event: ' + description)
                        check(time.monotonic() < deadline, 'Actual viewer chat did not resume the restored wait')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            state = saved['decoded']
            check(state['globals'][0] == 1, 'Wait restart replayed its earlier increment')
            if cycle == 0:
                if args.background:
                    backgrounds = state['detector_state']['background_frames']
                    check(saved['checkpointVersion'] == 13 and saved['description'] == initial_description and
                          state['active_event'] == 'state_entry' and state['event_context_depth'] == 0 and
                          state['resume_entry_pending'] and state['resume_frame_count'] == 3 and
                          not state['event_wait'] and len(backgrounds) == 1 and
                          backgrounds[0]['index'] == 1 and backgrounds[0]['event'] == 'timer' and
                          backgrounds[0]['detectors'] == [] and
                          backgrounds[0]['return_mask1'] == 32768 and backgrounds[0]['return_mask2'] == 0 and
                          backgrounds[0]['background_mask1'] == 4 and backgrounds[0]['background_mask2'] == 0 and
                          state['globals'][1:3] == [1, 0] and state['sleep_remaining'] > 0,
                          'First shutdown did not preserve the original root, handwritten background wait and timer frame')
                else:
                    check(saved['checkpointVersion'] in (9, 10, 11, 12) and saved['description'] == initial_description and state['active_event'] == 'state_entry' and
                          state['event_wait'] and state['event_wait_return_mask1'] == 32768 and state['event_wait_return_mask2'] == 0,
                          'First shutdown did not persist the active event wait in the new core checkpoint format')
            else:
                result = state['globals'][3 if args.background else 1]
                check(saved['description'] == completed_description and len(result) == 5 and
                      result[0] == 15 and result[1] == 77 and result[2] == 'Dequeue Viewer' and
                      uuid.UUID(str(result[3])) == uuid.UUID(str(login['agent_id'])) and result[4] == 'hello' and
                      not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and not state['event_wait'],
                      'Completed event wait or returned arguments were not retained across clean restart')
                if args.background:
                    check(state['globals'][1:3] == [1, 1] and state['event_context_depth'] == 0,
                          'Restart replayed background side effects or retained an abandoned event context')
            report['processes'].append(observation)
        check(len({case['saved']['asset'] for case in report['processes']}) == 1, 'Restart replaced the script asset')
        report.update(passed=True, object=str(identity), item=str(item), checks=[
            'Normal CLI account, login, UDP rez/task creation and UpdateScriptTask capability upload',
            'Plain event handler persists a pending native event wait through a clean process shutdown',
            'Authenticated viewer chat after restart resumes the exact wait and returns its event arguments once',
            'A third process retains the completed result, original asset and single pre-wait side effect'])
        if args.background:
            report['checks'] = [
                'Normal account/login, authenticated UDP rez/task creation and script-editor capability upload',
                'First clean shutdown persists the sleeping timer handler and outer state_entry wait in checkpoint10',
                'A fresh process completes the same background sleep once, then returns to the outer wait',
                'Authenticated viewer chat resumes that outer wait with exact listen arguments and single handler side effects',
                'A third clean process retains the result, original asset and no abandoned event contexts']
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: event wait resumes through actual viewer chat across clean application processes: ' + str(root))


if __name__ == '__main__':
    main()
