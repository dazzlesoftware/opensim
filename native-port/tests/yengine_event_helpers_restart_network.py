"""Persist real script event helpers, boxed values and queued detector data.

Fresh account/login, UDP rez/task creation and normal script-editor upload are
used throughout. SQL is read only after clean shutdown in isolated run folders.
"""

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
list saved;
list result;
list typed;
string seen;
default {
    state_entry() {
        ++calls;
        typed = [(key)"11111111-2222-3333-4444-555555555555", 33, 1.5, "plain"];
        xmrEventLoadDets([1, "11111111-2222-3333-4444-555555555555", <1.125,2.25,3.5>, 7,
            "22222222-3333-4444-5555-666666666666", "saved detector", (string)llGetOwner(),
            <4.125,5.25,6.5>, <0,0,0,1>, 3, <7.125,8.25,9.5>,
            <0.125,0.25,0>, <0,0,1>, <0,1,0>, <10.125,11.25,12.5>, <0.5,0.75,0>, 5]);
        saved = xmrEventSaveDets();
        xmrEventEnqueue([21,7]);
        xmrEventLoadDets([1]);
        llSetObjectDesc("helpers sleeping:" + (string)calls);
        llOwnerSay("event helpers sleeping");
        llSleep(3.0);
        result = xmrEventDequeue(5.0,2097152,0,0,0);
        seen = llDetectedName(0);
        xmrEventLoadDets(saved);
        llSetObjectDesc((string)calls + ":" + (string)llList2Integer(result,0) + ":" +
            (string)llList2Integer(result,1) + ":" + seen + ":" + (string)llDetectedLinkNumber(0) + ":" +
            (string)llGetListEntryType(typed,0) + ":" + (string)llGetListEntryType(typed,1) + ":" +
            (string)llGetListEntryType(typed,2) + ":" + (string)llGetListEntryType(typed,3));
        llOwnerSay("event helpers completed");
    }
    touch_start(integer n) {}
}'''
# Original LSL_Api.llGetListEntryType recognizes UUID text as TYPE_KEY even
# when the actual list element is an LSLString. Wire boxing is checked below.
COMPLETE = '1:21:7:saved detector:7:4:1:2:3'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('event-helpers-' + uuid.uuid4().hex)
    root.mkdir()
    region = uuid.uuid4()
    control = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Bundled default assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, control, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Event Helpers Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(SOURCE)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root), nativeSha256=sha(native),
                  probeSha256=sha(probe), testSha256=sha(Path(__file__)), sourceSha256=sha(root / 'source.lsl'),
                  helperSha256={name: sha(Path(__file__).with_name(name)) for name in
                                ('preview_entry_network.py', 'seated_crossing_network.py',
                                 'lsl_user_function_restart_network.py', 'xmr_event_dequeue_restart_network.py',
                                 'yengine_reduced_stack_restart_network.py')},
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Helper', 'Viewer'], 'fixture-password\n')):
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
                    first='Helper', last='Viewer', passwd='fixture-password', start='home',
                    channel='Event helpers restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], SOURCE, {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Event helper source did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'event helpers sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'helpers sleeping:1', 'Helpers did not reach the intended first suspension: ' + description)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETE:
                            break
                        check(description == 'helpers sleeping:1', 'Helpers replayed or lost their queued detector/type data: ' + description)
                        check(time.monotonic() < deadline, 'Restored event helper sequence never completed')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            state = saved['decoded']
            check(saved['checkpointVersion'] in (11, 12) and state['program_hash'] == hashlib.md5(SOURCE).hexdigest() and
                  state['globals'][0] == 1 and len(state['globals'][1]) == 17 and len(state['globals'][3]) == 4,
                  'Checkpoint did not retain boxed helper state, source identity or one entry side effect')
            boxes = state['global_boxed_counts']
            check(boxes == dict(integer=5 if cycle == 0 else 7, float=1, string=6, key=0),
                  'Checkpoint erased original integer/float/string/key boxing identity: ' + str(boxes))
            if cycle == 0:
                check(state['queued_event_count'] == 1 and state['active_event'] == 'state_entry' and
                      state['sleep_remaining'] > 0 and state['globals'][2] == [],
                      'First shutdown lost the queued event or skipped the sleeping helper sequence')
            else:
                check(state['globals'][2] == [21, 7] and state['globals'][4] == 'saved detector' and
                      not state['active_event'] and not state['event_wait'] and state['queued_event_count'] == 0,
                      'Restored helpers did not return their exact queued arguments and detector snapshot')
            report['processes'].append(observation)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1, 'Restart replaced the helper script asset')
        report.update(passed=True, object=str(identity), item=str(item), checks=[
            'Normal account/login, authenticated UDP rez/task creation and script-editor capability upload',
            'Load/Save all detector data, enqueue an event, then replace active detectors before a saved sleep',
            'Checkpoint retains boxed integer/float/string elements, original source-key string boxing and queued detectors',
            'A fresh process dequeues exact arguments and old detector data, reloads the saved detector list, and reports key/list types',
            'A third process retains the result, same asset and one entry side effect'])
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: event helpers, queued detectors and boxed types survive actual process restarts: ' + str(root))


if __name__ == '__main__':
    main()
