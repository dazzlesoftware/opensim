"""Verify constructor helpers and a sleeping initializer through real restarts."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time
import urllib.request
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, decode, post
from seated_crossing_network import create_script, wait_script_chat
from lsl_user_function_restart_network import properties_description
from xmr_event_dequeue_restart_network import saved_state, sha
from yengine_reduced_stack_restart_network import configuration


SOURCE = b'''integer calls;
integer entries;
list saved;
list result;
string seen;
integer value = initialize();
integer initialize() {
    ++calls;
    llResetScript();
    xmrEventLoadDets([1, "11111111-2222-3333-4444-555555555555", <1,2,3>, 7,
        "22222222-3333-4444-5555-666666666666", "constructor detector", (string)llGetOwner(),
        <4,5,6>, <0,0,0,1>, 3, <7,8,9>,
        <0.125,0.25,0>, <0,0,1>, <0,1,0>, <10,11,12>, <0.5,0.75,0>, 5]);
    saved = xmrEventSaveDets();
    xmrEventEnqueue([21,9]);
    xmrEventLoadDets([1]);
    llSetObjectDesc("initializing:" + (string)calls);
    llOwnerSay("constructor sleeping");
    llSleep(3.0);
    result = xmrEventDequeue(5.0,2097152,0,0,0);
    seen = llDetectedName(0);
    xmrEventLoadDets(saved);
    return 7;
}
default {
    state_entry() {
        ++entries;
        llSetObjectDesc((string)calls + ":" + (string)entries + ":" + (string)value + ":" +
            (string)llList2Integer(result,0) + ":" + (string)llList2Integer(result,1) + ":" +
            seen + ":" + (string)llDetectedLinkNumber(0));
        llOwnerSay("constructor completed");
    }
    touch_start(integer n) {}
}'''
COMPLETE = '1:1:7:21:9:constructor detector:7'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('global-event-' + uuid.uuid4().hex)
    root.mkdir()
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled default assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Constructor Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(SOURCE)
    command = [str(native), '--config', str(config)]
    original_fixture = Path(__file__).with_name('yengine_global_event_process_fixture') / 'process_constructor_restart'
    original = json.loads((original_fixture / 'metadata.json').read_text(encoding='utf-8'))
    check(original['accepted'] and original['suspended'] and original['migrated'] and
          original['initial_exception'] is None and original['resume_exception'] is None and
          original['final']['object_description'] == COMPLETE and
          sha(original_fixture / 'source.lsl') == hashlib.sha256(SOURCE).hexdigest() == original['sha256']['source.lsl'],
          'Exact original constructor source/execution provenance mismatch')
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root), nativeSha256=sha(native),
                  probeSha256=sha(probe), scriptSha256=sha(Path(__file__)), sourceSha256=hashlib.sha256(SOURCE).hexdigest(),
                  originalReference=dict(metadataSha256=sha(original_fixture / 'metadata.json'),
                                         sourceSha256=sha(original_fixture / 'source.lsl')),
                  helperSha256={name: sha(Path(__file__).with_name(name)) for name in
                                ('preview_entry_network.py', 'seated_crossing_network.py',
                                 'lsl_user_function_restart_network.py', 'xmr_event_dequeue_restart_network.py',
                                 'yengine_reduced_stack_restart_network.py')},
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Constructor', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True,
                                   capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(3):
            server = PreviewServer(command, root, root / f'server-{cycle}.log')
            viewer = None
            observation = dict(cycle=cycle)
            try:
                url = server.start()
                observation['pid'] = server.process.pid
                login = xmlrpc.client.ServerProxy(url).login_to_simulator(dict(
                    first='Constructor', last='Viewer', passwd='fixture-password', start='home',
                    channel='Constructor restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity,
                                                                is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], SOURCE,
                                                     {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Constructor source did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'constructor sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'initializing:1', 'Initializer replayed or skipped its first suspension')
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETE:
                            break
                        check(description == 'initializing:1', 'Constructor result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored constructor did not complete')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            state = saved['decoded']
            check(saved['checkpointVersion'] == 12 and state['global_initialization_state'] and
                  state['program_hash'] == hashlib.md5(SOURCE).hexdigest() and state['initialized_globals'] == 6 and
                  state['globals'][0] == 1 and len(state['globals'][2]) == 17,
                  'Checkpoint lost allocated globals, source identity or constructor schema')
            check(state['global_boxed_counts'] == dict(integer=4 if cycle == 0 else 6, float=0, string=4, key=0),
                  'Constructor helper values lost their original boxed types')
            if cycle == 0:
                check(state['global_initialization_pending'] and state['global_initializer_cursor'] == 5 and
                      state['active_event'] == 'state_entry' and state['function'] and state['call_stack_depth'] >= 1 and
                      state['sleep_remaining'] > 0 and state['queued_event_count'] == 1 and
                      state['globals'][1] == 0 and state['globals'][3] == [] and state['globals'][5] == 0,
                      'First shutdown did not retain the actual constructor event and suspended initializer')
            else:
                check(not state['global_initialization_pending'] and state['global_initializer_cursor'] == -1 and
                      state['globals'][1] == 1 and state['globals'][3] == [21, 9] and
                      state['globals'][4] == 'constructor detector' and state['globals'][5] == 7 and
                      not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
                      not state['event_wait'] and state['queued_event_count'] == 0,
                      'Constructor restart replayed initialization, lost detectors or skipped its entry body')
            report['processes'].append(observation)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1,
              'Constructor restart replaced the original script asset')
        report.update(passed=True, object=str(identity), item=str(item), checks=[
            'Fresh account/login, authenticated UDP rez/task creation and normal script-editor capability upload',
            'Global initializer shares state_entry context, ignores self-reset, saves detectors and enqueues an event',
            'Clean shutdown records checkpoint12, actual state_entry, initializer cursor and suspended function frames',
            'A fresh process resumes once, dequeues captured detector data, completes globals and executes entry once',
            'A third process retains completed globals, boxed types, same asset and no pending constructor phase'])
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: constructor helpers and initializer frames survive actual process restarts: ' + str(root))


if __name__ == '__main__':
    main()
