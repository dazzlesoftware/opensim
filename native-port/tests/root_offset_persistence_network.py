"""Verify root OffsetPosition survives two actual application processes without SQL writes."""
import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import platform
import sqlite3
import subprocess
from urllib.request import Request, urlopen
import uuid
import xmlrpc.client

from preview_entry_network import PreviewServer, check


def saved(path, objects):
    observations = []
    with contextlib.closing(sqlite3.connect(f'file:{path.as_posix()}?mode=ro', uri=True)) as database:
        for object_id, group, offset in objects:
            row = database.execute(
                'SELECT GroupPositionX,GroupPositionY,GroupPositionZ,PositionX,PositionY,PositionZ '
                'FROM prims WHERE UUID=?', (str(object_id),)).fetchone()
            check(row is not None, f'Imported root was not saved: {object_id}')
            check(tuple(map(float, row[:3])) == group,
                  f'Root group position changed: {object_id}: {row[:3]} versus {group}')
            check(tuple(map(float, row[3:])) == offset,
                  f'Root offset changed: {object_id}: {row[3:]} versus {offset}')
            observations.append(dict(object=str(object_id), groupPosition=list(row[:3]), rootOffset=list(row[3:])))
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    binary = args.native.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex
    root = work / ('root-offset-' + run_id)
    root.mkdir()
    region = str(uuid.uuid4())
    objects = [(uuid.uuid4(), (100., 100., 30.), (1., 2., 3.)),
               (uuid.uuid4(), (110., 100., 30.), (7., 8., 9.))]
    config = root / 'region.ini'
    config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableSimulationEndpoint=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
[Network]
http_listener_port=0
[NativeRegion]
ID={region}
Name=Root Offset Restart
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
''', encoding='utf-8')
    command = [str(binary), '--config', str(config)]
    report = dict(passed=False, runId=run_id, platform=platform.system(), caseDirectory=str(root),
                  nativeSha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  scriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  helperSha256=hashlib.sha256(Path(__file__).with_name('preview_entry_network.py').read_bytes()).hexdigest(),
                  databaseWritesByFixture=False, realFirestormVerified=False, observations=[])
    try:
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Offset', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password, text=True,
                                   capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(2):
            log = root / f'server-{cycle}.log'
            server = PreviewServer(command, root, log)
            process = dict(cycle=cycle, log=str(log))
            try:
                url = server.start().rstrip('/')
                process['pid'] = server.process.pid
                if cycle == 0:
                    login = xmlrpc.client.ServerProxy(url + '/').login_to_simulator(dict(
                        first='Offset', last='Viewer', passwd='fixture-password', start='home',
                        channel='Root offset restart ' + run_id, version='1.0'))
                    check(login.get('login') == 'true', str(login))
                    for object_id, group, offset in objects:
                        xml = ('<SceneObjectGroup><SceneObjectPart>'
                               f'<UUID><UUID>{object_id}</UUID></UUID>'
                               f'<OwnerID><UUID>{login["agent_id"]}</UUID></OwnerID>'
                               '<Name>Independent root offset</Name>'
                               f'<GroupPosition><X>{group[0]}</X><Y>{group[1]}</Y><Z>{group[2]}</Z></GroupPosition>'
                               f'<OffsetPosition><X>{offset[0]}</X><Y>{offset[1]}</Y><Z>{offset[2]}</Z></OffsetPosition>'
                               '<RotationOffset><X>0</X><Y>0</Y><Z>0</Z><W>1</W></RotationOffset>'
                               '<Scale><X>0.5</X><Y>0.5</Y><Z>0.5</Z></Scale>'
                               '<Shape><PCode>9</PCode><State>0</State></Shape>'
                               '</SceneObjectPart><OtherParts/></SceneObjectGroup>')
                        (root / f'import-{object_id}.xml').write_text(xml, encoding='utf-8')
                        body = json.dumps(dict(destination_uuid=region, sog=xml, new_position=list(group))).encode()
                        request = Request(f'{url}/object/{object_id}/', body, {'Content-Type': 'application/json'})
                        with urlopen(request, timeout=10) as response:
                            check(response.read() == b'True', 'SimulationService rejected root offset fixture')
            finally:
                server.stop()
            process['saved'] = saved(root / 'region.db', objects)
            report['observations'].append(process)
        check(report['observations'][0]['saved'] == report['observations'][1]['saved'],
              'Second process changed independently stored group position or root offset')
        report['passed'] = True
        report['checks'] = [
            'CLI-created fresh account and isolated application databases; no fixture SQL writes',
            'SimulationService imported two roots with independent group and nonzero offset positions',
            'First clean application shutdown persisted GroupPositionXYZ and PositionXYZ independently',
            'Second process loaded and cleanly saved both roots without changing either position']
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (root / 'results.json').write_text(text, encoding='utf-8')
        (work / 'results.json').write_text(text, encoding='utf-8')
    print('PASS: independent root offset survives application restart; evidence: ' + str(root / 'results.json'))


if __name__ == '__main__':
    main()
