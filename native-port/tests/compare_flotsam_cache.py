"""Original combined Flotsam cache and IAssetService methods with real models/timers."""

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
    generated = w / 'ExtractedCache.cs'
    signatures = [
        'private void UpdateWeakReference(',
        'private void UpdateMemoryCache(',
        'public void Cache(',
        'public void CacheNegative(',
        'private AssetBase GetFromWeakReference(',
        'private AssetBase GetFromMemoryCache(',
        'private bool CheckFromMemoryCache(',
        'public AssetBase Get(string id)',
        'public AssetBase Get(string id, string ForeignAssetService, bool dummy)',
        'public bool Get(string id, out AssetBase asset)',
        'public bool GetFromMemory(',
        'public bool Check(',
        'public AssetBase GetCached(',
        'public void Expire(',
        'public void Clear(',
        'public AssetMetadata GetMetadata(',
        'public byte[] GetData(',
        'public bool Get(string id, object sender, AssetRetrieved handler)',
        'public void Get(string id, string ForeignAssetService, bool StoreOnLocalGrid, SimpleAssetRetrieved callBack)',
        'public bool[] AssetsExist(',
        'public string Store(',
        'public bool UpdateContent(',
        'public bool Delete(',
    ]
    generated.write_text(
        'using System;using System.IO;using System.Collections.Generic;using System.Runtime.InteropServices;using OpenMetaverse;using OpenSim.Framework;using File=FakeFile;using Directory=FakeDirectory;partial class FlotsamAssetCache{'
        + ''.join(extract(text, s) for s in signatures)
        + '}',
        encoding='utf-8',
    )
    slutil = ROOT / 'OpenSim/Framework/SLUtil.cs'
    s = slutil.read_text(encoding='utf-8-sig')
    conversion = w / 'SLUtil.cs'
    conversion.write_text(s[: s.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8')
    originals = [
        ROOT / 'OpenSim/Framework' / n
        for n in ['AssetBase.cs', 'NrbfReader.cs', 'ExpiringCacheOS.cs', 'ExpiringKey.cs']
    ]
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
        ROOT / 'native-port/tests/flotsam_cache_reference.cs',
    ] + originals:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'CacheReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/CacheReference.dll')]
    commands = []
    for memory, negative, file, touch, seconds, negms, log in itertools.product(
        range(2), range(2), range(2), range(2), [-1, 0.016, 2.9], [120, 3000], [0, 2]
    ):
        commands += [
            f'init {memory} {negative} {file} {touch} {seconds} {negms} {log} 0.75',
            'seed 0',
            'get 0',
            'from 0',
            'cached 0',
            'check 0',
            'negative 0',
            'get 0',
            'cached 0',
            'async 0',
            'cache 0',
            'get 0',
            'meta-mutate 0',
            'data-mutate 0',
            'get 0',
            'drop 0',
            'advance 4000',
            'from 0',
            'get 0',
            'update 0',
            'drop 0',
            'advance 4000',
            'get 0',
            'negative 7',
            'cache 7',
            'get 7',
            'cached 7',
            'check 7',
            'async 7',
            'foreign 0',
            'foreign-async 0',
            'exists 0',
            'update-null 0',
            'data 0',
            'update 6',
            'async 6',
            'async-null 6',
            'store 2',
            'store-null 0',
            'delete 0',
            'get 0',
            'seed 1',
            'seed 2',
            'clear',
            'cached 1',
            'get 2',
            'cached 7',
        ]
    for memory, log, point, op in itertools.product(
        range(2),
        [0, 2],
        range(1, 13),
        ['cache', 'get', 'from', 'cached', 'check', 'expire', 'clear', 'delete', 'async', 'update'],
    ):
        commands += [
            f'init {memory} 1 1 1 .5 3000 {log} 0.75',
            'seed 0',
            'seed 1',
            'seed 2',
            'seed 7',
            'cache 0',
            'negative 0',
            f'fault {point}',
            op + ' 0',
            'fault 0',
            'get 0',
            'cached 0',
            'from 0',
            'check 0',
            'drop 0',
            'advance 4000',
            'get 0',
            'cached 1',
            'cached 2',
        ]
    commands += [
        'init 1 1 1 1 .5 3000 2 0.75',
        'negative 0',
        'negative 6',
        'rename 1',
        'cache 0',
        'get 0',
        'get 6',
        'cached 0',
        'cached 6',
        'fault 1',
        'cache 1',
        'fault 0',
        'get 1',
        'root 0',
        'clear',
        'cached 6',
    ]
    commands += [
        'init 0 1 0 0 .016 3001 0 0.75',
        'negative 0',
        'jump 3001.5',
        'advance 0',
        'get 0',
        'advance 2000',
        'get 0',
    ]
    commands += [
        'init 1 1 1 1 .5 3001 2 0.75',
        'cache 0',
        'advance 4000',
        'fault 2',
        'get 0',
        'from 0',
        'fault 0',
        'drop 0',
        'from 0',
        'get 0',
    ]
    commands += [
        'init 1 1 1 1 .5 3001 2 0.75',
        'seed 0',
        'fault 3',
        'get 0',
        'from 0',
        'cached 0',
        'check 0',
        'fault 0',
        'get 0',
    ]
    commands += [
        'init 1 1 1 1 .5 3001 2 0.75',
        'cache 0',
        'fault 12',
        'async 0',
        'async-null 0',
        'async 1',
        'negative 0',
        'async 0',
        'fault 0',
        'get 0',
    ]
    commands += [
        'init 1 1 1 1 .5 3001 2 0.75',
        'cache 0',
        'fault 1',
        'update 0',
        'cached 0',
        'fault 0',
        'data 0',
        'fault 11',
        'expire 0',
        'fault 0',
        'get 0',
    ]
    for failure in [6, 7, 8, 9, 10, 11]:
        commands += [
            'init 1 1 1 1 .5 3001 2 0.75',
            'cache 0',
            'cache 1',
            'cache 2',
            'cache 7',
            'negative 0',
            f'fault {failure}',
            'clear',
            'fault 0',
            'cached 0',
            'cached 1',
            'cached 2',
            'cached 7',
            'get 0',
        ]
    rng = random.Random(622791)
    for memory, negative, file, touch in itertools.product(range(2), repeat=4):
        commands += [f'init {memory} {negative} {file} {touch} .5001 3001 2 0.75']
        for _ in range(800):
            op = rng.choice(
                [
                    'seed',
                    'cache',
                    'replace',
                    'drop',
                    'mutate',
                    'negative',
                    'get',
                    'from',
                    'asset',
                    'cached',
                    'check',
                    'meta',
                    'meta-mutate',
                    'data',
                    'data-mutate',
                    'async',
                    'async-null',
                    'foreign',
                    'foreign-async',
                    'exists',
                    'store',
                    'store-null',
                    'update',
                    'update-null',
                    'expire',
                    'delete',
                    'clear',
                    'advance',
                    'jump',
                    'fault',
                    'rename',
                    'root',
                    'null',
                ]
            )
            if op in ['advance', 'jump']:
                line = (
                    op
                    + ' '
                    + str(
                        rng.choice(
                            [0, 0.25, 0.5, 0.75, 1, 499.75, 500, 2000, 2000.5, 3001.5, 2147483647]
                        )
                    )
                )
            elif op == 'fault':
                line = op + ' ' + str(rng.randrange(13))
            elif op in ['rename', 'root']:
                line = op + ' ' + str(rng.randrange(2))
            else:
                line = op + ' ' + str(rng.randrange(8))
            commands.append(line)
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
    sources = [source, slutil] + originals + [ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources
        },
        scope='Unchanged original weak/memory/negative/file composition and all public IAssetCache/IAssetService data methods with actual AssetBase/SDK and complete original ExpiringCacheOS/ExpiringKey. Controlled file operations/clock/timers, forced GC boundaries, trace/state/fault ordering, partial mutations, getters/zero/negative/ordinary misses, metadata/data alias mutation, Store ID assignment, callback/null/no-op methods, replace flags, expiry logs and per-directory clear failure handling. Native clear adapter executes actual control flow under controlled directory backend; real queue/NRBF/filesystem integration separately covers disk fallback, update, expiry, root-file retention and callback behavior. Full module/config/lifecycle/scanning/cleanup/commands/default file-profile enablement, arbitrary CLR string/null and filesystem/permission/concurrent/GC schedules and Linux/macOS execution remain pending.',
        differences=diffs[:6],
    )
    (w / 'flotsam-cache-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:3], indent=2)
    print(
        f'PASS: {len(commands)} original combined cache/service traces and actual SDK/expiration containers'
    )


if __name__ == '__main__':
    main()
