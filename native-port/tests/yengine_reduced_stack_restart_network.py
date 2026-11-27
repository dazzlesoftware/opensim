"""Resume a real saved script after reducing the configured stack size.

Accounts, objects and script upload use normal application entry points. The
only configuration change between processes is YEngine ScriptStackSize. SQL is
read only after clean shutdown; each run retains its isolated evidence.
"""

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


SOURCE = b'''integer entries;
integer calls;
integer result;
integer recur(integer n) {
    ++calls;
    if (n > 0) return recur(n - 1);
    llSetObjectDesc("sleeping:" + (string)entries + ":" + (string)calls);
    llOwnerSay("reduced stack sleeping");
    llSleep(3.0);
    return 7;
}
default {
    state_entry() {
        ++entries;
        llSetObjectDesc("entered:" + (string)entries);
        result = recur(130);
        llSetObjectDesc("done:" + (string)result + ":" + (string)entries + ":" + (string)calls);
        llOwnerSay("reduced stack completed");
    }
}'''


def configuration(root, region, control, stack):
    return f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableRegionAssets=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port=0
[NativeRegion]
ID={region}
Name=Reduced Stack Restart
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
[Terrain]
InitialTerrain=flat
[YEngine]
ScriptStackSize={stack}
[Modules]
AssetServices=LocalAssetServicesConnector
AssetCaching=FlotsamAssetCache
[AssetCache]
FileCacheEnabled=false
MemoryCacheEnabled=true
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{root / 'assets.db'};version=3;Pooling=false
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs={control}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native, probe, work = args.native.resolve(), args.probe.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('reduced-stack-' + uuid.uuid4().hex)
    root.mkdir()
    region = uuid.uuid4()
    control = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Bundled default assets missing')
    config = root / 'region.ini'
    config.write_text(configuration(root, region, control, 64), encoding='utf-8')
    (root / 'source.lsl').write_bytes(SOURCE)
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root), nativeSha256=sha(native),
                  probeSha256=sha(probe), testSha256=sha(Path(__file__)), sourceSha256=sha(root / 'source.lsl'),
                  helperSha256={name: sha(Path(__file__).with_name(name)) for name in
                                ('preview_entry_network.py', 'seated_crossing_network.py',
                                 'lsl_user_function_restart_network.py', 'xmr_event_dequeue_restart_network.py')},
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    identity = item = None
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Stack', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True, capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle, stack in enumerate((64, 16, 16)):
            config.write_text(configuration(root, region, control, stack), encoding='utf-8')
            (root / f'configuration-{cycle}.ini').write_bytes(config.read_bytes())
            server = PreviewServer(command, root, root / f'server-{cycle}.log')
            viewer = None
            observation = dict(cycle=cycle, configuredStackKiB=stack, configurationSha256=sha(config))
            try:
                url = server.start()
                observation['pid'] = server.process.pid
                login = xmlrpc.client.ServerProxy(url).login_to_simulator(dict(
                    first='Stack', last='Viewer', passwd='fixture-password', start='home',
                    channel='Reduced stack restart fixture', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    local, identity = viewer.create_box()
                    caps = post(login['seed_capability'], ['UpdateScriptTask'])
                    item = create_script(viewer, local, identity, caps)
                    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
                    check(upload.get('state') == 'upload', str(upload))
                    request = urllib.request.Request(upload['uploader'], SOURCE, {'Content-Type': 'application/octet-stream'})
                    with urllib.request.urlopen(request, timeout=10) as response:
                        complete = decode(ET.fromstring(response.read()))
                    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True),
                          'Deep recursive source did not compile through viewer capability: ' + str(complete))
                    wait_script_chat(viewer, identity, 'reduced stack sleeping')
                    description = properties_description(viewer, identity)
                    check(description == 'sleeping:1:131', 'First process did not reach the intended deep suspension: ' + description)
                else:
                    deadline = time.monotonic() + 10
                    while True:
                        description = properties_description(viewer, identity)
                        if description == 'done:7:1:131':
                            break
                        check(description == 'sleeping:1:131', 'Reduced-stack restore replayed entry or changed its saved prefix: ' + description)
                        check(time.monotonic() < deadline, 'Valid deep continuation did not unwind under the reduced stack')
                        time.sleep(.05)
                observation['viewerDescription'] = description
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            saved = saved_state(root, region, identity, item, probe, cycle)
            observation['saved'] = saved
            state = saved['decoded']
            check(state['program_hash'] == hashlib.md5(SOURCE).hexdigest(), 'Restart changed the script source identity')
            if cycle == 0:
                check(state['globals'] == [1, 131, 0] and state['call_stack_depth'] >= 130 and
                      state['function'].startswith('recur') and state['sleep_remaining'] > 0,
                      'First clean shutdown omitted the deep native call frames or original counters')
            else:
                check(state['globals'] == [1, 131, 7] and not state['active_event'] and not state['function'] and
                      state['call_stack_depth'] == 0 and not state['event_wait'] and state['event_context_depth'] == 0,
                      'Reduced-stack restart lost its completed result or replayed recursive execution')
            report['processes'].append(observation)
        check(len({entry['saved']['asset'] for entry in report['processes']}) == 1, 'Configuration change replaced the script asset')
        report.update(passed=True, object=str(identity), item=str(item), checks=[
            'Normal fresh CLI account, authenticated login/UDP rez/task creation and script-editor upload',
            'A configured 64 KiB process persists more than 130 recursive frames with entries1/calls131/result0',
            'Only normal INI ScriptStackSize changes to16 before starting a separate process',
            'The valid existing frames unwind to result7 without state_entry replay or new recursion',
            'A third16 KiB process retains that exact result, original asset and one entry side effect'])
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: configured64-to16 KiB clean restart resumes existing frames without entry replay: ' + str(root))


if __name__ == '__main__':
    main()
