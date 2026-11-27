"""Import original KFM over SimulationService, then verify a real SQLite restart.

No database writes: CLI initializes the database and account, HTTP transfers the
object, and authenticated viewer packets request its transform. SQLite is opened
read-only after each clean shutdown. Generate the fixture once on a host with the
original compiled simulator and a .NET SDK; reuse --motion-blob on Linux.
"""

import argparse
import base64
import contextlib
import hashlib
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import tempfile
import time
from urllib.request import Request, urlopen
import uuid
import xmlrpc.client
from xml.sax.saxutils import escape

from preview_entry_network import PreviewServer, Viewer, check


REGION = '11111111-1111-4111-8111-111111111111'
OBJECT = uuid.UUID('4c0f076b-43cc-4daa-aaf4-fc44b6c02401')

ORIGINAL_GENERATOR = r'''
using System;
using System.Reflection;
using OpenMetaverse;
using Motion = OpenSim.Region.Framework.Scenes.KeyframeMotion;
class Fixture {
    static void Set(Motion m, string n, object v) => typeof(Motion).GetField(n,
        BindingFlags.Instance | BindingFlags.NonPublic).SetValue(m, v);
    static void Main() {
        var m = new Motion(null, Motion.PlayMode.Forward, Motion.DataFormat.Translation);
        m.SetKeyframes(new[] {new Motion.Keyframe {Position = new Vector3(30,0,0), TimeMS = 60000}});
        Set(m, "m_serializedPosition", new Vector3(120,121,30));
        Set(m, "m_basePosition", new Vector3(120,121,30));
        Set(m, "m_baseRotation", Quaternion.Identity);
        Set(m, "m_running", true);
        Console.WriteLine(Convert.ToBase64String(m.Serialize()));
    }
}
'''


def original_blob(args):
    if args.motion_blob:
        return args.motion_blob.read_bytes()
    check(args.original_bin is not None,
          'Supply --motion-blob, or --original-bin with original compiled DLLs and a .NET SDK')
    with tempfile.TemporaryDirectory(dir=args.work, prefix='original-kfm-generator-') as directory:
        root = Path(directory)
        (root / 'Fixture.cs').write_text(ORIGINAL_GENERATOR, encoding='utf-8')
        references = ''.join(
            f'<Reference Include="{name}"><HintPath>{escape(str(args.original_bin.resolve() / (name + ".dll")))}</HintPath></Reference>'
            for name in ('OpenSim.Region.Framework', 'OpenSim.Framework', 'OpenMetaverseTypes', 'OpenMetaverse'))
        (root / 'Fixture.csproj').write_text(
            '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework>'
            '<OutputType>Exe</OutputType><EnableUnsafeBinaryFormatterSerialization>true</EnableUnsafeBinaryFormatterSerialization>'
            '</PropertyGroup><ItemGroup>' + references + '</ItemGroup></Project>', encoding='utf-8')
        build = subprocess.run([args.dotnet, 'build', str(root / 'Fixture.csproj'), '-o', str(root / 'app'),
                                '--nologo', '-v:q'], capture_output=True, text=True, timeout=90)
        check(build.returncode == 0, build.stdout + build.stderr)
        generated = subprocess.run([args.dotnet, str(root / 'app/Fixture.dll')],
                                   capture_output=True, text=True, timeout=30)
        check(generated.returncode == 0, generated.stdout + generated.stderr)
        return base64.b64decode(generated.stdout.strip(), validate=True)


def sample(viewer, local):
    # A normal cache miss requests a fresh full update, avoiding stale cached
    # packets and the position quantization in ImprovedTerseObjectUpdate.
    viewer.received.clear()
    viewer.send(bytes.fromhex('ff03') + viewer.credentials() + struct.pack('<BBI', 1, 0, local))
    packet = viewer.wait(b'\x0c', OBJECT.bytes,
                         predicate=lambda data: len(data) >= 65 and data[17:33] == OBJECT.bytes)
    return struct.unpack_from('<3f', packet, 53)


def saved(path, probe):
    with contextlib.closing(sqlite3.connect(f'file:{path.as_posix()}?mode=ro', uri=True)) as database:
        row = database.execute('SELECT GroupPositionX,GroupPositionY,GroupPositionZ,KeyframeMotion '
                               'FROM prims WHERE UUID=?', (str(OBJECT),)).fetchone()
    check(row is not None, 'Transferred object was not saved')
    check(isinstance(row[3], bytes) and len(row[3]) > 100, 'Motion was not stored as a SQLite BLOB')
    decoded = subprocess.run([str(probe)], input=json.dumps({'nrbf': row[3].hex()}) + '\n',
                             capture_output=True, text=True, timeout=10)
    check(decoded.returncode == 0, decoded.stdout + decoded.stderr)
    state = json.loads(decoded.stdout)
    check(state is not None and state['running'] and state['current']['total'] == 60000,
          'Saved playback did not retain its running state and original frame duration')
    remaining = state['current']['ms']
    check(55000 < remaining < 60000, f'Unexpected saved playback cursor: {remaining}')
    return dict(position=list(map(float, row[:3])), blob_sha256=hashlib.sha256(row[3]).hexdigest(),
                blob_bytes=len(row[3]), remaining_ms=remaining, total_ms=state['current']['total'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path,
                        help='Built keyframed_motion_probe, for reading the saved NRBF cursor')
    parser.add_argument('--motion-blob', type=Path)
    parser.add_argument('--original-bin', type=Path)
    parser.add_argument('--dotnet', default='dotnet')
    args = parser.parse_args()
    args.native = args.native.resolve()
    args.probe = args.probe.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / 'results.json').unlink(missing_ok=True)
    blob = original_blob(args)
    (args.work / 'original-motion.bin').write_bytes(blob)
    observations = []
    with tempfile.TemporaryDirectory(dir=args.work, prefix='kfm-restart-') as directory:
        root = Path(directory)
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
ID={REGION}
Name=KFM Restart
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
''', encoding='utf-8')
        command = [str(args.native), '--config', str(config)]
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Motion', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password,
                                   text=True, capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        for cycle in range(2):
            server = PreviewServer(command, root, args.work / f'server-{cycle}.log')
            viewer = None
            try:
                url = server.start().rstrip('/')
                login = xmlrpc.client.ServerProxy(url + '/').login_to_simulator(dict(
                    first='Motion', last='Viewer', passwd='fixture-password', start='home',
                    channel='KFM restart acceptance', version='1.0'))
                check(login.get('login') == 'true', str(login))
                viewer = Viewer(login)
                viewer.connect()
                if cycle == 0:
                    owner = login['agent_id']
                    xml = ('<SceneObjectGroup><SceneObjectPart>'
                           f'<UUID><UUID>{OBJECT}</UUID></UUID><OwnerID><UUID>{owner}</UUID></OwnerID>'
                           '<Name>KFM SQLite Restart</Name><GroupPosition><X>120</X><Y>121</Y><Z>30</Z></GroupPosition>'
                           '<RotationOffset><X>0</X><Y>0</Y><Z>0</Z><W>1</W></RotationOffset>'
                           '<Scale><X>0.5</X><Y>0.5</Y><Z>0.5</Z></Scale>'
                           '<Shape><PCode>9</PCode><State>0</State></Shape></SceneObjectPart><OtherParts/>'
                           f'<KeyframeMotion>{base64.b64encode(blob).decode()}</KeyframeMotion></SceneObjectGroup>')
                    body = json.dumps(dict(destination_uuid=REGION, sog=xml,
                                           new_position=[120, 121, 30])).encode()
                    request = Request(f'{url}/object/{OBJECT}/', body, {'Content-Type': 'application/json'})
                    with urlopen(request, timeout=10) as response:
                        check(response.read() == b'True', 'SimulationService rejected the original motion object')
                packet = viewer.wait(b'\x0c', OBJECT.bytes,
                                     predicate=lambda data: len(data) >= 65 and data[17:33] == OBJECT.bytes)
                local = struct.unpack_from('<I', packet, 12)[0]
                first = sample(viewer, local)
                time.sleep(1.2)
                second = sample(viewer, local)
                check(.25 < second[0] - first[0] < 1.2,
                      f'Motion did not advance at expected 0.5 m/s in cycle {cycle}: {first} -> {second}')
                check(abs(second[1] - 121) < .01 and abs(second[2] - 30) < .01,
                      'Motion changed an unrelated coordinate')
                if cycle:
                    check(observations[0]['saved']['position'][0] - .02 <= first[0]
                          < observations[0]['saved']['position'][0] + 1.5,
                          'Restart jumped away from the saved pose')
                observations.append(dict(first=list(first), second=list(second)))
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
            observations[-1]['saved'] = saved(root / 'region.db', args.probe)
        check(observations[0]['saved']['blob_sha256'] != observations[1]['saved']['blob_sha256'],
              'Playback cursor BLOB did not change after restored playback')
        check(observations[1]['saved']['remaining_ms'] < observations[0]['saved']['remaining_ms'] - 500,
              'Restart reset the frame cursor instead of continuing the saved frame')
    result = dict(passed=True, checks=[
        'Fresh databases and account created through native CLI without SQL writes',
        'Original compiled simulator KFM imported through SimulationService HTTP object transfer',
        'Authenticated viewer cache misses observe motion before and after a clean process restart',
        'Clean shutdown stores the object transform and changing KFM cursor as a SQLite BLOB',
        'Restart resumes near the saved transform and continues at the original motion speed'],
        observations=observations, original_blob_sha256=hashlib.sha256(blob).hexdigest(),
        native_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),
        probe_sha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(),
        test_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (args.work / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
