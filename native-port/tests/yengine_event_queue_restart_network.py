"""Verify original background detector persistence across ordinary entry and three clean servers."""
import argparse
import hashlib
import json
import struct
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


COMPLETED = 'queue arguments complete:1:1:0:1.234568'


def original_source():
    root = Path(__file__).with_name('yengine_event_queue_process_fixture') / 'process_event_queue_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = (root / 'source.lsl').read_bytes()
    check(source == Path(__file__).with_name('yengine_event_queue_process_source.lsl').read_bytes(),
          'Exact original queued-argument source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
              'queue_xml_write_exception', 'queue_xml_restore_exception', 'resume_exception', 'live_resume_exception']),
          'Actual original queue production migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin', 'event-queue.xml']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    check(metadata['initial']['queue_aliases'][0]['same_list'] and
          not metadata['after_load']['queue_aliases'][0]['same_list'], 'Original queue XML alias identity differs')
    check(metadata['final']['object_description'] == COMPLETED, 'Original completed queue marker differs')
    return root, source


def plain_original(node):
    kind = node['type']
    if kind in ('vector', 'rotation'):
        values = [item['value'] for item in node['value']]
        # A subsequent clean production stop applies the independently proven
        # SDK binary Quaternion constructor to every LSL-valued zero rotation.
        if kind == 'rotation' and all(value == 0 for value in values):
            values[3] = 1
        return values
    if kind == 'list':
        return [plain_original(item) for item in node['value']]
    return node['value']


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
          'Event queue persistence source failed normal script capability compilation: ' + str(result))
    return identity, item


def queue_original(node):
    # Queued rotations traverse the SDK string constructor, which retains W0;
    # the separate global-value persistent binary policy does not apply here.
    if node['type'] in ('vector', 'rotation'):
        return [item['value'] for item in node['value']]
    if node['type'] == 'list':
        return [queue_original(item) for item in node['value']]
    return node['value']


def check_saved(saved, source, cycle):
    state = saved['decoded']
    root = Path(__file__).with_name('yengine_event_queue_process_fixture') / 'process_event_queue_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    expected = metadata['after_load' if cycle == 0 else 'final']['globals']
    names = ['shared', 'result', 'observed_rotation', 'entries', 'complete', 'returned_w']
    check(saved['checkpointVersion'] == 12 and state['program_hash'] == hashlib.md5(source).hexdigest() and
          state['initialized_globals'] == len(names), 'Saved queue program/schema identity differs')
    check(state['globals'] == [plain_original(expected[name]) for name in names],
          'Persistent globals differ from actual original queue migration: ' + repr(state['globals']))
    check(state['global_rotation_bits'][2] == ['0000000000000000'] * 3 + ['3ff0000000000000'],
          'Subsequent persistent LSL zero Rotation constructor differs')
    if cycle == 0:
        original_event = metadata['after_load']['queue'][0]
        queue = state['detector_state']['queued']
        check(len(queue) == 1 and queue[0]['event'] == original_event['name'] and
              queue[0]['arguments'] == [queue_original(item) for item in original_event['arguments']],
              'Queued SDK number/vector/zero Rotation precision differs from original XML reload')
        check(queue[0]['argument_kinds'] == [6, 7, 8, 3, 4, 5, 5],
              'Queued SDK wrappers or nested-list kinds differ')
        expected_aliases = [dict(left=int(pair['left'].split('/')[-1]),
                                 right=int(pair['right'].split('/')[-1]), same_list=pair['same_list'])
                            for pair in metadata['after_load']['queue_aliases']]
        check(queue[0]['list_aliases'] == expected_aliases,
              'XML reload did not reconstruct separately serialized nested-list arguments')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
              state['queued_event_count'] == 1, 'First clean stop did not preserve the queued event and sleep')
    else:
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0, 'Completed restart retained/replayed queued execution')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('event-queue-persistence-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=Event Queue Persistence Restart'), encoding='utf-8')
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
                                  (['--create-user', 'EventQueuePersistence', 'Viewer'], 'fixture-password\n')):
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
                    first='EventQueuePersistence', last='Viewer', passwd='fixture-password', start='home',
                    channel='Event queue persistence restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    deadline = time.monotonic() + 5
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'queue arguments sleeping':
                            break
                        check(time.monotonic() < deadline, 'Actual queue sleep marker missing')
                        time.sleep(.02)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'queue arguments sleeping', 'Event queue persistence result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored queued caller failed to finish')
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
              'Event queue persistence restart replaced the task script asset')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual event queue argument production persistence: ' + str(root))


if __name__ == '__main__':
    main()
