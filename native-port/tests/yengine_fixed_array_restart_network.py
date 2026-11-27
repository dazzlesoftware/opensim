"""Verify FixedArray aliases, String cache origins and List identity across three clean servers."""
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
YOption chars;
object ints;
object int_alias;
object chars;
object floats;
object objects;
object object_alias;
object typename;
integer before_hash;
integer after_hash;
integer repeat_hash;
integer contents;
integer aliases;
integer type_cache_equal;
integer entries;
integer complete;
default
{
    state_entry()
    {
        entries++;
        ints = xmrFixedArrayAllocI(2);
        xmrFixedArraySetI(ints, 0, 19);
        xmrFixedArraySetI(ints, 1, -7);
        int_alias = ints;
        chars = xmrFixedArrayAllocC(2);
        xmrFixedArraySetC(chars, 0, 'A');
        xmrFixedArraySetC(chars, 1, (char)955);
        floats = xmrFixedArrayAllocF(1);
        xmrFixedArraySetF(floats, 0, xmrString2Float("1.234567891"));
        objects = xmrFixedArrayAllocO(3);
        xmrFixedArraySetO(objects, 0, ints);
        xmrFixedArraySetO(objects, 1, chars);
        xmrFixedArraySetO(objects, 2, "payload");
        object_alias = objects;
        typename = xmrTypeName(objects);
        before_hash = xmrHashCode(objects);
        llSetObjectDesc("array sleeping");
        llSleep(3.0);
        contents = (xmrFixedArrayGetI(ints, 0) == 19 && xmrFixedArrayGetI(int_alias, 1) == -7 &&
            (integer)xmrFixedArrayGetC(chars, 0) == 65 && (integer)xmrFixedArrayGetC(chars, 1) == 955 &&
            xmrFixedArrayGetF(floats, 0) == xmrString2Float("1.234567891") &&
            (string)xmrFixedArrayGetO(objects, 2) == "payload");
        aliases = (ints == int_alias && objects == object_alias &&
            xmrFixedArrayGetO(objects, 0) == ints && xmrFixedArrayGetO(objects, 1) == chars);
        type_cache_equal = (typename == (object)xmrTypeName(objects));
        after_hash = xmrHashCode(objects);
        repeat_hash = xmrHashCode(object_alias);
        complete++;
        llSetObjectDesc("array complete:" + (string)contents + ":" + (string)aliases + ":" +
            (string)type_cache_equal + ":" + (string)entries + ":" + (string)complete);
    }
}
"""
COMPLETED = "array complete:1:1:0:1:1"


def original_source():
    root = Path(__file__).with_name('yengine_fixed_array_process_fixture') / 'process_fixed_array_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = SOURCE.encode('utf-8')
    check(source == (root / 'source.lsl').read_bytes(), 'Exact original fixed-array source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                                'binary_load_exception', 'queue_restore_exception',
                                                'resume_exception', 'live_resume_exception']),
          'Actual original fixed-array migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    check(metadata['final']['object_description'] == COMPLETED and
          metadata['live_resume']['object_description'] == 'array complete:1:1:1:1:1',
          'Original runtime-type cache migration contract differs')
    for name in ['contents', 'aliases', 'entries', 'complete']:
        check(metadata['final']['globals'][name]['value'] == 1, 'Original result differs: ' + name)
    check(metadata['final']['globals']['type_cache_equal']['value'] == 0,
          'Original process cache identity survived migration')
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
          'FixedArray source failed normal script capability compilation: ' + str(result))
    return identity, item


def check_saved(saved, source, cycle, previous):
    state = saved['decoded']
    check(saved['checkpointVersion'] == 20, 'Fixed arrays did not select checkpoint20')
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 15,
          'Fixed-array program/global identity differs')
    values, refs = state['globals'], state['global_references']
    check(len(values) == 15 and len(refs) == 15, 'Fixed-array global count differs')
    for index, kind, items in [(0, 2, [19, -7]), (1, 2, [19, -7]), (2, 0, [65, 955])]:
        check(values[index]['element_kind'] == kind and values[index]['items'] == items,
              'Fixed-array typed contents differ at global ' + str(index))
    original = json.loads((Path(__file__).with_name('yengine_fixed_array_process_fixture') /
                           'process_fixed_array_restart' / 'metadata.json').read_text())
    expected_float_bits = original['initial']['globals']['floats']['value'][0]['bits']
    check(values[3]['element_kind'] == 1 and values[3]['double_bits'] == [expected_float_bits],
          'Float-array IEEE contents differ')
    for index in [4, 5]:
        array = values[index]
        check(array['element_kind'] == 3 and len(array['items']) == 3 and
              array['items'][0]['items'] == [19, -7] and array['items'][1]['items'] == [65, 955] and
              array['items'][2] == 'payload', 'Object-array typed fields differ')
    check(values[6] == 'System.Object[]', 'Stored runtime array type name differs')
    check(refs[0]['reference'] == refs[1]['reference'] and refs[4]['reference'] == refs[5]['reference'] and
          refs[4]['items'][0]['reference'] == refs[0]['reference'] and
          refs[4]['items'][1]['reference'] == refs[2]['reference'], 'Shared array graph aliases split')
    for index, kind, length in [(0, 2, 2), (1, 2, 2), (2, 0, 2), (3, 1, 1), (4, 3, 3), (5, 3, 3)]:
        check(refs[index]['kind'] == 'fixed_array' and refs[index]['element_kind'] == kind and
              refs[index]['length'] == length and refs[index]['identity_hash'] is None,
              'Saved array identity/type/process hash differs at ' + str(index))
    check(refs[6]['kind'] == 'string' and refs[6]['array_type_cache'] is None and
          refs[6]['literal'] is False, 'Saved runtime type retained process cache origin')
    check(state['global_initialization_pending'] is False and state['global_initializer_cursor'] == -1,
          'Fixed-array restart retained constructor phase')
    if cycle == 0:
        check(values[8:] == [0, 0, 0, 0, 0, 1, 0], 'Initial handler ran past real sleep')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry',
              'Clean shutdown omitted sleeping fixed-array handler')
    else:
        check(values[7] == previous[0]['saved']['decoded']['globals'][7] and
              values[8] == values[9] and values[10:] == [1, 1, 0, 1, 1],
              'Fixed-array contents/cache/hash/no-replay result differs')
        if cycle == 2:
            check(values == previous[1]['saved']['decoded']['globals'], 'Third process replayed entry')
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0, 'Completed fixed-array entry retained frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('fixed-array-reference-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=FixedArray Reference Production Restart'), encoding='utf-8')
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
                                  (['--create-user', 'FixedArrayReference', 'Viewer'], 'fixture-password\n')):
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
                    first='FixedArrayReference', last='Viewer', passwd='fixture-password', start='home',
                    channel='FixedArray reference production restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    deadline = time.monotonic() + 5
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'array sleeping':
                            break
                        check(description == '', 'Initial FixedArray marker differs: ' + description)
                        check(time.monotonic() < deadline, 'FixedArray handler did not reach its sleeping marker')
                        time.sleep(.05)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'array sleeping', 'FixedArray result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored FixedArray handler failed to finish')
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
              'FixedArray restart replaced the task script asset')
        final = report['processes'][1]['saved']['decoded']['globals']
        report['observedHashChangedAcrossProcesses'] = final[7] != final[8]
        report['hashInequalityRequired'] = False
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual FixedArray reference three-process persistence: ' + str(root))


if __name__ == '__main__':
    main()
