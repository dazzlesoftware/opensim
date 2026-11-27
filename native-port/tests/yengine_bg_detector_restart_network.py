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


COMPLETED = 'bg detectors complete:1:1:1:1'


def original_source():
    root = Path(__file__).with_name('yengine_bg_detector_process_fixture') / 'process_bg_detector_restart'
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8'))
    source = (root / 'source.lsl').read_bytes()
    check(source == Path(__file__).with_name('yengine_bg_detector_process_source.lsl').read_bytes(),
          'Exact original background detector process source differs')
    check(metadata['accepted'] and metadata['suspended'] and metadata['migrated'] and
          all(metadata[key] is None for key in ['initial_exception', 'production_write_exception',
                                               'resume_exception', 'live_resume_exception']),
          'Original background detector production migration did not succeed')
    for name in ['source.lsl', 'original.yobj', 'original.il.txt', 'snapshot.bin']:
        check(sha(root / name) == metadata['sha256'][name], 'Original artifact hash differs: ' + name)
    check(metadata['live_resume']['globals']['selected_rotation']['value'][3]['value'] == 0 and
          metadata['final']['globals']['selected_rotation']['value'][3]['value'] == 1,
          'Original live versus binary selected zero Rotation distinction differs')
    check(metadata['final']['object_description'] == COMPLETED, 'Original final marker differs')
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
          'Background detector persistence source failed normal script capability compilation: ' + str(result))
    return identity, item


def detector_bits(detector):
    result = {key: detector[key] for key in
              ['name', 'key', 'owner', 'group', 'type', 'link_number', 'touch_face']}
    result['name_units'] = list(struct.unpack('<' + 'H' * (len(detector['name'].encode('utf-16-le')) // 2),
                                             detector['name'].encode('utf-16-le')))
    for key in ['position', 'velocity', 'grab', 'touch_pos', 'touch_normal',
                'touch_binormal', 'touch_st', 'touch_uv', 'rotation']:
        result[key + '_bits'] = [item['bits'] if isinstance(item, dict) else
                                struct.pack('>d', item).hex() for item in detector[key]]
    return result


def selected_detector(metadata):
    items = metadata['final']['globals']['after']['value']
    names = ['key', 'grab', 'link_number', 'group', 'name', 'owner', 'position',
             'rotation', 'type', 'velocity', 'touch_st', 'touch_normal', 'touch_binormal',
             'touch_pos', 'touch_uv', 'touch_face']
    return {name: items[index + 1]['value'] for index, name in enumerate(names)}


def check_saved(saved, source, cycle):
    state = saved['decoded']
    fixture = Path(__file__).with_name('yengine_bg_detector_process_fixture') / 'process_bg_detector_restart'
    metadata = json.loads((fixture / 'metadata.json').read_text(encoding='utf-8'))
    names = ['selected_rotation', 'queued_rotation', 'root_before', 'before', 'replacement',
             'after', 'result', 'returned', 'entries', 'background', 'resumed', 'complete']
    expected = metadata['after_load' if cycle == 0 else 'final']['globals']
    check(state['program_hash'] == hashlib.md5(source).hexdigest() and state['initialized_globals'] == len(names),
          'Background restart changed program/global identity')
    expected_values = [plain_original(expected[name]) for name in names]
    check(state['globals'] == expected_values,
          'Detector values/counters differ from genuine original: ' + repr(state['globals']))
    expected_rotation_bits = [[component['bits'] for component in expected[name]['value']]
                              for name in ['selected_rotation', 'queued_rotation']]
    check(state['global_rotation_bits'][:2] == expected_rotation_bits,
          'Persistent selected/queued Rotation bits differ from original stage')
    if cycle == 0:
        roles = state['detector_state']
        check(roles['active'] == [detector_bits(item) for item in metadata['after_load']['detectors']],
              'Root XML detector precision/zero Rotation/default surface differs')
        check(roles['queued'] == [dict(event=item['name'], detectors=[detector_bits(d) for d in item['detectors']])
                                  for item in metadata['after_load']['queue']],
              'Queued XML detector precision/default surface differs')
        backgrounds = roles['background_frames']
        check(len(backgrounds) == 1 and backgrounds[0]['event'] == 'touch_start' and
              backgrounds[0]['detectors'] == [detector_bits(selected_detector(metadata))],
              'Binary selected frame lost precision, zero normalization or immutable A identity')
        check(state['sleep_remaining'] > 0 and state['active_event'] == 'state_entry' and
              state['resume_frame_count'] >= 3 and state['event_context_depth'] == 0,
              'Clean stop omitted canonical original background sleep capture')
    else:
        check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
              state['queued_event_count'] == 0 and state['event_context_depth'] == 0 and
              state['resume_frame_count'] == 0,
              'Restart replayed entry or retained active frames')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    original, source = original_source()
    root = work / ('bg-detector-persistence-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    region = uuid.uuid4()
    assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not assets.is_file():
        assets = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(assets.is_file(), 'Bundled assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, assets, 16).replace(
        'Name=Reduced Stack Restart', 'Name=BG Detector Persistence Restart'), encoding='utf-8')
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
                                  (['--create-user', 'BGDetectorPersistence', 'Viewer'], 'fixture-password\n')):
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
                    first='BGDetectorPersistence', last='Viewer', passwd='fixture-password', start='home',
                    channel='BG detector persistence restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    identity, item = upload(viewer, login, source)
                    deadline = time.monotonic() + 5
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'bg detectors sleeping':
                            break
                        check(time.monotonic() < deadline, 'Actual background sleep marker missing')
                        time.sleep(.02)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == COMPLETED:
                            break
                        check(description == 'bg detectors sleeping', 'Background detector persistence result differs: ' + description)
                        check(time.monotonic() < deadline, 'Restored background handler failed to finish')
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
              'Background detector persistence restart replaced the task script asset')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS actual background detector production persistence: ' + str(root))


if __name__ == '__main__':
    main()
