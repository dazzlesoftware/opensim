"""Verify original zero-Rotation persistence across ordinary entry and three clean servers."""
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


COMPLETED = 'rotation complete:1:1:1'


def original_source():
    root = Path(__file__).with_name('yengine_rotation_persistence_process_fixture') / 'process_rotation_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = (root / 'source.lsl').read_bytes()
    check(source == Path(__file__).with_name('yengine_rotation_persistence_process_source.lsl').read_bytes(),
          'Exact original rotation process source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                                'resume_exception', 'live_resume_exception']),
          'Actual original rotation production migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    live_bits = ['8000000000000000', '0000000000000000', '8000000000000000', '0000000000000000']
    persistent_bits = live_bits[:3] + ['3ff0000000000000']
    for key in ['initial', 'live_resume']:
        check(metadata[key]['globals']['value']['bits'] == live_bits,
              'Original live zero-Rotation component bits differ')
    for key in ['after_load', 'final']:
        check(metadata[key]['globals']['value']['bits'] == persistent_bits,
              'Original migration normalized the wrong Rotation components')
    check(metadata['live_resume']['object_description'] == 'rotation complete:0:1:1' and
          metadata['final']['object_description'] == COMPLETED,
          'Original live versus migrated outcome was conflated')
    for key in ['initial', 'after_load', 'live_resume', 'final']:
        globals_ = metadata[key]['globals']
        check(globals_['calls']['value'] == globals_['entries']['value'] == 1 and
              globals_['done']['value'] == (1 if key in ['live_resume', 'final'] else 0),
              'Original counters or completion stage differ')
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
          'Rotation persistence source failed normal script capability compilation: ' + str(result))
    return identity, item


def check_saved(saved, source, cycle):
    state = saved['decoded']
    check(saved['checkpointVersion'] == 12, 'Ordinary Rotation program changed checkpoint schema')
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == 4,
          'Rotation restart changed program/global identity')
    check(state['globals'] == [[0, 0, 0, 1], 1, 1, 0 if cycle == 0 else 1],
          'Persistent Rotation normalization or no-replay counters differ: ' + repr(state['globals']))
    expected_bits = ['8000000000000000', '0000000000000000', '8000000000000000', '3ff0000000000000']
    check(state['global_rotation_bits'] == [expected_bits, None, None, None],
          'Persistent Rotation signed XYZ or normalized W bits differ')
    if cycle == 0:
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
              state['global_initialization_pending'] is False,
              'Clean stop omitted the actual sleeping event')
    else:
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0,
              'Restart replayed Rotation entry or retained active frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('rotation-persistence-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Rotation Persistence Restart'), encoding='utf-8')
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
                                  (['--create-user', 'RotationPersistence', 'Viewer'], 'fixture-password\n')):
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
                    first='RotationPersistence', last='Viewer', passwd='fixture-password', start='home',
                    channel='Rotation persistence restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    wait_script_chat(viewer, identity, 'rotation zero sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'rotation zero sleeping', 'Original sleeping entry marker differs')
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'rotation zero sleeping', 'Rotation persistence result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored Rotation entry failed to finish')
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
              'Rotation persistence restart replaced the task script asset')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual Rotation production persistence: ' + str(root))


if __name__ == '__main__':
    main()
