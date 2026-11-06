"""Compile the original appearance class and compare native state and protocol packs."""

import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
from compare_asset_model import reference as asset_reference, ROOT


def cases():
    c = [
        'reset',
        'description',
        'initialized 0',
        'initialized 1',
        'saved-alias',
        'saved-mutate',
        'copy 1 1',
        'saved-alias',
        'map-null',
        'copy-null',
        'scalar 7 40400000 3f000000',
        'unpack empty',
        'unpack null',
        'unpack height',
        'reset',
    ]
    for value in [
        0.0,
        -0.0,
        0.05,
        0.1,
        0.2,
        0.3,
        1.2,
        1.9,
        32.0,
        33.0,
        -1.0,
        float('nan'),
        float('inf'),
        -float('inf'),
    ]:
        bits = struct.unpack('>I', struct.pack('>f', value))[0]
        c += [f'size {bits:x} {bits:x} {bits:x}', 'copy 1 1']
    for count in [0, 1, 217, 218, 219, 255]:
        c += [
            'reset',
            f'visual 0 {count} 17',
            f'visual 0 {count} 17',
            f'visual 0 {count} 18',
            'visual 2 0 0',
            'copy 1 1',
            'reset-appearance',
            'reset-bakes',
        ]
    for index in [-1, 0, 14, 15, 16, 17, 25]:
        c += [
            'reset',
            f'wear {index} 0 2 3',
            f'wear {index} 1 0 0',
            f'wear {index} 2 0 0',
            'clear-wearables',
        ]
    c += ['reset', 'wear 0 0 7 8', 'copy 1 1', 'saved-mutate', 'assets', 'copy 0 1', 'copy 1 0']
    for index in [0, 8, 20, 28, 29, 40, 44]:
        c += ['reset', f'textures 0 {index} 23', f'textures 1 {index} 23', 'copy 1 1']
    for point in [0, 1, 2, 31, 38, 39, 127, 128, 129, 255, 256, -1]:
        c += [
            f'attach {point} 1 0',
            f'attach {point} 1 2',
            f'attach {point} 1 3',
            f'attach {point} 2 4',
            'lookup 1',
            f'remove {point} 1',
            f'attach {point} 0 0',
            'detach 2',
        ]
    c += [
        'clear-attachments',
        'append 8 1 2',
        'append 8 1 3',
        'append 8 2 3',
        'replace 9 1 4',
        'lookup 1',
        'mutate-attachment 1 77',
        'lookup 1',
        'copy 1 1',
        'detach 1',
        'lookup 1',
        'clear-attachments',
    ]
    c += [
        'reset',
        *(f'attach {point} {point} {point+1}' for point in range(30, 40)),
        'notecard 1',
        'notecard 0',
        'pack null',
        'copy 1 1',
        'notecard 1',
    ]
    c += [
        'reset',
        'wear 0 0 1 0',
        'wear 1 0 2 0',
        'copy 1 1',
        'wear 0 0 1 99',
        'wear 1 0 2 98',
        'copy 1 1',
        'wear 0 0 1 0',
        'wear 1 0 2 0',
        'assets',
    ]
    for version in [0.0, 0.5, 0.6, 0.79999995, 0.8, 0.8000001, 1.0, float('nan')]:
        bits = struct.unpack('>I', struct.pack('>f', version))[0]
        c += [
            'reset',
            'wear 16 0 21 22',
            f'can {bits:x}',
            f'pack {bits:x}',
            'cache',
            f'pack {bits:x}',
            'notecard 1',
            'notecard 0',
            'copy 1 1',
            'saved-mutate',
            'unpack packed',
        ]
    c += [
        'reset',
        'cache',
        'copy 1 1',
        'saved-mutate',
        'pack null',
        'unpack empty',
        'unpack bad-wear',
        'reset',
        'unpack extra-only',
        'pack null',
        'textures 2 0 0',
        'pack null',
        'reset',
    ]
    c += [
        'unpack cache-extra',
        'pack 3f4ccccd',
        'visual-null',
        'pack null',
        'copy 1 1',
        'description',
        'reset',
        'initialized 1',
        'visual 0 218 17',
        'saved-alias',
        'visual 0 219 18',
        'saved-alias',
        'wear 16 0 1 2',
        'saved-alias',
        'description',
    ]
    rng = random.Random(88721)
    for _ in range(350):
        op = rng.randrange(12)
        if op < 3:
            c.append(f'attach {rng.randrange(-5,260)} {rng.randrange(7)} {rng.randrange(7)}')
        elif op == 3:
            c.append(f'detach {rng.randrange(7)}')
        elif op == 4:
            c.append(f'lookup {rng.randrange(7)}')
        elif op == 5:
            c.append(f'wear {rng.randrange(20)} 0 {rng.randrange(7)} {rng.randrange(7)}')
        elif op == 6:
            c.append(f'textures 0 {rng.randrange(45)} {rng.randrange(7)}')
        elif op == 7:
            c.append(f'visual 0 {rng.choice([1,218,219])} {rng.randrange(256)}')
        elif op == 8:
            c.append(f'copy {rng.randrange(2)} {rng.randrange(2)}')
        elif op == 9:
            c.append(f'pack {rng.choice(["null","3f19999a","3f4ccccd","3f800000"])}')
        elif op == 10:
            c.append('notecard 1')
        else:
            c.append('reset')
    return c


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    args = p.parse_args()
    work = args.work.resolve()
    asset_reference(args.dotnet, work / 'assets')
    source = (ROOT / 'OpenSim/Framework/AvatarAppearance.cs').read_text(encoding='utf-8-sig')
    expected = re.findall(
        r'^\s*([A-Z_][A-Z_0-9]*)\s*=\s*(\d+)\s*,',
        source[source.index('public enum VPElement') :],
        re.M,
    )
    actual = re.findall(
        r'^\s*([A-Z_][A-Z_0-9]*)\s*=\s*(\d+)\s*,',
        (ROOT / 'native-port/include/opensim/framework/avatar_visual_parameter.hpp').read_text(
            encoding='utf-8'
        ),
        re.M,
    )
    assert actual == expected, 'Visual parameter name/index mismatch'
    project = ET.parse(work / 'assets/AssetModelReference.csproj')
    group = project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag == 'Compile' and Path(item.get('Include')).name == 'asset_model_reference.cs':
            group.remove(item)
    sources = [
        'AvatarAppearance.cs',
        'AvatarWearable.cs',
        'AvatarAttachment.cs',
        'WearableCacheItem.cs',
        'IAssetCache.cs',
        'EntityTransferContext.cs',
    ]
    version = (ROOT / 'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig')
    declarations = [
        re.search(r'public readonly static float ' + name + r' = [^;]+;', version).group()
        for name in ['SimulationServiceVersionAcceptedMax', 'SimulationServiceVersionSupportedMax']
    ]
    stub = work / 'VersionInfo.cs'
    stub.write_text(
        'namespace OpenSim.Framework {public class VersionInfo {' + '\n'.join(declarations) + '}}',
        encoding='utf-8',
    )
    for source in [
        *(ROOT / 'OpenSim/Framework' / s for s in sources),
        stub,
        ROOT / 'native-port/tests/avatar_appearance_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(source))
    for name in ['OpenMetaverse', 'OpenMetaverse.StructuredData']:
        node = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(node, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    path = work / 'AvatarAppearanceReference.csproj'
    project.write(path, encoding='utf-8', xml_declaration=True)
    built = subprocess.run(
        [
            args.dotnet,
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
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    commands = cases()
    results = []
    for command in [[args.dotnet, str(work / 'app/AvatarAppearanceReference.dll')], [args.native]]:
        run = subprocess.run(
            command,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=60,
        )
        if run.returncode:
            raise RuntimeError(run.stdout[-500:] + run.stderr)
        results.append(run.stdout.splitlines())
    if any(len(rows) != len(commands) for rows in results):
        raise AssertionError([len(commands), *map(len, results)])
    differences = [
        {'index': i, 'input': c, 'source': a, 'native': b}
        for i, (c, a, b) in enumerate(zip(commands, *results))
        if a != b
    ]
    coverage = {
        'successfulPacks': sum(row.startswith('OK|{') for row in results[0]),
        'exceptionPaths': sum(row.startswith('EX|') for row in results[0]),
        'extendedWearablePacks': sum('wrbls8=' in row for row in results[0]),
        'binaryTexturePacks': sum('te8=' in row for row in results[0]),
    }
    assert all(coverage.values()), coverage
    report = {
        'passed': not differences,
        'observations': len(commands),
        'visualParameterDeclarations': len(expected),
        'coverage': coverage,
        'sourceHashes': {
            s: hashlib.sha256((ROOT / 'OpenSim/Framework' / s).read_bytes()).hexdigest()
            for s in sources + ['AssetBase.cs', 'NrbfReader.cs', 'SLUtil.cs', 'VersionInfo.cs']
        },
        'libraryHashes': {
            name: hashlib.sha256((ROOT / 'bin' / (name + '.dll')).read_bytes()).hexdigest()
            for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']
        },
        'differences': differences[:10],
    }
    (work / 'avatar-appearance-parity-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    if differences:
        raise AssertionError(json.dumps(differences[:1], indent=2))
    print(f'PASS: {len(commands)} avatar appearance state/pack observations')


if __name__ == '__main__':
    main()
