"""Original ubODE character/terrain methods and ODEApi against same actual native engine."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        if source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--library', required=True, type=Path)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    library = a.library.resolve()
    folder = ROOT / 'OpenSim/Region/PhysicsModules/ubOde'
    scene = folder / 'ODEScene.cs'
    character = folder / 'ODECharacter.cs'
    api = folder / 'ODEApi.cs'
    s = scene.read_text()
    c = character.read_text()
    methods = [
        'public OdeCharacter(uint localID',
        'private void AvatarGeomAndBodyCreate()',
        'private void AvatarGeomAndBodyDestroy()',
        'public void Move()',
        'public static void SetSmooth(',
        'private void changeTargetVelocity(',
        'public override void AvatarJump(',
        'private void setFreeMove()',
        'private void changeForce(',
        'public override Quaternion Orientation',
        'private void changeOrientation(',
        'public override bool IsColliding',
    ]
    terrain = [
        'public unsafe float GetTerrainHeightAtXY(',
        'public unsafe Vector3 GetTerrainNormalAtXY(',
        'private void InitTerrain()',
        'public override void SetTerrain(',
    ]
    code = (
        'using System;using System.Runtime.CompilerServices;using System.Runtime.InteropServices;using OpenMetaverse;\nnamespace OpenSim.Region.PhysicsModule.ubOde {\n'
        + extract(s, 'public enum CollisionCategories : uint')
        + '\n'
        + extract(c, 'public enum dParam : int')
        + '\npartial class OdeCharacter {\n'
        + '\n'.join(extract(c, m).replace('public override', 'public') for m in methods)
        + '\n}\npartial class ODEScene {\n'
        + '\n'.join(extract(s, m).replace('public override', 'public') for m in terrain)
        + '\n}\n}\n'
    )
    generated = w / 'ExtractedUbode.cs'
    generated.write_text(code)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        AllowUnsafeBlocks='true',
        EnableDefaultCompileItems='false',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, api, ROOT / 'native-port/tests/ubode_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'UbodeReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    diff = []
    frames = 0
    for mode in (
        list(range(256))
        + [mode | 256 for mode in range(128)]
        + [mode | 512 for mode in range(128)]
        + [mode | 768 for mode in range(128)]
        + [
            1024 | (profile << 11) | base
            for profile in range(8)
            for base in [0, 1, 4, 33, 64, 129, 257]
        ]
    ):
        rows = []
        for cmd in [
            [a.dotnet, str(w / 'app/UbodeReference.dll'), str(library), str(mode)],
            [a.native, str(library), str(mode)],
        ]:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            assert r.returncode == 0, (mode, r.returncode, r.stderr)
            rows.append(r.stdout.splitlines())
        assert len(rows[0]) == len(rows[1]) == 150
        frames += len(rows[0])
        d = [
            dict(mode=mode, frame=i, source=s, native=n)
            for i, (s, n) in enumerate(zip(*rows))
            if s != n
        ]
        diff += d[:2]
        if len(diff) > 6:
            break
    files = [scene, character, api, library, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diff,
        frames=frames,
        sourceHashes={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
        scope='Original character constructor/body/Move/SetSmooth/target/jump/changeForce/setFreeMove /orientation setter/changeOrientation and collision filter property; whole original ODEApi; original terrain init/set/height/normal methods, controlled empty-scene interfaces. Same actual native ubODE solver, fixed source20ms frames. Guarded original SDK normalization rejects zero yaw before its native ODE abort path. Exact normalized/native quaternion, rejection and published/actual positions, observed/actual velocities, acceleration, controller force and collision flags. Scene scheduler clocks, control-to-target mapping, prim/avatar/mesh contacts, raycasts, hover/buoyancy and Unix execution separate.',
        differences=diff[:6],
    )
    (w / 'ubode-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diff, json.dumps(diff[:2], indent=2)
    print(f'PASS: {frames} original ubODE frames against actual native engine')


if __name__ == '__main__':
    main()
