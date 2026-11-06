"""Whole original local connector, actual Nini, asset/cache/service contracts and SDK."""

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
    conversion = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        conversion[: conversion.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    sources = [
        ROOT / f
        for f in [
            'OpenSim/Region/CoreModules/ServiceConnectorsOut/Asset/LocalAssetServiceConnector.cs',
            'OpenSim/Framework/AssetBase.cs',
            'OpenSim/Framework/NrbfReader.cs',
            'OpenSim/Framework/IAssetCache.cs',
            'OpenSim/Services/Interfaces/IAssetService.cs',
            'OpenSim/Region/Framework/Interfaces/ISharedRegionModule.cs',
            'OpenSim/Region/Framework/Interfaces/IRegionModuleBase.cs',
        ]
    ]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'AllowUnsafeBlocks': 'true',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in sources + [
        w / 'SLUtil.cs',
        ROOT / 'native-port/tests/local_asset_connector_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'Nini']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'LocalAsset.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    configs = {
        'I': '[Modules]\nAssetServices=LocalAssetServicesConnector\n[AssetService]\nLocalServiceModule=provider\n',
        'i': '[Other]\nValue=1\n',
        'E': '[Modules]\nAssetServices=Other\n',
        'M': '[Modules]\nAssetServices=LocalAssetServicesConnector\n',
        'N': '[Modules]\nAssetServices=LocalAssetServicesConnector\n[AssetService]\n',
    }
    for key, value in configs.items():
        (w / f'config-{key}.ini').write_text(value, encoding='utf-8')
    operations = [
        'IARGCTDX',
        'IARBQ',
        'IARSQ',
        'IARbQ',
        'IARsQ',
        'IAROLUGCTDdH',
        'IARWGCTDBSQ',
        'IARBYKPQ',
        'IARSKQ',
        'IARSVIKQ',
        'IARFYRGSQ',
        'iARG',
        'EARG',
        'MARG',
        'NARG',
        'IVIARG',
        'IARNARG',
        'IARiARG',
        'IARRRAAJ',
        'IARGYYGCTD',
        'IARBSPSQ',
        'IARUCG',
        'IARdG',
        'ARBSQ',
        'IARGZCbQsQ',
    ]
    cases = [
        f'{cache} {service} {shared} {ops} -'
        for cache, service, shared, ops in itertools.product(
            range(5), range(6), range(2), operations
        )
    ]
    failures = [
        'load:provider',
        'register',
        'scene.cache',
        'scene.name',
        'get:asset',
        'memory:asset',
        'service.get:asset',
        'service.async:asset:sender',
        'service.async:asset:N',
        'cache:A:0',
        'negative:asset',
        'expire:asset',
        'service.store:A',
        'update:asset:09',
        'exists:2',
        'post:LocalAssetServiceConnector.GotFromServiceCallback',
        'post:LocalAssetServiceConnector.GotFromCacheCallback',
        'callback:asset:sender:A',
        'simple:A',
    ]
    cases += [
        f'{cache} {service} 1 IARGCTDBSQOLUdH {failure}'
        for cache, service, failure in itertools.product(range(5), [0, 1, 4, 5], failures)
    ]
    rng = random.Random(78322)
    for _ in range(240):
        cases.append(
            f'{rng.randrange(5)} {rng.randrange(6)} {rng.randrange(2)} IAR'
            + ''.join(rng.choices('IARGCTDBbSsOLUdHPQKVWYFJ', k=20))
            + ' -'
        )
    rows = []
    for command in [[a.dotnet, str(w / 'app/LocalAsset.dll')], [str(Path(a.native).resolve())]]:
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
    differences = [{'case': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n]
    (w / 'local-asset-connector-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'sources': {
                    str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in sources
                },
                'scope': 'Entire unchanged original connector, actual Nini, AssetBase, cache/service and region module contracts, SDK. Controlled scene, plugin, cache, service, logging and queued FireAndForget boundaries. General managed sender identity/null string inputs, native scheduler/platform/production provider and arbitrary concurrency remain pending.',
                'differences': differences[:5],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} local asset connector source observations')


if __name__ == '__main__':
    main()
