"""Unchanged Flotsam Initialise, actual bundled Nini getters and required host boundaries."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(command, commands=None):
    r = subprocess.run(
        command,
        input=None if commands is None else '\n'.join(commands) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-4000:], r.stderr[-4000:])
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
    generated = w / 'ExtractedFlotsamCacheConfig.cs'
    generated.write_text(
        'using System;using OpenSim.Framework;using IConfig=Section;using IConfigSource=Source;using Path=FakePath;using TimeSpan=FakeTimeSpan;partial class FlotsamAssetCache{'
        + extract(source.read_text(encoding='utf-8-sig'), 'public void Initialise(')
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        NoWarn='0649;0414',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/flotsam_cache_config_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'FlotsamCacheConfigReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/FlotsamCacheConfigReference.dll')]
    commands = []
    for profile in range(-1, 16):
        commands += [
            'reset',
            f'init 0 {profile}',
            f'init 0 {profile}',
            f'init 1 {profile}',
            'init 0 3',
            'init 1 4',
        ]
    for first, second in itertools.product(range(3, 16), range(3, 16)):
        commands += [
            'reset',
            f'init 0 {first}',
            f'init 1 {second}',
            'init 0 3',
            'init 1 4',
            'init 0 5',
            'init 1 6',
        ]
    for profile, point, nth in itertools.product(range(3, 16), range(1, 15), [1, 2, 3, 5, 7]):
        commands += [
            'reset',
            'init 1 5',
            f'fail {point} {nth}',
            f'init 0 {profile}',
            f'init 1 {profile}',
            'fail 0 1',
            f'init 0 {profile}',
            'init 1 3',
        ]
    rng = random.Random(721159)
    for _ in range(80):
        commands += ['reset']
        for _ in range(80):
            commands.append(
                f'fail {rng.randrange(15)} {rng.randrange(1,8)}'
                if rng.random() < 0.2
                else f'init {rng.randrange(2)} {rng.randrange(-1,16)}'
            )
    outputs = [invoke(managed, commands), invoke([a.native, '--probe'], commands)]
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [
        dict(index=i, command=c, source=s, native=n, context=commands[max(0, i - 5) : i])
        for i, (c, s, n) in enumerate(zip(commands, *outputs))
        if s != n
    ]
    files = [source, ROOT / 'bin/Nini.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged Initialise with actual bundled Nini typed getters, two instances and original static/per-instance fields. Required path, key/memory construction, log and registration callbacks are controlled; TimeSpan.FromHours/TotalHours use actual .NET BCL in the oracle. Repeated/missing/null/wrong sections, fallback values, clamping/wrap, whitespace, malformed getters, constructor/log/command faults and partial publication compared. This is the configuration orchestration core; native production Nini replacement/path/time-span providers, module-manager command binding, arbitrary numeric/culture behavior and production file hosting are separate work.',
        differences=diffs[:5],
    )
    (w / 'flotsam-cache-config-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:2], indent=2)
    print(f'PASS: {len(commands)} original Initialise/Nini state/trace observations')


if __name__ == '__main__':
    main()
