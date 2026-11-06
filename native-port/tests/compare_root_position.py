"""Actual MakeRootAgent bounds/terrain/avatar-height fragment with controlled scene callbacks."""

import argparse, hashlib, json, random, struct, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def bits(f):
    return f'{struct.unpack("<I",struct.pack("<f",f))[0]:x}'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/ScenePresence.cs'
    s = source.read_text()
    root = s.index('private bool MakeRootAgent(')
    start = s.index('                if (pos.X < 0f || pos.Y < 0f', root)
    end = s.index('                bool checkPhysics =', start)
    generated = w / 'ExtractedRootPosition.cs'
    generated.write_text(
        'using OpenMetaverse;\npartial class RootPositionReference {public Vector3 Fragment(Vector3 pos){\n'
        + s[start:end]
        + '\nreturn pos;}}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/root_position_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'RootPositionReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = []

    def add(width, height, ah, x, y, z, mode, appearance=1):
        commands.append(
            ' '.join(
                map(str, [width, height, bits(ah), bits(x), bits(y), bits(z), mode, appearance])
            )
        )

    for width, height in [
        (256, 256),
        (32, 48),
        (512, 256),
        (1, 1),
        (0, 0),
        (4294967295, 4294967295),
    ]:
        for ah in [-1.0, -0.0, 0.0, 1.6, 1.9, 2.1, float('inf'), float('nan')]:
            for x in [-1.0, -0.0, 0.0, 0.5, width - 0.5, float(width), float('inf'), float('nan')]:
                for y in [-10.0, -0.0, height - 0.5, float(height), float('-inf')]:
                    for z in [-100.0, 21.0, 21.81, 100.0, float('inf'), float('nan')]:
                        for mode in range(4):
                            add(width, height, ah, x, y, z, mode)
    rng = random.Random(828211)
    for i in range(2000):
        width, height = rng.choice([(256, 256), (32, 48), (512, 256)])
        add(
            width,
            height,
            rng.uniform(-1, 3),
            rng.uniform(-5, width + 5),
            rng.uniform(-5, height + 5),
            rng.uniform(-100, 200),
            i % 4,
            i % 2,
        )
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/RootPositionReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (len(rows[0]), len(rows[1]))
    diff = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    report = dict(
        passed=not diff,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Exact actual MakeRootAgent bounds/GetGroundHeight/avatar-height/raise-only-Z fragment; unchanged source statements with scene/appearance/logger shell and actual SDK Vector3. Float bits plus ground callback coordinates/count, ordinary/edge/NaN/infinity/large unsigned dimensions and absent appearance adapter. Earlier landing policy, sitting/root lifecycle and later filtered physics placement/collision interfaces remain separate; actual Unix execution not claimed.',
        differences=diff[:3],
    )
    (w / 'root-position-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diff, json.dumps(diff[:2], indent=2)
    print(f'PASS: {report["cases"]} exact original root placement positions/backend traces')


if __name__ == '__main__':
    main()
