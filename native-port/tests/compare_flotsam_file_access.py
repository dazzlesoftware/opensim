"""Execute unchanged Flotsam timestamp policies and original ExpiringKey timers."""

import argparse, hashlib, itertools, json, os, random, stat, subprocess, tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, lines=None):
    r = subprocess.run(
        cmd,
        input=None if lines is None else '\n'.join(lines) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (cmd, r.stdout, r.stderr)
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
    key = ROOT / 'OpenSim/Framework/ExpiringKey.cs'
    text = source.read_text(encoding='utf-8')
    generated = w / 'ExtractedAccess.cs'
    generated.write_text(
        'using System;using System.IO;using File=FakeFile;partial class FlotsamAssetCache{'
        + ''.join(
            extract(text, s)
            for s in [
                'private static bool CheckUpdateFileLastAccessTime(',
                'private static void UpdateFileLastAccessTime(',
            ]
        )
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false', NoWarn='0436'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, key, ROOT / 'native-port/tests/flotsam_file_access_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = w / 'AccessReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/AccessReference.dll')]
    commands = []
    for enabled, point, kind, op in itertools.product(
        range(2), range(1, 4), range(6), ['check', 'touch', 'note']
    ):
        commands += [
            f'init {enabled} 0',
            f'failure {point} {kind}',
            op + ' 0' + (' 900000' if op == 'note' else ''),
            'failure 0 0',
            'touch 0',
            'check 1',
            'advance 900000',
            'touch 0',
            'advance 300000',
            'touch 1',
        ]
    for start, ttl in itertools.product(
        [0, 0.25, 0.75, 1, 2146583647, 2147483646, 2147483647, 2147483648, 4294967296, -300000],
        [-2147483648, -1, 0, 1, 299999, 300000, 300001, 900000, 2147483647],
    ):
        commands += [
            f'init 1 {start}',
            f'note 0 {ttl}',
            'touch 1',
            'jump 900001',
            'touch 0',
            'pump',
            'touch 0',
            'advance 300000',
            'touch 0',
            'check 2',
        ]
    commands += [
        'init 1 0.75',
        'note 0 300001',
        'jump 300001.5',
        'pump',
        'touch 0',
        'advance 300000.25',
        'touch 0',
    ]
    commands += [
        'init 1 0',
        'touch 0',
        'advance 300000',
        'advance 300000',
        'advance 300000',
        'touch 0',
        'advance 299999',
        'touch 0',
        'advance 1',
        'touch 0',
    ]
    rng = random.Random(981247)
    for enabled in range(2):
        commands += [f'init {enabled} 0']
        for _ in range(7000):
            op = rng.choice(['check', 'touch', 'note', 'advance', 'jump', 'pump', 'failure'])
            if op in ['check', 'touch']:
                line = f'{op} {rng.randrange(3)}'
            elif op == 'note':
                line = f'note {rng.randrange(3)} ' + str(
                    rng.choice([-1, 0, 1, 300000, 900000, 2147483647])
                )
            elif op in ['advance', 'jump']:
                line = (
                    op
                    + ' '
                    + str(
                        rng.choice(
                            [0, 1, 299999, 300000, 300001, 899999, 900000, 900001, 2147483647]
                        )
                    )
                )
            elif op == 'failure':
                line = f'failure {rng.randrange(4)} {rng.randrange(6)}'
            else:
                line = op
            commands.append(line)
    outputs = [invoke(managed, commands), invoke([a.native, '--probe'], commands)]
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    differences = [
        dict(index=i, command=c, source=s, native=n, context=commands[max(0, i - 8) : i])
        for i, (c, s, n) in enumerate(zip(commands, *outputs))
        if s != n
    ]
    real_root = Path(tempfile.mkdtemp(prefix='actual-', dir=w))
    actual = []
    for profile, op, enabled in itertools.product(
        ['file', 'directory', 'missing', 'missing-parent', 'readonly', 'unicode'],
        ['check', 'touch'],
        range(2),
    ):
        observations = []
        for index, cmd in enumerate([managed, [a.native]]):
            folder = real_root / f'{profile}-{op}-{enabled}-{index}'
            folder.mkdir()
            target = folder / ('漢😀 asset' if profile == 'unicode' else 'asset')
            if profile == 'directory':
                target.mkdir()
            elif profile == 'missing-parent':
                target = target / 'child'
            elif profile != 'missing':
                target.write_bytes(b'asset')
            if target.exists():
                os.utime(target, ns=(1000000000000000000, 1100000000000000000))
            if profile == 'readonly':
                target.chmod(stat.S_IREAD)
            before = target.stat() if target.exists() else None
            result = invoke(cmd + ['--real', str(target).encode('utf-8').hex(), op, str(enabled)])
            after = target.stat() if target.exists() else None
            observation = dict(
                result=result,
                exists=after is not None,
                touched=bool(after and after.st_atime_ns != before.st_atime_ns),
                writeTimeUnchanged=not after or after.st_mtime_ns == before.st_mtime_ns,
            )
            assert observation['writeTimeUnchanged'], observation
            observations.append(observation)
        if observations[0] != observations[1]:
            differences.append(
                dict(
                    profile=profile,
                    op=op,
                    enabled=enabled,
                    source=observations[0],
                    native=observations[1],
                )
            )
        actual.append(dict(profile=profile, op=op, enabled=enabled, observations=observations))
    report = dict(
        passed=not differences,
        cases=len(commands),
        actualFileCases=len(actual),
        actualDirectory=str(real_root),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, key]
        },
        scope='Unchanged CheckUpdateFileLastAccessTime and UpdateFileLastAccessTime plus complete original ExpiringKey. Controlled file/clock/timer/entry faults verify null tracker differences, contains/set/add ordering, missing-file versus all other exceptions, 900000ms TTL with300000ms periodic strict purge, no lazy read expiration, reset/refresh and unchecked Int32 deadlines. Actual original/native filesystem timestamps compared for files/directories/missing parents/read-only/Unicode with write-time preservation. Shared module lifecycle, cache composition/scanning/cleanup, all filesystem/permission/concurrent/GC schedules and Linux/macOS execution remain pending.',
        differences=differences[:5],
        fileObservations=actual,
    )
    (w / 'flotsam-file-access-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:3], indent=2)
    print(
        f'PASS: {len(commands)} original timestamp/tracker observations and {len(actual)} actual filesystem cases'
    )


if __name__ == '__main__':
    main()
