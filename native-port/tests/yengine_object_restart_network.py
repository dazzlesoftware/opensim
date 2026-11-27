"""Verify Object aliases, String cache origins and List identity across three clean servers."""
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
from seated_crossing_network import create_script
from lsl_user_function_restart_network import properties_description
from xmr_event_dequeue_restart_network import saved_state, sha
from yengine_reduced_stack_restart_network import configuration


SOURCE = r"""YOption arrays;
object boxed = (object)7;
object alias = boxed;
list values = [1];
object list_alias = (object)values;
string cached = (string)42;
string cache_alias = cached;
string left = llGetSubString("xsame", 1, -1);
string right = llGetSubString("ysame", 1, -1);
integer before_hash;
integer after_hash;
integer repeat_hash;
integer before_box_equal;
integer after_box_equal;
integer strings_equal;
integer cache_equal;
integer list_equal;
integer entries;
integer complete;
default
{
    state_entry()
    {
        entries++;
        before_box_equal = (boxed == alias);
        before_hash = xmrHashCode((object)values);
        llSetObjectDesc("object sleeping");
        llSleep(3.0);
        after_box_equal = (boxed == alias);
        strings_equal = ((object)left == (object)right);
        cache_equal = ((object)cached == (object)(string)42);
        list_equal = ((object)values == list_alias);
        after_hash = xmrHashCode((object)values);
        repeat_hash = xmrHashCode(list_alias);
        complete++;
        llSetObjectDesc("object complete:" + (string)after_box_equal + ":" + (string)strings_equal + ":" +
            (string)cache_equal + ":" + (string)list_equal + ":" + (string)entries + ":" + (string)complete);
    }
}
"""
COMPLETED = 'object complete:0:1:0:1:1:1'


def original_source():
    root = Path(__file__).with_name('yengine_object_process_fixture') / 'process_object_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = SOURCE.encode('utf-8')
    check(source == (root / 'source.lsl').read_bytes(), 'Exact original Object source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                                'resume_exception', 'live_resume_exception']),
          'Actual original Object migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    final = metadata['final']
    check(final['object_description'] == COMPLETED,
          'Actual original production Object identity transitions differ')
    for name, expected in {'before_box_equal': 1, 'after_box_equal': 0, 'strings_equal': 1,
                           'cache_equal': 0, 'list_equal': 1, 'entries': 1, 'complete': 1}.items():
        check(final['globals'][name]['value'] == expected, 'Original Object result differs: ' + name)
    check(final['globals']['after_hash']['value'] == final['globals']['repeat_hash']['value'] and
          final['globals']['before_hash']['value'] == metadata['initial']['globals']['before_hash']['value'],
          'Original List hash alias/stored-value contract differs')
    return root, source


def upload(viewer, login, source):
    local, identity = viewer.create_box()
    caps = post(login['seed_capability'], ['UpdateScriptTask'])
    item = create_script(viewer, local, identity, caps)
    reply = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
    check(reply.get('state') == 'upload', str(reply))
    request = urllib.request.Request(reply['uploader'], source, {'Content-Type': 'application/octet-stream'})
    with urllib.request.urlopen(request, timeout=10) as response:
        result = decode(ET.fromstring(response.read()))
    check(result.get('state') == 'complete' and result.get('compiled') in ('1', 'true', True),
          'Object source failed normal script capability compilation: ' + str(result))
    return identity, item


def check_saved(saved, source, cycle, previous):
    state = saved['decoded']
    check(saved['checkpointVersion'] == 19, 'Object references did not select checkpoint19')
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 18,
          'Object restart changed program/global identity')
    values = state['globals']
    check(len(values) == 18 and values[:8] == [7, 7, [1], [1], '42', '42', 'same', 'same'] and
          all(type(value) is int for value in values[8:]), 'Object values/types differ: ' + repr(values))
    refs = state['global_references']
    check(refs[0]['reference'] != refs[1]['reference'], 'External primitive box aliases were retained')
    check(refs[2]['reference'] == refs[3]['reference'], 'External List aliases split')
    check(refs[4]['reference'] == refs[5]['reference'] and refs[6]['reference'] == refs[7]['reference'],
          'External equal String values did not merge')
    check(refs[4]['kind'] == 'string' and refs[4]['small_integer_cache'] is None and
          refs[4]['literal'] is False and refs[2]['identity_hash'] is None,
          'External persistence retained process-local cache or List hash metadata')
    check(state['global_initialization_pending'] is False and state['global_initializer_cursor'] == -1,
          'Object restart retained constructor phase')
    if cycle == 0:
        check(values[11:] == [1, 0, 0, 0, 0, 1, 0], 'Initial Object handler ran past sleep')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry',
              'Clean shutdown omitted sleeping Object handler')
    else:
        check(values[8] == previous[0]['saved']['decoded']['globals'][8] and values[9] == values[10] and
              values[11:] == [1, 0, 1, 0, 1, 1, 1], 'Object identity/hash/no-replay outcome differs')
        if cycle == 2:
            check(values == previous[1]['saved']['decoded']['globals'], 'Third process replayed Object entry')
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0, 'Completed Object entry retained frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('object-reference-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Object Reference Production Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(source)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root),
                  nativeSha256=sha(native), probeSha256=sha(probe), driverSha256=sha(Path(__file__)),
                  sourceSha256=hashlib.sha256(source).hexdigest(),
                  originalReference=dict(metadataSha256=sha(original / 'metadata.json'),
                                         sourceSha256=sha(original / 'source.lsl')),
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'ObjectReference', 'Viewer'], 'fixture-password\n')):
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
                    first='ObjectReference', last='Viewer', passwd='fixture-password', start='home',
                    channel='Object reference production restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    deadline = time.monotonic() + 5
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'object sleeping':
                            break
                        check(description == '', 'Initial Object marker differs: ' + description)
                        check(time.monotonic() < deadline, 'Object handler did not reach its sleeping marker')
                        time.sleep(.05)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'object sleeping', 'Object result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored Object handler failed to finish')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            check_saved(saved, source, cycle, report['processes'])
            report['processes'].append(observation)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1,
              'Object restart replaced the task script asset')
        final = report['processes'][1]['saved']['decoded']['globals']
        report['observedHashChangedAcrossProcesses'] = final[8] != final[9]
        report['hashInequalityRequired'] = False
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual Object reference three-process persistence: ' + str(root))


if __name__ == '__main__':
    main()
