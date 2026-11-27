"""Check live UTF16 values and original-compatible clean process persistence."""
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

SOURCE_TEXT = '''YOption chars;
string original;
list boxed;
string result;
integer unit_before;
integer unit_after;
integer calls;
integer entries;
integer done;
string echo(string value) {
    string local = value;
    ++calls;
    unit_before = (integer)local[0];
    llSetObjectDesc("utf16 sleeping:" + (string)unit_before);
    llOwnerSay("utf16 sleeping");
    llSleep(3.0);
    unit_after = (integer)local[0];
    return local;
}
default {
    state_entry() {
        ++entries;
        original = llGetSubString("😀", 0, 0);
        boxed = [original];
        result = echo(original);
        done = 1;
        llSetObjectDesc("utf16 completed:" + (string)unit_after + ":" + (string)calls + ":" + (string)entries);
    }
}'''
SOURCE = SOURCE_TEXT.encode('utf-8')
PAIR_SOURCE = SOURCE_TEXT.replace('llGetSubString("😀", 0, 0)', 'llGetSubString("😀", 0, 1)').encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--pair', action='store_true', help='Run the valid surrogate-pair control')
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('utf16-' + uuid.uuid4().hex)
    root.mkdir()
    source = PAIR_SOURCE if args.pair else SOURCE
    normalized = [55357, 56832] if args.pair else [65533]
    completed_unit = normalized[0]
    complete_description = f'utf16 completed:{completed_unit}:1:1'
    original_fixture = Path(__file__).with_name('yengine_utf16_process_fixture') / ('pair' if args.pair else 'high')
    original = json.loads((original_fixture / 'metadata.json').read_text(encoding='utf-8'))
    check(original['accepted'] and original['suspended'] and original['migrated'] and
          original['initial_exception'] is None and original['production_write_exception'] is None and
          original['resume_exception'] is None and original['live_resume_exception'] is None,
          'Exact original source/capture/production migration did not succeed')
    check((original_fixture / 'source.lsl').read_bytes() == source and
          original['sha256']['source.lsl'] == hashlib.sha256(source).hexdigest(),
          'Exact original UTF16 process source identity differs')
    check(original['live_resume']['globals']['unit_after']['value'] == 55357 and
          original['final']['globals']['unit_after']['value'] == completed_unit and
          original['final']['globals']['result']['utf16'] == normalized and
          original['final']['object_description'] == complete_description,
          'Original live versus production persistence evidence differs')
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled default assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=UTF16 Production Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(source)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), pairControl=args.pair, caseDirectory=str(root),
                  nativeSha256=sha(native), probeSha256=sha(probe), driverSha256=sha(Path(__file__)),
                  sourceSha256=hashlib.sha256(source).hexdigest(),
                  originalReference=dict(metadataSha256=sha(original_fixture / 'metadata.json'),
                                         sourceSha256=sha(original_fixture / 'source.lsl')),
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Unicode', 'Viewer'], 'fixture-password\n')):
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
                    first='Unicode', last='Viewer', passwd='fixture-password', start='home',
                    channel='UTF16 production restart fixture', version='1.0'))
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
                    request = urllib.request.Request(upload['uploader'], source,
                                                     {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        uploaded = decode(ET.fromstring(response.read()))
                    check(uploaded.get('state') == 'complete' and uploaded.get('compiled') in ('1', 'true', True),
                          'UTF16 source did not compile through normal script capability: ' + str(uploaded))
                    wait_script_chat(viewer, identity, 'utf16 sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'utf16 sleeping:55357', 'Before-stop LIVE UTF16 unit was lost')
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == complete_description:
                            break
                        check(description == 'utf16 sleeping:55357', 'Restored UTF16 result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored UTF16 function did not finish')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            report['processes'].append(observation)
            state = saved['decoded']
            check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 8,
                  'UTF16 persistence changed source/global identity')
            check(state['global_string_units'][0] == normalized and
                  state['global_string_units'][2] == ([] if cycle == 0 else normalized),
                  'Persisted raw string units differ from actual original production migration')
            check(state['global_boxed_counts']['string'] == 1 and state['globals'][1] ==
                  ['😀' if args.pair else '\ufffd'], 'Persisted boxed string identity/value differs')
            check(state['globals'][3:] == [55357, 0 if cycle == 0 else completed_unit, 1, 1, 0 if cycle == 0 else 1],
                  'UTF16 function/entry counters or before/after units differ')
            if cycle == 0:
                check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
                      state['function'] and state['call_stack_depth'] >= 1,
                      'Clean shutdown omitted sleeping UTF16 callee/caller state')
            else:
                check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
                      state['queued_event_count'] == 0, 'UTF16 restart replayed entry or function')
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1, 'UTF16 restart replaced asset')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual UTF16 production persistence: ' + str(root))


if __name__ == '__main__':
    main()
