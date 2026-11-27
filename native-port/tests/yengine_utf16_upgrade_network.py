"""Capture an old checkpoint and resume it after the UTF16 implementation upgrade."""
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

SOURCE = b'''YOption chars;
string result;
integer calls;
integer entries;
char checkpoint_anchor = 'A';
string pause() {
    ++calls;
    llSetObjectDesc("upgrade sleeping");
    llOwnerSay("upgrade sleeping");
    llSleep(3.0);
    return "x";
}
default {
    state_entry() {
        ++entries;
        result = "a" + "b" + pause() + "c" + "d";
        llSetObjectDesc(result);
    }
}'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('prepare', 'resume'), required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    manifest = work / 'upgrade.json'
    if args.phase == 'prepare':
        check(not manifest.exists(), 'Prepare evidence already exists; choose a new isolated work directory')
        root = work / ('upgrade-' + uuid.uuid4().hex)
        root.mkdir()
        region = uuid.uuid4()
        assets = native.parent / 'share/opensim-native/assets/AssetSets.xml'
        check(assets.is_file(), 'Copied old runtime assets are missing')
        config = root / 'region.ini'
        config.write_text(configuration(root, region, assets, 16).replace(
            'Name=Reduced Stack Restart', 'Name=UTF16 Upgrade'), encoding='utf-8')
        (root / 'source.lsl').write_bytes(SOURCE)
        report = dict(prepared=False, resumed=False, platform=platform.system(), root=str(root),
                      region=str(region), config=str(config), sourceSha256=hashlib.sha256(SOURCE).hexdigest(),
                      driverSha256=sha(Path(__file__)), oldNativeSha256=sha(native), oldProbeSha256=sha(probe),
                      databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Upgrade', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run([str(native), '--config', str(config)] + options, cwd=root,
                                   input=password, text=True, capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        cycles = [0]
    else:
        report = json.loads(manifest.read_text(encoding='utf-8'))
        check(report['prepared'] and not report['resumed'], 'A clean old checkpoint must be prepared exactly once')
        check(report['sourceSha256'] == hashlib.sha256(SOURCE).hexdigest(), 'Upgrade source changed')
        root, config = Path(report['root']), Path(report['config'])
        region = uuid.UUID(report['region'])
        report.update(newNativeSha256=sha(native), newProbeSha256=sha(probe))
        cycles = [1, 2]
    try:
        for cycle in cycles:
            server = PreviewServer([str(native), '--config', str(config)], root, root / f'server-{cycle}.log')
            viewer = None
            observation = dict(cycle=cycle, phase=args.phase)
            try:
                url = server.start()
                observation['pid'] = server.process.pid
                login = xmlrpc.client.ServerProxy(url).login_to_simulator(dict(
                    first='Upgrade', last='Viewer', passwd='fixture-password', start='home',
                    channel='UTF16 upgrade fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    report.update(object=str(identity), item=str(item))
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity,
                                                                is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], SOURCE,
                                                     {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Old-version source did not compile normally: ' + str(complete))
                    wait_script_chat(viewer, identity, 'upgrade sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'upgrade sleeping', 'Old process did not retain its initial suspension')
                else:
                    identity, item = uuid.UUID(report['object']), uuid.UUID(report['item'])
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'abxcd':
                            break
                        check(description == 'upgrade sleeping', 'Upgraded expression differs: ' + description)
                        check(time.monotonic() < deadline, 'Upgraded continuation did not finish')
                        time.sleep(.05)
                observation['description'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, report['object'], report['item'], probe, cycle)
            observation['saved'] = saved
            report['processes'].append(observation)
            state = saved['decoded']
            check(state['program_hash'] == hashlib.md5(SOURCE).hexdigest() and
                  state['initialized_globals'] == 4 and state['global_character_count'] == 1,
                  'Upgrade lost source/global identity')
            check(state['globals'] == (['', 1, 1, 65] if cycle == 0 else ['abxcd', 1, 1, 65]),
                  'Upgrade replayed entry/callee or lost concatenation journal')
            if cycle == 0:
                check(saved['checkpointVersion'] == 14 and state['sleep_remaining'] > 0 and
                      state['active_event'] == 'state_entry' and state['function'] and
                      state['call_stack_depth'] >= 1,
                      'Old capture did not preserve checkpoint14 suspended caller/callee state')
                report['prepared'] = True
            else:
                check(not state['active_event'] and not state['function'] and state['call_stack_depth'] == 0 and
                      state['queued_event_count'] == 0, 'Upgrade failed to finish the original continuation')
        if args.phase == 'resume':
            check(len({x['saved']['asset'] for x in report['processes']}) == 1, 'Upgrade changed script asset')
            report['resumed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        manifest.write_text(text, encoding='utf-8')
        (root / 'upgrade.json').write_text(text, encoding='utf-8')
    print('PASS ' + args.phase + ': ' + str(root))


if __name__ == '__main__':
    main()
