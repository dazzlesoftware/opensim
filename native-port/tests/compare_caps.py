"""Complete original Caps class with real CapsHandlers/SDK and controlled listener/event adapters."""

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
    source = ROOT / 'OpenSim/Capabilities/Caps.cs'
    s = (
        source.read_text(encoding='utf-8-sig')
        .replace(
            'private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);',
            'private static readonly global::CapLog m_log = new();',
        )
        .replace(
            'private ManualResetEvent m_capsActive = new(false);',
            'private global::ControlledActivation m_capsActive = new(false);',
        )
    )
    wrapper = w / 'Caps.cs'
    wrapper.write_text(s, encoding='utf-8')
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'StartupObject': 'NativeCapsReference',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        wrapper,
        ROOT / 'OpenSim/Capabilities/CapsHandlers.cs',
        ROOT / 'native-port/tests/caps_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData', 'log4net']:
        r = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'CapsReference.csproj'
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
        ' '.join(map(str, c))
        for c in itertools.product(range(2), range(3), range(16), range(16), range(8))
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/CapsReference.dll')], [a.native]]:
        r = subprocess.run(
            cmd, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=120
        )
        assert r.returncode == 0, r.stdout[-2000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases), (len(rows[0]), len(rows[1]), len(cases))
    diffs = [{'case': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'caps-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original class method bodies; logger field and wait primitive type projections. Actual CapsHandlers/SDK XML. Controlled listener/MainServer/log/event dependencies verify activation deadline120000, partial cleanup, identity/aliasing, live versus captured base URLs, external/poll/listings. Full poll runtime/HTTP seed endpoints, GC finalization/concurrency/invalid UTF16 and world/platform remain pending.',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not diffs, json.dumps(diffs[:2])
    print(f'PASS: {len(cases)} concrete caps source observations')


if __name__ == '__main__':
    main()
