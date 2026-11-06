"""Unchanged storage-service methods; constructors/plugin/default-loader pending."""

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
    source = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        source[: source.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    originals = [
        ROOT / f
        for f in [
            'OpenSim/Services/AssetService/AssetService.cs',
            'OpenSim/Services/AssetService/AssetServiceBase.cs',
            'OpenSim/Data/IAssetData.cs',
            'OpenSim/Framework/IPlugin.cs',
            'OpenSim/Framework/IAssetLoader.cs',
            'OpenSim/Framework/AssetBase.cs',
            'OpenSim/Framework/NrbfReader.cs',
            'OpenSim/Services/Interfaces/IAssetService.cs',
        ]
    ]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'AllowUnsafeBlocks': 'true',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in originals + [w / 'SLUtil.cs', ROOT / 'native-port/tests/asset_service_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'Nini']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AssetService.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    ids = [
        '',
        'bad',
        '00000000-0000-0000-0000-000000000000',
        '01234567-89ab-cdef-0123-456789abcdef',
        '{01234567-89ab-cdef-0123-456789abcdef}',
        '0123456789abcdef0123456789abcdef',
        ' 01234567-89ab-cdef-0123-456789abcdef ',
        '01234567-89ab-cdef-0123-456789abcdefx',
    ]
    cases = [
        f'{mode} {exists} {stored} {deleted} {failure} {id.encode().hex() or "."} GCMDARQFSsUXEeTGCMDAS'
        for mode, exists, stored, deleted, failure, id in itertools.product(
            range(4), range(4), range(2), range(2), ['-', 'warn', 'error'], ids
        )
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/AssetService.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=120
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    differences = [{'case': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n]
    (w / 'asset-service-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'sourceHashes': {
                    str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in originals
                },
                'scope': 'Entire unchanged original AssetService/AssetServiceBase compiled; uninitialized service receives a controlled data plugin, so constructor/config/plugin/default-loader/root lifecycle is not verified. Actual SDK UUID and AssetBase; unchanged service method behavior including virtual retrieval, catches, callbacks, store deduplication and no-op overload/update. Logger exception text normalized; general null strings/arrays and provider-null returns, GC/scheduler/concurrency/platform semantics pending.',
                'differences': differences[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} stored asset service source observations')


if __name__ == '__main__':
    main()
