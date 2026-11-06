"""Unchanged Flotsam region lifecycle with source models and real BCL timer validation."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, commands=None):
    r = subprocess.run(
        cmd,
        input=None if commands is None else '\n'.join(commands) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs'
    text = source.read_text(encoding='utf-8')
    generated = w / 'ExtractedModule.cs'
    signatures = [
        'public Type ReplaceableInterface',
        'public string Name',
        'public void PostInitialise(',
        'public void Close(',
        'public void AddRegion(',
        'public void RemoveRegion(',
        'public void RegionLoaded(',
        'private void HandleLoadDefaultAssets(',
        'private void HandleDeleteDefaultAssets(',
    ]
    generated.write_text(
        'using System;using System.Collections.Generic;using OpenSim.Framework;partial class FlotsamAssetCache{'
        + ''.join(extract(text, s) for s in signatures)
        + '}',
        encoding='utf-8',
    )
    slutil = ROOT / 'OpenSim/Framework/SLUtil.cs'
    s = slutil.read_text(encoding='utf-8-sig')
    conversion = w / 'SLUtil.cs'
    conversion.write_text(s[: s.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8')
    originals = [ROOT / 'OpenSim/Framework' / n for n in ['AssetBase.cs', 'NrbfReader.cs']]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        AllowUnsafeBlocks='true',
        NoWarn='0436;0649;0169;0414',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        generated,
        conversion,
        ROOT / 'native-port/tests/flotsam_cache_module_reference.cs',
    ] + originals:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'CacheModuleReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/CacheModuleReference.dll')]
    commands = []
    for enabled, file, loader, expiration, cleanup in itertools.product(
        range(2),
        range(2),
        range(6),
        [-1, 0, 1, 1728000000000],
        [-1, 0, 1, 10000, 36000000000, 21474836470000, 21474836470001, 9223372036854775807],
    ):
        commands += [
            f'init {enabled} {file} {loader} 1 1 4 {expiration} {cleanup}',
            'meta 0',
            'post 0',
            'close 0',
            'add 0 -1',
            'add 0 0',
            'add 0 0',
            'add 0 1',
            'load 0 0',
            'load 0 1',
            'load 0 2',
            'close 0',
            'cleanup 0 1',
            'remove 0 2',
            'remove 0 0',
            'remove 0 0',
            'remove 0 1',
            'close 0',
            'load 0 -1',
            'add 1 1',
            'load 1 1',
            'close 0',
            'remove 1 1',
            'close 1',
        ]
    for point, nth in itertools.product(range(1, 17), [1, 2]):
        commands += [
            'init 1 1 4 1 1 4 1728000000000 500000',
            f'fail {point} {nth}',
            'add 0 0',
            'add 0 0',
            'load 0 0',
            'load 0 0',
            'emit 0',
            'cleanup 0 1',
            'remove 0 0',
            'close 0',
            'remove 0 0',
            'close 0',
            'close 1',
            'fail 0 1',
            'load 0 0',
            'add 1 2',
            'load 1 2',
            'remove 1 2',
            'close 0',
            'close 1',
        ]
    for mode, mutate, file in itertools.product(range(7), range(2), range(2)):
        commands += [
            f'init 1 {file} 4 0 0 0 1728000000000 500000',
            f'loader {mode}',
            f'mutate {mutate}',
            'service 0 0',
            'add 0 0',
            'load 0 0',
            'service 0 1',
            'load 0 0',
            'load 0 2',
            'loader 2',
            'mutate 0',
            'add 0 1',
            'load 0 1',
            'remove 0 0',
            'load 0 1',
            'close 1',
            'load 0 1',
            'emit 0',
            'remove 0 1',
            'load 0 -1',
            'close 0',
        ]
    rng = random.Random(441903)
    for enabled, file, loader in itertools.product(range(2), range(2), [0, 4]):
        commands += [f'init {enabled} {file} {loader} 1 1 4 1728000000000 500000']
        for _ in range(1200):
            op = rng.choice(
                [
                    'add',
                    'remove',
                    'load',
                    'close',
                    'post',
                    'meta',
                    'emit',
                    'service',
                    'cleanup',
                    'loader',
                    'mutate',
                    'fail',
                ]
            )
            if op in ['add', 'remove', 'load']:
                line = f'{op} {rng.randrange(2)} {rng.choice([-1,0,1,2])}'
            elif op in ['close', 'post', 'meta']:
                line = f'{op} {rng.randrange(2)}'
            elif op == 'emit':
                line = f'emit {rng.randrange(5)}'
            elif op == 'service':
                line = f'service {rng.randrange(3)} {rng.randrange(2)}'
            elif op == 'cleanup':
                line = f'cleanup {rng.randrange(2)} {rng.randrange(2)}'
            elif op == 'loader':
                line = f'loader {rng.randrange(7)}'
            elif op == 'mutate':
                line = f'mutate {rng.randrange(2)}'
            else:
                line = f'fail {rng.randrange(17)} {rng.choice([1,2])}'
            commands.append(line)
    previous_cases = len(commands)
    for enabled, file, loader, mode, mutate in itertools.product(
        range(2), range(2), range(6), range(8), range(2)
    ):
        commands += [
            f'init {enabled} {file} {loader} 1 0 4 0 0',
            f'loader {mode}',
            f'mutate {mutate}',
            'defaults 0',
            'late 0',
            'delete-defaults 0',
            'late 0',
            'defaults 1',
            'late 0',
            'delete-defaults 1',
            'late 0',
        ]
    for operation, point, nth, mode, mutate in itertools.product(
        ['defaults', 'delete-defaults'], range(12, 19), [1, 2, 3], range(8), range(2)
    ):
        commands += [
            'init 1 1 4 1 0 4 0 0',
            f'loader {mode}',
            f'mutate {mutate}',
            f'fail {point} {nth}',
            operation + ' 0',
            'late 0',
            'fail 0 1',
            operation + ' 0',
            'late 0',
        ]
    outputs = [invoke(managed, commands), invoke([a.native, '--probe'], commands)]
    assert len(outputs[0]) == len(outputs[1]) == len(commands), (
        len(outputs[0]),
        len(outputs[1]),
        len(commands),
    )
    diffs = [
        dict(index=i, command=c, source=s, native=n, context=commands[max(0, i - 8) : i])
        for i, (c, s, n) in enumerate(zip(commands, *outputs))
        if s != n
    ]
    retained = [invoke(managed + ['--retained']), invoke([a.native, '--retained'])]
    assert retained == [['1:1:0'], ['1:1:0']], retained
    sources = [source, slutil] + originals + [ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        defaultCommandCases=len(commands) - previous_cases,
        retainedCallback=retained,
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources
        },
        scope='Unchanged lifecycle and HandleLoadDefaultAssets/HandleDeleteDefaultAssets methods. Source AssetBase/SDK models and actual .NET8 BCL System.Timers.Timer constructor validation execute under controlled registration/service/loader/cache/worker/timer dependencies. Two instances share worker/default IDs; exact traces and all partial states cover disabled/empty close, duplicates/reference removal, null scenes, retained/null service selection, timer gates/rounding/bounds/property/subscribe/start/stop/close failures, worker create/dispose failures, first region UUID equality, repeated loading, null/empty/throwing default loaders, cache ID mutation and publication only after successful enumeration. Native real one-shot timer/shared executor/NRBF file integration and actual configured default XML cache loading are separately exercised by the native unit. Full initializer/configuration/console, protected scene gathering/cleanup, all module-manager/concurrent/GC schedules and Linux/macOS execution remain pending; production file-backed profile remains unavailable.',
        differences=diffs[:6],
    )
    (w / 'flotsam-cache-module-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:3], indent=2)
    print(
        f'PASS: {len(commands)} original cache lifecycle traces, actual SDK and BCL timer construction'
    )


if __name__ == '__main__':
    main()
