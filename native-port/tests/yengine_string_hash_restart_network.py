"""Verify process-seeded String hashes across ordinary entry and three clean servers."""
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


COMPLETED = 'hash complete:1:1:1'


def original_source():
    root = Path(__file__).with_name('yengine_string_hash_process_fixture') / 'process_a' / 'process_string_hash_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = (root / 'source.lsl').read_bytes()
    check(source == Path(__file__).with_name('yengine_string_hash_process_source.lsl').read_bytes(),
          'Exact original String hash source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                                'resume_exception', 'live_resume_exception']),
          'Actual original String hash migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    initial = metadata['initial']['globals']
    check(initial['input']['value'] == 'restart \u03bb\U0001f600' and initial['entries']['value'] == 1 and
          all(initial[name]['value'] == 0 for name in ['result', 'repeat_hash', 'complete']),
          'Original sleeping hash witness differs')
    for key in ['live_resume', 'final']:
        state = metadata[key]
        globals_ = state['globals']
        check(state['object_description'] == COMPLETED and
              globals_['before_hash']['value'] == initial['before_hash']['value'] and
              globals_['result']['value'] == globals_['repeat_hash']['value'] and
              all(globals_[name]['value'] == 1 for name in ['entries', 'complete']),
              'Original String hash resume/no-replay result differs')
    imported_root = root.parent.parent / 'process_import' / 'process_string_hash_restart'
    imported = json.loads((imported_root / 'metadata.json').read_text(encoding='utf-8'))
    check(imported['accepted'] and imported['imported_capture'] and
          imported['initial_exception'] is None and imported['resume_exception'] is None,
          'Actual separate-process original import failed')
    final = imported['final']['globals']
    check(imported['final']['object_description'] == COMPLETED and
          final['before_hash']['value'] == initial['before_hash']['value'] and
          final['result']['value'] == final['repeat_hash']['value'] and
          final['entries']['value'] == final['complete']['value'] == 1,
          'Original separate-process stored/recomputed hash contract differs')
    check(sha(imported_root / 'source.lsl') == hashlib.sha256(source).hexdigest(),
          'Original separate-process source differs')
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
          'String hash source failed normal script capability compilation: ' + str(result))
    return identity, item


def check_saved(saved, source, cycle, previous):
    state = saved['decoded']
    check(saved['checkpointVersion'] == 12, 'Pure String hash program did not retain checkpoint12')
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 6,
          'String hash restart changed program/global identity')
    values = state['globals']
    check(len(values) == 6 and values[0] == 'restart \u03bb\U0001f600' and
          all(type(value) is int and -(2 ** 31) <= value < 2 ** 31 for value in values[1:]),
          'String hash scalar types/code units differ: ' + repr(values))
    check(state['global_initialization_pending'] is False and state['global_initializer_cursor'] == -1,
          'String hash entry retained constructor phase')
    if cycle == 0:
        check(values[2:] == [0, 0, 1, 0], 'Sleeping String hash source ran past its capture point')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
              not state['function'] and state['call_stack_depth'] == 0,
              'Clean shutdown omitted the sleeping root handler')
    else:
        first = previous[0]['saved']['decoded']['globals']
        check(values[1] == first[1] and values[2] == values[3] and values[4:] == [1, 1],
              'Stored hash changed, entry replayed, or same-process equal Strings hashed differently')
        if cycle == 2:
            check(values == previous[1]['saved']['decoded']['globals'],
                  'Third process recomputed completed hashes')
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0,
              'Completed String hash source retained active frames or replayed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('string-hash-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=String Hash Production Restart'), encoding='utf-8')
    (root / 'source.lsl').write_bytes(source)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root),
                  nativeSha256=sha(native), probeSha256=sha(probe), driverSha256=sha(Path(__file__)),
                  sourceSha256=hashlib.sha256(source).hexdigest(),
                  originalReference=dict(metadataSha256=sha(original / 'metadata.json'),
                                         sourceSha256=sha(original / 'source.lsl'),
                                         separateProcessMetadataSha256=sha(original.parent.parent / 'process_import' /
                                                                          'process_string_hash_restart' / 'metadata.json')),
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'StringHash', 'Viewer'], 'fixture-password\n')):
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
                    first='StringHash', last='Viewer', passwd='fixture-password', start='home',
                    channel='String hash production restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    deadline = time.monotonic() + 5
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'hash sleeping':
                            break
                        check(description == '', 'Initial String hash marker differs: ' + description)
                        check(time.monotonic() < deadline, 'String hash handler did not reach its sleeping marker')
                        time.sleep(.05)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'hash sleeping', 'String hash result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored String hash handler failed to finish')
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
              'String hash restart replaced the task script asset')
        final = report['processes'][1]['saved']['decoded']['globals']
        report['observedHashChangedAcrossProcesses'] = final[1] != final[2]
        report['hashInequalityRequired'] = False
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual String hash three-process persistence: ' + str(root))


if __name__ == '__main__':
    main()
