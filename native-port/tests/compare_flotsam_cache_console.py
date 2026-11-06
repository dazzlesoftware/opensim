"""Original complete Flotsam console command orchestration with controlled providers."""

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
    generated = w / 'ExtractedFlotsamCacheConsole.cs'
    generated.write_text(
        'using System;using DateTime=FakeDate;using Thread=FakeThread;using File=FakeFile;using Directory=FakeDirectory;partial class FlotsamAssetCache{'
        + extract(source.read_text(encoding='utf-8-sig'), 'private void HandleConsoleCommand(')
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
    for f in [generated, ROOT / 'native-port/tests/flotsam_cache_console_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = w / 'FlotsamCacheConsoleReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/FlotsamCacheConsoleReference.dll')]

    def call(*args):
        return (
            'call '
            + str(len(args))
            + ''.join(' ' + ('~' if s is None else s.encode().hex()) for s in args)
        )

    operations = [
        call(),
        call('fcache'),
        call('fcache', 'status'),
        call('fcache', 'clear'),
        call('fcache', 'clear', 'file'),
        call('fcache', 'clear', 'memory'),
        call('fcache', 'clear', 'FILE', 'MeMoRy'),
        call('fcache', 'clear', None),
        call('fcache', 'clearnegatives'),
        call('fcache', 'assets'),
        call('fcache', 'expire'),
        call('fcache', 'expire', None),
        call('fcache', 'expire', 'now'),
        call('fcache', 'expire', 'NOW'),
        call('fcache', 'expire', '2024-01-01'),
        call('fcache', 'expire', '2025-01-01'),
        call('fcache', 'expire', '2030-01-01'),
        call('fcache', 'expire', 'bad'),
        call('fcache', 'expire', '12/31/2024', '23:59:59'),
        call('fcache', 'cachedefaultassets'),
        call('fcache', 'deletedefaultassets'),
        call('fcache', 'WHAT'),
        call('fcache', None),
        'call -1',
    ]
    commands = []
    for flags, timer, cleanup, operation, dispatch in itertools.product(
        range(8), range(2), range(2), operations, range(2)
    ):
        commands += [
            f'reset {flags} {timer} {cleanup}',
            f'dispatch {dispatch}',
            operation,
            'drain 0',
        ]
    for point, nth, operation, timer in itertools.product(
        range(1, 23), [1, 2, 3, 5, 7], operations, range(2)
    ):
        commands += [
            f'reset 7 {timer} 0',
            f'fail {point} {nth}',
            operation,
            'drain 0',
            'fail 0 1',
            'drain 0',
        ]
    for point, nth, mutation, operation in itertools.product(
        range(1, 23),
        [1, 2],
        range(1, 7),
        [operations[2], operations[3], operations[9], operations[12], operations[14]],
    ):
        commands += ['reset 7 1 0', f'mutate {point} {nth} {mutation}', operation, 'drain 0']
    for mode, operation in itertools.product(range(6), [operations[2], operations[3]]):
        commands += ['reset 7 1 0', f'files {mode}', operation, 'drain 0']
    for n, operation in itertools.product(range(2), operations):
        commands += [
            'reset 7 1 0',
            f'console {n}',
            operation,
            'drain 0',
            'reset 7 1 0',
            f'timer {n}',
            operation,
            'drain 0',
        ]
    rng = random.Random(913558)
    for _ in range(60):
        commands += [f'reset {rng.randrange(8)} {rng.randrange(2)} 0']
        for _ in range(80):
            op = rng.randrange(8)
            commands.append(
                rng.choice(operations)
                if op < 3
                else (
                    'drain ' + str(rng.randrange(3))
                    if op == 3
                    else (
                        f'flags {rng.randrange(8)}'
                        if op == 4
                        else (
                            f'life {rng.randrange(2)} {rng.randrange(2)}'
                            if op == 5
                            else (
                                f'fail {rng.randrange(23)} {rng.randrange(1,8)}'
                                if op == 6
                                else f'files {rng.randrange(6)}'
                            )
                        )
                    )
                )
            )
    for flags, at, current, later in itertools.product(
        range(8), range(1, 10), ['garbage', 'memory', None], ['memory', 'file', None]
    ):
        commands += [
            f'reset {flags} 1 0',
            'lower '
            + str(at)
            + ' '
            + ('~' if current is None else current.encode().hex())
            + ' '
            + ('~' if later is None else later.encode().hex()),
            call('fcache', 'clear', 'FILE', 'ignored'),
            'drain 0',
        ]
    outputs = [invoke(managed, commands), invoke([a.native, '--probe'], commands)]
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [
        dict(index=i, command=c, source=s, native=n, context=commands[max(0, i - 6) : i])
        for i, (c, s, n) in enumerate(zip(commands, *outputs))
        if s != n
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Complete unchanged HandleConsoleCommand. Controlled task queue/output/cache/maintenance/timer providers with actual .NET DateTime.TryParse and custom status-date formatting in the oracle. Flags, null arguments/output/timer, deferred and inline work, status marker UTF16 path slicing, fault boundaries, mutations, cancellation/gating and source flag retention compared. Typed non-provider exception class/HResult/parameter/actual-value/message and actual String.ToLower current-culture receiver callbacks that mutate current/later argument slots are compared. Native production provider binding, complete culture/date parsing/case mapping and actual file/world/console host integration remain separate obligations; no empty gather or ISO-only parser is supplied.',
        differences=diffs[:5],
    )
    (w / 'flotsam-cache-console-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:2], indent=2)
    print(f'PASS: {len(commands)} original complete console state/trace observations')


if __name__ == '__main__':
    main()
