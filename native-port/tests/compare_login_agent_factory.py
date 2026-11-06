"""Compile original MakeAgent/SetServiceURLs with actual circuit/appearance models."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_asset_model import reference as asset_reference, ROOT
from compare_presence_endpoint import extract


def canonical(element):
    tag = element.tag.split('}')[-1]
    if tag == 'llsd':
        return canonical(element[0])
    if tag == 'map':
        return {element[i].text or '': canonical(element[i + 1]) for i in range(0, len(element), 2)}
    if tag == 'array':
        return [canonical(item) for item in element]
    text = element.text or ''
    if tag == 'real':
        return ('real', float(text))
    if tag == 'integer':
        return ('integer', int(text))
    if tag == 'boolean':
        return ('boolean', text.lower() in ['1', 'true'])
    if tag == 'binary':
        return ('binary', base64.b64decode(text).hex())
    return (tag, text)


def normalize(line):
    parts = line.split('|')
    assert len(parts) == 6, len(parts)
    for i in [0, 3]:
        if parts[i] != 'EX':
            parts[i] = canonical(ET.fromstring(base64.b64decode(parts[i])))
    return parts


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    work = a.work.resolve()
    asset_reference(a.dotnet, work / 'assets')
    project = ET.parse(work / 'assets/AssetModelReference.csproj')
    group = project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag == 'Compile' and Path(item.get('Include')).name == 'asset_model_reference.cs':
            group.remove(item)
    ET.SubElement(project.getroot().find('PropertyGroup'), 'StartupObject').text = (
        'FactoryReference'
    )
    source = (ROOT / 'OpenSim/Services/LLLoginService/LLLoginService.cs').read_text(
        encoding='utf-8-sig'
    )
    methods = [
        extract(source, r'private [^\n{]+ ' + name + r'\(')
        for name in ['MakeAgent', 'SetServiceURLs']
    ]
    wrapper = work / 'FactoryMethods.cs'
    wrapper.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class FactoryReference {'
        + '\n'.join(methods)
        + '}',
        encoding='utf-8',
    )
    version = (ROOT / 'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig')
    stub = work / 'VersionInfo.cs'
    stub.write_text(
        'namespace OpenSim.Framework {public class VersionInfo {'
        + '\n'.join(
            re.search(r'public readonly static float ' + name + r' = [^;]+;', version).group()
            for name in [
                'SimulationServiceVersionAcceptedMax',
                'SimulationServiceVersionSupportedMax',
            ]
        )
        + '}}',
        encoding='utf-8',
    )
    sources = [
        'AgentCircuitData.cs',
        'AvatarAppearance.cs',
        'AvatarWearable.cs',
        'AvatarAttachment.cs',
        'WearableCacheItem.cs',
        'IAssetCache.cs',
        'EntityTransferContext.cs',
        'Culture.cs',
    ]
    for path in [
        *(ROOT / 'OpenSim/Framework' / s for s in sources),
        ROOT / 'OpenSim/Services/Interfaces/IUserAccountService.cs',
        wrapper,
        stub,
        *(
            ROOT / 'native-port/tests' / s
            for s in ['avatar_appearance_reference.cs', 'login_agent_factory_reference.cs']
        ),
    ]:
        ET.SubElement(group, 'Compile', Include=str(path))
    for name in ['OpenMetaverse', 'OpenMetaverse.StructuredData']:
        item = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(item, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    path = work / 'LoginFactoryReference.csproj'
    project.write(path, encoding='utf-8', xml_declaration=True)
    result = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(work / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    scalar_results = []
    for command in [[a.dotnet, str(work / 'app/LoginFactoryReference.dll')], [a.native]]:
        run = subprocess.run(
            command + ['suffixes'], capture_output=True, text=True, encoding='utf-8', timeout=60
        )
        assert run.returncode == 0, run.stderr
        scalar_results.append(run.stdout.splitlines())
    assert scalar_results[0] == scalar_results[1], 'Service prefix Unicode boundary mismatch'
    results = []
    count = 432
    for command in [[a.dotnet, str(work / 'app/LoginFactoryReference.dll')], [a.native]]:
        run = subprocess.run(
            command,
            input='\n'.join(map(str, range(count))) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=60,
        )
        if run.returncode:
            raise RuntimeError(run.stdout[-500:] + run.stderr)
        rows = run.stdout.splitlines()
        assert len(rows) == count, len(rows)
        results.append([normalize(row) for row in rows])
    differences = [
        {'profile': i, 'source': s, 'native': n} for i, (s, n) in enumerate(zip(*results)) if s != n
    ]
    successes = sum(isinstance(row[i], dict) for row in results[0] for i in [0, 3])
    stores = sum(int(row[4].split(',')[0]) for row in results[0])
    failures = count * 2 - successes
    assert successes and failures and stores, (successes, failures, stores)
    report = {
        'passed': not differences,
        'profiles': count,
        'factoryCalls': count * 2,
        'successfulCalls': successes,
        'exceptionCalls': failures,
        'storeCalls': stores,
        'unicodePrefixBoundaryScalars': 0x110000 - 0x800,
        'methodHashes': {
            name: hashlib.sha256(method.encode()).hexdigest()
            for name, method in zip(['MakeAgent', 'SetServiceURLs'], methods)
        },
        'sourceHash': hashlib.sha256(source.encode()).hexdigest(),
        'modelHashes': {
            name: hashlib.sha256((ROOT / 'OpenSim/Framework' / name).read_bytes()).hexdigest()
            for name in sources
        },
        'accountModelHash': hashlib.sha256(
            (ROOT / 'OpenSim/Services/Interfaces/IUserAccountService.cs').read_bytes()
        ).hexdigest(),
        'libraryHashes': {
            name: hashlib.sha256((ROOT / 'bin' / (name + '.dll')).read_bytes()).hexdigest()
            for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']
        },
        'differences': differences[:5],
    }
    (work / 'login-factory-parity-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    if differences:
        raise AssertionError(json.dumps(differences[:1], indent=2))
    print(f'PASS: {count*2} login agent factory calls')


if __name__ == '__main__':
    main()
