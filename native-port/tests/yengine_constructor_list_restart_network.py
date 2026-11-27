"""Verify typed List-null constructor state across ordinary entry and three clean servers."""
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


COMPLETED = 'list ctor complete:0:0:1:1'


def original_source():
    root = Path(__file__).with_name('yengine_constructor_list_process_fixture') / 'process_constructor_list_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = (root / 'source.lsl').read_bytes()
    check(source == Path(__file__).with_name('yengine_constructor_list_process_source.lsl').read_bytes(),
          'Exact original constructor List source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                                'resume_exception', 'live_resume_exception']),
          'Actual original constructor List migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    initial = metadata['initial']['globals']
    check(initial['calls']['value'] == 1 and initial['entries']['value'] == 0 and
          initial['early']['type'] == 'null' and initial['later']['type'] == 'null',
          'Original paused constructor null witness differs')
    for key in ['live_resume', 'final']:
        state = metadata[key]
        globals_ = state['globals']
        check(state['object_description'] == COMPLETED and globals_['early']['type'] == 'null' and
              globals_['later']['type'] == 'list' and globals_['later']['value'] == [] and
              all(globals_[name]['value'] == 1 for name in ['calls', 'entries']),
              'Original List-null identity or no-replay result differs')
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
          'Constructor List source failed normal script capability compilation: ' + str(result))
    return identity, item


def check_saved(saved, source, cycle):
    state = saved['decoded']
    check(saved['checkpointVersion'] == 17, 'Typed List-null roots did not select checkpoint17')
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 4,
          'Constructor List restart changed program/global identity')
    expected = [1, 0, None, None] if cycle == 0 else [1, 1, None, []]
    check(state['globals'] == expected, 'Constructor List globals changed: ' + repr(state['globals']))
    check(state['global_reference_null_counts'] == dict(list=2 if cycle == 0 else 1, object=0),
          'Typed List-null references were confused with Object-null or empty List')
    check(state['global_null_counts'] == dict(raw_string=0, boxed_string=0, boxed_key=0),
          'Typed List null was confused with String null')
    if cycle == 0:
        check(state['global_initialization_pending'] is True and state['global_initializer_cursor'] == 2,
              'Paused constructor phase/cursor differs from source declaration order')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
              state['function'].startswith('pause:') and state['call_stack_depth'] >= 1,
              'Clean shutdown omitted the sleeping constructor callee/caller')
    else:
        check(state['global_initialization_pending'] is False and state['global_initializer_cursor'] == -1,
              'Completed constructor retained initialization phase')
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0,
              'Restart replayed constructor script entry or retained active frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('constructor-list-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Constructor List Production Restart'), encoding='utf-8')
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
                                  (['--create-user', 'ListConstructor', 'Viewer'], 'fixture-password\n')):
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
                    first='ListConstructor', last='Viewer', passwd='fixture-password', start='home',
                    channel='Constructor List production restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    wait_script_chat(viewer, identity, 'list constructor sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'list constructor sleeping', 'Original sleeping entry marker differs')
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'list constructor sleeping', 'Constructor List result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored constructor List callee failed to finish')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            report['processes'].append(observation)
            check_saved(saved, source, cycle)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1,
              'Constructor List restart replaced the task script asset')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual constructor List production persistence: ' + str(root))


if __name__ == '__main__':
    main()
