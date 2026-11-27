"""Import original YEngine state through SimulationService and restart three real processes.

Fixture database access is strictly read-only. Source assets use the production
filesystem loader; original compiled metadata uses the production cache path.
This checks server import/persistence, not a real Firestorm session.
"""

import argparse
import base64
import contextlib
import hashlib
import json
from pathlib import Path
import platform
import shutil
import sqlite3
import subprocess
import time
from urllib.request import Request, urlopen
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from preview_entry_network import PreviewServer, check


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def child(parent, name, value):
    ET.SubElement(parent, name).text = str(value)


def identity(parent, name, value):
    child(ET.SubElement(parent, name), 'UUID', value)


def object_xml(root_id, item, asset, owner, source_hash, snapshot):
    group = ET.Element('SceneObjectGroup')
    part = ET.SubElement(group, 'SceneObjectPart')
    for name, value in (('UUID', root_id), ('OwnerID', owner), ('CreatorID', owner)):
        identity(part, name, value)
    child(part, 'Name', 'Original suspended YEngine import')
    for name, axes in (('GroupPosition', dict(X=100, Y=100, Z=30)),
                       ('OffsetPosition', dict(X=0, Y=0, Z=0)),
                       ('RotationOffset', dict(X=0, Y=0, Z=0, W=1)),
                       ('Scale', dict(X=.5, Y=.5, Z=.5))):
        element = ET.SubElement(part, name)
        for axis, value in axes.items():
            child(element, axis, value)
    for name in ('BaseMask', 'OwnerMask', 'NextOwnerMask'):
        child(part, name, 2147483647)
    shape = ET.SubElement(part, 'Shape')
    child(shape, 'PCode', 9)
    child(shape, 'State', 0)
    child(part, 'InventorySerial', 1)
    task = ET.SubElement(ET.SubElement(part, 'TaskInventory'), 'TaskInventoryItem')
    for name, value in (('ItemID', item), ('AssetID', asset), ('ParentID', root_id),
                        ('ParentPartID', root_id), ('OwnerID', owner), ('CreatorID', owner)):
        identity(task, name, value)
    for name, value in (('InvType', 10), ('Type', 10), ('Name', 'Original continuation'),
                        ('BasePermissions', 2147483647), ('CurrentPermissions', 2147483647),
                        ('NextPermissions', 2147483647)):
        child(task, name, value)
    ET.SubElement(group, 'OtherParts')
    saved = ET.SubElement(ET.SubElement(group, 'GroupScriptStates'), 'SavedScriptState', UUID=str(item))
    wrapper = ET.SubElement(saved, 'State', Engine='YEngine', UUID=str(item), Asset=str(asset))
    state = ET.SubElement(wrapper, 'ScriptState', Engine='YEngine', Asset=str(asset), SourceHash=source_hash)
    child(state, 'Running', 'True')
    child(state, 'Snapshot', snapshot)
    return ET.tostring(group, encoding='unicode')


def read_saved(database, region, root_id, item, asset):
    with contextlib.closing(sqlite3.connect(f'file:{database.as_posix()}?mode=ro', uri=True)) as connection:
        prim = connection.execute('SELECT UUID FROM prims WHERE UUID=?', (str(root_id),)).fetchone()
        row = connection.execute(
            'SELECT asset_id,checkpoint FROM native_script_states WHERE region_id=? AND part_id=? AND item_id=?',
            (str(region), str(root_id), str(item))).fetchone()
        task = connection.execute('SELECT assetID FROM primitems WHERE primID=? AND itemID=?',
                                  (str(root_id), str(item))).fetchone()
    check(prim and row and task, 'Clean shutdown omitted imported object, task or script state')
    check(row[0] == str(asset) and task[0] == str(asset), 'Imported source asset identity changed')
    check(isinstance(row[1], bytes) and row[1], 'Persisted script state is empty or not BLOB')
    return row[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--fixtures', type=Path, default=Path(__file__).with_name('yengine_managed_frame_fixture'))
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    native, probe, fixtures = args.native.resolve(), args.probe.resolve(), args.fixtures.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    root = work / ('original-import-' + uuid.uuid4().hex)
    root.mkdir()
    fixture = fixtures / 'pending_sleep'
    metadata = json.loads((fixture / 'metadata.json').read_text(encoding='utf-8'))
    for name in ('source.lsl', 'snapshot.bin', 'original.yobj', 'cache_key.txt'):
        check(sha(fixture / name) == metadata['sha256'][name], 'Original fixture provenance mismatch: ' + name)
    region, root_id, item, asset = (uuid.uuid4() for _ in range(4))
    source_hash = metadata['source_hash']
    native_program_hash = hashlib.md5((fixture / 'source.lsl').read_bytes()).hexdigest()
    snapshot = base64.b64encode((fixture / 'snapshot.bin').read_bytes()).decode('ascii')
    shutil.copyfile(fixture / 'source.lsl', root / 'original-source.lsl')
    control = ET.Element('Nini')
    ET.SubElement(ET.SubElement(control, 'Section', Name='Original script'), 'Key',
                  Name='file', Value='original-assets.xml')
    ET.ElementTree(control).write(root / 'asset-sets.xml', encoding='utf-8')
    control = ET.Element('Nini')
    section = ET.SubElement(control, 'Section', Name='Original script')
    for name, value in (('assetID', asset), ('assetType', 10), ('name', 'Original source'),
                        ('fileName', 'original-source.lsl')):
        ET.SubElement(section, 'Key', Name=name, Value=str(value))
    ET.ElementTree(control).write(root / 'original-assets.xml', encoding='utf-8')
    cache_root = root / 'original-cache'
    cache = cache_root / 'Yengine' / str(region) / ((fixture / 'cache_key.txt').read_text().strip() + '.yobj')
    config = root / 'region.ini'
    config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableSimulationEndpoint=true
EnableRegionAssets=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
[Network]
http_listener_port=0
[NativeRegion]
ID={region}
Name=Original YEngine Import Restart
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[YEngine]
ScriptEnginesPath={cache_root}
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
    command = [str(native), '--config', str(config)]
    report = dict(passed=False, platform=platform.system(), caseDirectory=str(root),
                  nativeSha256=sha(native), probeSha256=sha(probe), testSha256=sha(Path(__file__)),
                  helperSha256=sha(Path(__file__).with_name('preview_entry_network.py')),
                  originalFixture=metadata, region=str(region), object=str(root_id), item=str(item), asset=str(asset),
                  databaseWritesByFixture=False, realFirestormVerified=False, processes=[])
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Original', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True,
                                   capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(3):
            server = PreviewServer(command, root, root / f'server-{cycle}.log')
            observation = dict(cycle=cycle, log=str(root / f'server-{cycle}.log'))
            report['processes'].append(observation)
            try:
                url = server.start().rstrip('/')
                ready_at = time.monotonic()
                observation['pid'] = server.process.pid
                if cycle == 0:
                    check(not cache.exists(), 'Original metadata was installed before opaque preservation check')
                    login = xmlrpc.client.ServerProxy(url + '/').login_to_simulator(dict(
                        first='Original', last='Viewer', passwd='fixture-password', start='home',
                        channel='Original continuation import', version='1.0'))
                    check(login.get('login') == 'true', str(login))
                    xml = object_xml(root_id, item, asset, login['agent_id'], source_hash, snapshot)
                    (root / 'import.xml').write_text(xml, encoding='utf-8')
                    body = json.dumps(dict(destination_uuid=str(region), sog=xml,
                                           new_position=[100, 100, 30])).encode()
                    request = Request(f'{url}/object/{root_id}/', body, {'Content-Type': 'application/json'})
                    with urlopen(request, timeout=10) as response:
                        check(response.read() == b'True', 'SimulationService rejected original continuation import')
                # Original llSleep has already completed in the captured frame.
                # Main starts polling after LISTENING; a fresh state_entry replay
                # cannot finish its two-second sleep within this observation.
                time.sleep(.5)
            finally:
                server.stop()
            observation['readyToExitSeconds'] = time.monotonic() - ready_at
            if cycle:
                check(observation['readyToExitSeconds'] < 2.,
                      'Process observation exceeded original sleep deadline; no-replay evidence is inconclusive')
            payload = read_saved(root / 'region.db', region, root_id, item, asset)
            saved = root / f'checkpoint-{cycle}.bin'
            saved.write_bytes(payload)
            observation.update(checkpointBytes=len(payload), checkpointSha256=sha(saved))
            if cycle == 0:
                opaque = ET.fromstring(payload)
                check(opaque.tag == 'ScriptState' and opaque.attrib.get('Engine') == 'YEngine' and
                      opaque.attrib.get('Asset') == str(asset) and opaque.attrib.get('SourceHash') == source_hash and
                      opaque.findtext('Snapshot') == snapshot and opaque.findtext('Running') == 'True',
                      'Metadata-free import lost the exact original snapshot/hash/asset/running state')
                observation['opaqueOriginalStatePreserved'] = True
                cache.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(fixture / 'original.yobj', cache)
                report['installedCache'] = dict(path=str(cache), sha256=sha(cache))
            else:
                check(payload.startswith(b'LSC'), 'Original managed state did not translate into native checkpoint')
                result = subprocess.run([str(probe), '--checkpoint', str(saved)], capture_output=True,
                                        text=True, timeout=15)
                check(result.returncode == 0, result.stdout + result.stderr)
                decoded = json.loads(result.stdout)
                observation['decodedCheckpoint'] = decoded
                check(decoded['program_hash'] == native_program_hash and decoded['state'] == 'default' and
                      decoded['initialized_globals'] == 2,
                      'Native continuation no longer belongs to the exact original source program')
                # Probe globals preserve declaration order: result, count.
                check(decoded['globals'] == [8, 111], 'Original continuation replayed or produced wrong globals: ' + str(decoded))
                check(not decoded['active_event'] and not decoded['function'] and decoded['call_stack_depth'] == 0,
                      'Completed original continuation still has active frames')
                check(decoded['running'], 'Translated script unexpectedly stopped')
                check(decoded['queued_event_count'] == 0 and not decoded['sleep_requested'] and
                      decoded['sleep_remaining'] == 0,
                      'Completed original script retained pending events or sleep')
        check(len({p['pid'] for p in report['processes']}) == 3, 'Expected three distinct application processes')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: original import survives opaque save, metadata admission and completed restart: ' + str(root / 'results.json'))


if __name__ == '__main__':
    main()
