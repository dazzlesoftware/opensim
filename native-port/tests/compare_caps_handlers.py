"""Run the complete original CapsHandlers class with controlled listener adapters."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'StartupObject': 'CapsReference',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    source = ROOT / 'OpenSim/Capabilities/CapsHandlers.cs'
    for f in [source, ROOT / 'native-port/tests/caps_handlers_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData']:
        r = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'Caps.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(cs),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    cases = [
        ' '.join(map(str, c)) for c in itertools.product(range(2), range(32), range(9), range(8))
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/Caps.dll')], [a.native]]:
        r = subprocess.run(
            cmd, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=120
        )
        assert r.returncode == 0, r.stdout[-2000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    diffs = [{'case': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'caps-handlers-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original container, actual SDK LLSD XML writer; controlled HTTP listener interfaces. Unordered simple-handler membership canonicalized. Full BaseHttpServer, concurrent scheduling, invalid UTF16 and seed endpoint remain pending.',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not diffs, json.dumps(diffs[:2])
    print(f'PASS: {len(cases)} capability handler source observations')


if __name__ == '__main__':
    main()
