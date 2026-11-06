"""Original storage service/base constructors, actual Nini/SDK/assets and SHA1 runtime."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
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
    for f in originals + [w / 'SLUtil.cs', ROOT / 'native-port/tests/asset_factory_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'Nini']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AssetFactory.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    configs = [
        '[AssetService]\nStorageProvider=provider\nConnectionString=asset-connection\nDefaultAssetLoader=loader\nAssetLoaderArgs=assets.xml\n',
        '[AssetService]\nStorageProvider=provider\n',
        '[Other]\nStorageProvider=other\nDefaultAssetLoader=loader\n',
        '[AssetService]\n[DatabaseService]\nStorageProvider=fallback\nConnectionString=fallback-connection\n',
        '[DatabaseService]\nStorageProvider=fallback\n',
        '[AssetService]\nStorageProvider=missing\n',
        '[AssetService]\nStorageProvider=provider\nDefaultAssetLoader=missing\n',
        '[AssetService]\nConnectionString=own\n[DatabaseService]\nStorageProvider=fallback\nConnectionString=fallback-connection\n',
        '[AssetService]\nStorageProvider=provider\nConnectionString=\n[DatabaseService]\nStorageProvider=fallback\nConnectionString=fallback-connection\n',
        '[Other]\nValue=x\n',
    ]
    configs += [
        configs[0] + 'AssetLoaderEnabled=' + v + '\n'
        for v in ['true', 'false', 'TRUE', 'False', 'yes', 'no', 'on', 'off', '1', '0', 'bad', '']
    ]
    for i, content in enumerate(configs):
        (w / f'config-{i}.ini').write_text(content, encoding='utf-8')
    operations = ['I', 'II', 'IRI', 'IGI', 'E', 'JJ', 'NII', 'CII', 'IJIE', 'IIRIG']
    cases = [
        f'{i} {mode} {loader} {ops} -'
        for i, mode, loader, ops in itertools.product(
            range(len(configs)), range(5), range(7), operations
        )
    ]
    failures = [
        'load-store:provider',
        'initialize:asset-connection',
        'load-loader:loader',
        'debug:[ASSET SERVICE]: Loading default asset set from assets.xml',
        'loader:assets.xml',
        'get:00000000-0000-0000-0000-000000000001',
        'hash:0102',
        'hash:0304',
        'store:00000000-0000-0000-0000-000000000001:0102',
        'debug:[ASSET SERVICE]: Local asset service enabled',
    ]
    cases += [
        f'0 {mode} {loader} IIRIG {failure.encode().hex()}'
        for mode, loader, failure in itertools.product(range(5), [0, 2, 6], failures)
    ]
    hashes = [
        b'',
        b'abc',
        b'a' * 55,
        b'a' * 56,
        b'a' * 63,
        b'a' * 64,
        b'a' * 65,
        bytes(range(256)),
        b'a' * 1000000,
    ]
    rng = random.Random(3817)
    hashes += [rng.randbytes(n) for n in range(140)]
    cases += ['h ' + (v.hex() or '.') for v in hashes] + ['h N']
    rows = []
    for command in [[a.dotnet, str(w / 'app/AssetFactory.dll')], [str(Path(a.native).resolve())]]:
        r = subprocess.run(
            command,
            input='\n'.join(cases) + '\n',
            cwd=w,
            capture_output=True,
            text=True,
            timeout=180,
        )
        assert r.returncode == 0, r.stderr[-2000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    differences = [
        {'case': c[:150], 'source': s[:3000], 'native': n[:3000]}
        for c, s, n in zip(cases, *rows)
        if s != n
    ]
    (w / 'asset-factory-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'hashObservations': len(hashes) + 1,
                'sourceHashes': {
                    str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in originals
                },
                'scope': 'Unchanged entire original AssetService and AssetServiceBase constructors with real Nini, AssetBase, SDK and runtime SHA1. Controlled ServiceBase plugin loading, database, default loader and logger/hash hook. Constructor failure/root retention/retry, repeated/custom/null configurations, fallback and loader/hash/store behavior. Full filesystem default-loader/plugin discovery, mutable config/reentry/GC/concurrent/platform semantics remain pending.',
                'differences': differences[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} asset configuration and default loading source observations')


if __name__ == '__main__':
    main()
