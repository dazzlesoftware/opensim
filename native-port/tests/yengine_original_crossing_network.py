"""Cross an opaque original suspended script between two actual native processes.

Source import and region crossing use the production SimulationService HTTP
transport; the owner moves the object with authenticated viewer UDP. Filesystem
asset provisioning and original compiled metadata are isolated. SQL is read only.
"""

import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import platform
import shutil
import sqlite3
import struct
import subprocess
import time
from urllib.request import Request, urlopen
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client
import base64

from preview_entry_network import PreviewServer, Viewer, check
from seated_crossing_network import wait_object
from yengine_original_import_restart_network import object_xml, read_saved, sha


def wait_for_ack(viewer, sequence):
    viewer.received.clear()
    viewer.wait(b'', predicate=lambda data: sequence in viewer.acknowledged_sequences)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--fixtures', type=Path, default=Path(__file__).with_name('yengine_managed_frame_fixture'))
    parser.add_argument('--case', choices=('pending_changed', 'pending_sleep'), default='pending_changed')
    args = parser.parse_args()
    reports = [exercise(args, False), exercise(args, True)]
    (args.work.resolve() / 'results.json').write_text(json.dumps(dict(passed=True, cases=reports), indent=2) + '\n',
                                                   encoding='utf-8')


def exercise(args, translated):
    native, probe = args.native.resolve(), args.probe.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('original-crossing-' + uuid.uuid4().hex)
    root.mkdir()
    fixture = args.fixtures.resolve() / args.case
    metadata = json.loads((fixture / 'metadata.json').read_text(encoding='utf-8'))
    if args.case == 'pending_changed':
        original_events = metadata['original_followup_changed_events']
        check(len(original_events) == 2 and all(event['flags'] == 256 and
              event['entry_route'] == 'XMRInstAbstract.CallSEH' for event in original_events),
              'Original handler reference does not prove two changed-event effects')
        # The oracle above directly executes original handlers via CallSEH.
        # Production's two notification causes come separately from original
        # XMRInstCtor.LoadInitialState: each running PrimCrossing admission
        # posts CHANGED_REGION, once on import and once on destination arrival.
        expected = original_events[-1]['globals']
    else:
        expected = metadata['original_roundtrip_resume_globals']
    for name in ('source.lsl', 'snapshot.bin', 'original.yobj', 'cache_key.txt'):
        check(sha(fixture / name) == metadata['sha256'][name], 'Original provenance mismatch: ' + name)
    region_ids = [uuid.uuid4(), uuid.uuid4()]
    object_id, item, asset = (uuid.uuid4() for _ in range(3))
    source_hash = metadata['source_hash']
    snapshot = base64.b64encode((fixture / 'snapshot.bin').read_bytes()).decode('ascii')
    shutil.copyfile(fixture / 'source.lsl', root / 'original-source.lsl')
    control = ET.Element('Nini')
    ET.SubElement(ET.SubElement(control, 'Section', Name='Original'), 'Key', Name='file', Value='original-assets.xml')
    ET.ElementTree(control).write(root / 'asset-sets.xml', encoding='utf-8')
    control = ET.Element('Nini')
    section = ET.SubElement(control, 'Section', Name='Original')
    for name, value in (('assetID', asset), ('assetType', 10), ('name', 'Original crossing source'),
                        ('fileName', 'original-source.lsl')):
        ET.SubElement(section, 'Key', Name=name, Value=str(value))
    ET.ElementTree(control).write(root / 'original-assets.xml', encoding='utf-8')
    commands = []
    cache_roots = [root / 'source-cache', root / 'destination-cache']
    key = (fixture / 'cache_key.txt').read_text().strip() + '.yobj'
    cache_index = 0 if translated else 1
    cache = cache_roots[cache_index] / 'Yengine' / str(region_ids[cache_index]) / key
    cache.parent.mkdir(parents=True)
    shutil.copyfile(fixture / 'original.yobj', cache)
    for index in range(2):
        config = root / f'region-{index}.ini'
        config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableRegionAssets=true
EnableSimulationEndpoint=true
EnableGridEndpoint=true
EnablePresenceEndpoint=true
EnableGridUserEndpoint=true
GridRegionProvider=SQLite
GridRegionDatabase=regions.db
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port=0
[NativeRegion]
ID={region_ids[index]}
Name=Original Crossing {'Source' if index == 0 else 'Destination'}
LocationX={1000 + index}
LocationY=1000
TerrainDatabase=region-{index}.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[PresenceService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=presence.db
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[Terrain]
InitialTerrain=flat
[YEngine]
ScriptEnginesPath={cache_roots[index]}
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
AssetLoaderArgs={root / 'asset-sets.xml'}
''', encoding='utf-8')
        commands.append([str(native), '--config', str(config)])
    servers = [PreviewServer(command, root, root / f'region-{index}.log') for index, command in enumerate(commands)]
    viewer = None
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root), nativeSha256=sha(native),
                  probeSha256=sha(probe), scriptSha256=sha(Path(__file__)), originalFixture=metadata,
                  helperSha256={name: sha(Path(__file__).with_name(name)) for name in
                                ('preview_entry_network.py', 'seated_crossing_network.py',
                                 'yengine_original_import_restart_network.py')},
                  object=str(object_id), item=str(item), asset=str(asset), regions=list(map(str, region_ids)),
                  databaseWritesByFixture=False, realFirestormVerified=False, transport='SimulationService HTTP',
                  mode='translated-native' if translated else 'opaque-original',
                  metadataRegion=str(region_ids[cache_index]), metadataSha256=sha(cache),
                  processes=[])
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Original', 'Crosser'], 'fixture-password\n')):
            setup = subprocess.run(commands[0] + options, cwd=root, input=password, text=True,
                                   capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        urls = []
        for index, server in enumerate(servers):
            urls.append(server.start().rstrip('/'))
            report['processes'].append(dict(region=str(region_ids[index]), pid=server.process.pid,
                                            log=str(root / f'region-{index}.log')))
        login = xmlrpc.client.ServerProxy(urls[0] + '/').login_to_simulator(dict(
            first='Original', last='Crosser', passwd='fixture-password',
            start='uri:Original Crossing Source&128&128&30', channel='Original suspended crossing', version='1.0'))
        check(login.get('login') == 'true', str(login))
        check(login['seed_capability'].startswith(urls[0] + '/'), 'Owner did not enter source process')
        viewer = Viewer(login)
        viewer.connect()
        absent_index = 1 - cache_index
        check(not (cache_roots[absent_index] / 'Yengine' / str(region_ids[absent_index]) / key).exists(),
              'Negative metadata placement check failed')
        if translated:
            servers[0].process.stdin.write('debug scene set scripting false\n')
            servers[0].process.stdin.flush()
            deadline = time.monotonic() + 5
            while not any('Scene scripting disabled' in line for line in servers[0].log):
                check(time.monotonic() < deadline, 'Source did not acknowledge scripting pause')
                time.sleep(.01)
        xml = object_xml(object_id, item, asset, login['agent_id'], source_hash, snapshot)
        (root / 'import.xml').write_text(xml, encoding='utf-8')
        body = json.dumps(dict(destination_uuid=str(region_ids[0]), sog=xml, new_position=[100, 100, 30])).encode()
        request = Request(f'{urls[0]}/object/{object_id}/', body, {'Content-Type': 'application/json'})
        with urlopen(request, timeout=10) as response:
            check(response.read() == b'True', 'Original object import was rejected')
        imported = wait_object(viewer, object_id)
        check(imported['parent'] == 0, 'Original imported root is unexpectedly parented')
        if translated:
            # SetScriptRunning Low245: authenticated AgentData, ObjectID,
            # ItemID, BOOL. The static compiler policy remains enabled while
            # scene execution is paused, so owner Start translates the frames.
            viewer.send(bytes.fromhex('ffff00f5') + viewer.credentials() + object_id.bytes + item.bytes + b'\x01')
            wait_for_ack(viewer, viewer.sequence)
            time.sleep(.15)
        viewer.received.clear()
        crossing_started = time.monotonic()
        viewer.send(bytes.fromhex('ff04') + viewer.credentials() + b'\x01' +
                    struct.pack('<I3f', imported['local'], 260, 100, 30))
        wait_for_ack(viewer, viewer.sequence)
        # Neighbor crossings deliberately remove the source group silently;
        # an observer with a neighbor view need not receive KillObject. Source
        # shutdown joins its in-flight handoff while destination remains alive.
        # The saved source absence and destination identity/pose/state below
        # positively establish successful transfer rather than packet timing.
        time.sleep(.25)
        for server in servers:
            server.stop()
        report['crossingToCleanExitSeconds'] = time.monotonic() - crossing_started
        check(report['crossingToCleanExitSeconds'] < 2.,
              'Crossing proof exceeded original sleep duration; fresh-entry replay cannot be excluded')
        with contextlib.closing(sqlite3.connect(f'file:{(root / "region-0.db").as_posix()}?mode=ro', uri=True)) as database:
            check(database.execute('SELECT UUID FROM prims WHERE UUID=?', (str(object_id),)).fetchone() is None,
                  'Accepted crossing left the source object in persistent storage')
        payload = read_saved(root / 'region-1.db', region_ids[1], object_id, item, asset)
        with contextlib.closing(sqlite3.connect(f'file:{(root / "region-1.db").as_posix()}?mode=ro', uri=True)) as database:
            position = database.execute('SELECT GroupPositionX,GroupPositionY,GroupPositionZ FROM prims WHERE UUID=?',
                                        (str(object_id),)).fetchone()
            check(position and all(abs(float(a) - b) < .001 for a, b in zip(position, (4, 100, 30))),
                  'Destination did not preserve crossing-local object coordinates')
            report['destinationPosition'] = list(position)
        state = root / 'destination-checkpoint.bin'
        state.write_bytes(payload)
        check(payload.startswith(b'LSC'), 'Destination did not translate the raw original state')
        decoded = subprocess.run([str(probe), '--checkpoint', str(state)], capture_output=True, text=True, timeout=15)
        check(decoded.returncode == 0, decoded.stdout + decoded.stderr)
        checkpoint = json.loads(decoded.stdout)
        report['destinationCheckpoint'] = checkpoint
        report['destinationCheckpointSha256'] = sha(state)
        check(checkpoint['program_hash'] == hashlib.md5((fixture / 'source.lsl').read_bytes()).hexdigest() and
              checkpoint['state'] == 'default' and checkpoint['initialized_globals'] == 2,
              'Transferred checkpoint no longer belongs to the exact original source')
        check(checkpoint['globals'] == [expected['result'], expected['count']] and checkpoint['running'] and not checkpoint['active_event'] and
              not checkpoint['function'] and checkpoint['call_stack_depth'] == 0 and
              checkpoint['queued_event_count'] == 0 and not checkpoint['sleep_requested'] and
              checkpoint['sleep_remaining'] == 0,
              'Destination lost the original continuation or replayed its entry/API sleep')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        if viewer:
            with contextlib.suppress(OSError):
                viewer.socket.close()
        cleanup_errors = []
        for server in reversed(servers):
            try:
                server.stop()
            except Exception as error:
                cleanup_errors.append(str(error))
        if cleanup_errors:
            report['cleanupErrors'] = cleanup_errors
            report['passed'] = False
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
        if cleanup_errors:
            raise AssertionError('Clean process shutdown failed: ' + '; '.join(cleanup_errors))
    print('PASS: raw original suspended state crosses real HTTP regions and resumes without replay: ' + str(root))
    return report


if __name__ == '__main__':
    main()
