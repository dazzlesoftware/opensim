"""Verify numeric format values through real process restarts."""

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


SOURCE = b'''string gi = xmrInteger2String(125,"G2");
string gf = xmrFloat2String(-0.04,"0.000;(0.0)");
string gv = xmrVector2String(<0,0,0>,"#");
string gr = xmrRotation2String(<0,0,0,1>,"F1");
string fi;
string ff;
string fv;
string fr;
integer calls;
integer entries;
integer done;
integer format_values() {
    ++calls;
    fi = xmrInteger2String(125,"G2");
    ff = xmrFloat2String(-0.04,"0.000;(0.0)");
    fv = xmrVector2String(<0,0,0>,"#");
    fr = xmrRotation2String(<0,0,0,1>,"F1");
    return 0;
}
default {
    state_entry() {
        ++entries;
        format_values();
        llSetObjectDesc("format sleeping");
        llOwnerSay("format sleeping");
        llSleep(3.0);
        done = 1;
        llSetObjectDesc("format completed");
    }
}'''
COMPLETE = 'format completed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('numeric-format-' + uuid.uuid4().hex)
    root.mkdir()
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled default assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Numeric Format Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(SOURCE)
    command = [str(native), '--config', str(config)]
    original_fixture = Path(__file__).with_name('yengine_numeric_format_process_fixture') / 'process_format_restart'
    original = json.loads((original_fixture / 'metadata.json').read_text(encoding='utf-8'))
    check(original['accepted'] and original['suspended'] and original['migrated'] and
          original['initial_exception'] is None and original['resume_exception'] is None and
          original['final']['object_description'] == COMPLETE and
          sha(original_fixture / 'source.lsl') == hashlib.sha256(SOURCE).hexdigest() == original['sha256']['source.lsl'],
          'Exact original numeric format source/execution provenance mismatch')
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
                                  (['--create-user', 'Format', 'Viewer'], 'fixture-password\n')):
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
                    first='Format', last='Viewer', passwd='fixture-password', start='home',
                    channel='Numeric format restart fixture', version='1.0'))
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
                          'Numeric-format source did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'format sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'format sleeping', 'Format script replayed or skipped its first suspension')
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETE:
                            break
                        check(description == 'format sleeping', 'Numeric-format result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored format script did not complete')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            state = saved['decoded']
            expected_values = ['1.3E+02', '-0.000', '<,,>', '<0.0,0.0,0.0,1.0>']
            check(saved['checkpointVersion'] == 12 and state['global_initialization_state'] and
                  not state['global_initialization_pending'] and state['global_initializer_cursor'] == -1 and
                  state['program_hash'] == hashlib.md5(SOURCE).hexdigest() and state['initialized_globals'] == 11,
                  'Checkpoint lost source identity, allocated globals or completed initializer phase')
            check(state['globals'][:4] == expected_values and state['globals'][4:8] == expected_values and
                  state['globals'][8:10] == [1, 1],
                  'Exact numeric-format results changed or user function/entry replayed')
            if cycle == 0:
                check(state['globals'][10] == 0 and state['active_event'] == 'state_entry' and
                      state['sleep_remaining'] > 0 and state['queued_event_count'] == 0 and
                      state['event_context_depth'] == 0,
                      'First shutdown did not retain actual script sleep after formatting')
            else:
                check(state['globals'][10] == 1 and not state['active_event'] and
                      not state['function'] and state['call_stack_depth'] == 0 and
                      not state['event_wait'] and state['queued_event_count'] == 0 and
                      state['event_context_depth'] == 0,
                      'Restart replayed entry/function or lost suspended continuation')
            report['processes'].append(observation)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1,
              'Numeric-format restart replaced the original script asset')
        report.update(passed=True, object=str(identity), item=str(item), checks=[
            'Fresh account/login, authenticated UDP rez/task creation and script-editor capability upload',
            'All four numeric format APIs execute in global initializers and a user function',
            'Clean shutdown records exact string globals and a real sleeping event',
            'Fresh process completes the event without replaying formatting, user function or entry',
            'Third process retains all exact string values, counters and original script asset'])
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: numeric format values survive actual process restarts: ' + str(root))


if __name__ == '__main__':
    main()
