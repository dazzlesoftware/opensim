"""Original Flotsam count/stamp/touch methods with controlled I/O and mutation."""

import argparse, hashlib, itertools, json, random, subprocess, tempfile, os, stat, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, rows=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr)
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
    text = source.read_text(encoding='utf-8-sig')
    generated = w / 'ExtractedMaintenance.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using File=FakeFile;using Directory=FakeDirectory;using Path=FakePath;using DateTime=FakeDate;using Thread=FakeThread;partial class FlotsamAssetCache{'
        + ''.join(
            extract(text, s)
            for s in [
                'private int GetFileCacheCount(',
                'private void StampRegionStatusFile(',
                'private int TouchAllSceneAssets(',
                'private void ClearFileCache(',
            ]
        )
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/flotsam_cache_maintenance_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'MaintenanceReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/MaintenanceReference.dll')]

    def row(
        op,
        profile,
        point=0,
        nth=1,
        cancel=0,
        cancel_nth=1,
        mutation=0,
        mut_point=0,
        mut_nth=1,
        get=1,
        running=1,
    ):
        return ' '.join(
            map(
                str,
                [
                    op,
                    profile,
                    point,
                    nth,
                    cancel,
                    cancel_nth,
                    mutation,
                    mut_point,
                    mut_nth,
                    get,
                    running,
                ],
            )
        )

    commands = [row(*args) for args in itertools.product(range(3), range(9), range(16), [1, 2, 3])]
    commands += [
        row(op, profile, cancel=point, cancel_nth=nth, get=get, running=running)
        for op, profile, point, nth, get, running in itertools.product(
            range(3), range(9), range(1, 16), [1, 2], range(2), range(2)
        )
    ]
    commands += [
        row(2, profile, mutation=m, mut_point=point, mut_nth=nth, get=get)
        for profile, m, point, nth, get in itertools.product(
            [0, 1, 4, 5, 6], range(1, 5), range(9, 16), [1, 2, 3], range(2)
        )
    ]
    commands += [
        row(3, profile, point, nth)
        for profile, point, nth in itertools.product(range(9), range(19), [1, 2, 3])
    ]
    outputs = [invoke(managed, commands), invoke([a.native, '--probe'], commands)]
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    actual = Path(tempfile.mkdtemp(prefix='actual-', dir=w))
    filesystem = []
    for mode in [
        'new',
        'existing',
        'readonly',
        'missing-parent',
        'status-directory',
        'unicode',
        'wide-tree',
    ] + (['locked'] if os.name == 'nt' else []):
        observations = []
        for index, cmd in enumerate([managed, [a.native]]):
            root = actual / (mode + '-' + str(index))
            root.mkdir()
            if mode == 'unicode':
                root = root / 'é漢😀'
                root.mkdir()
            if mode == 'missing-parent':
                root = root / 'missing' / 'child'
            status = root / 'RegionStatus_00000000-0000-0000-0000-000000000001.fac'
            if mode in ['existing', 'readonly', 'locked']:
                status.write_bytes(b'keep\x00bytes')
                os.utime(status, ns=(1730000000000000000, 1730000000000000000))
            if mode == 'readonly':
                status.chmod(stat.S_IREAD)
            if mode == 'status-directory':
                status.mkdir()
            if mode == 'wide-tree':
                for n in range(55):
                    d = root / f'd{n}'
                    d.mkdir()
                    (d / 'asset').write_bytes(b'asset')
            result = invoke(
                cmd + ['--fs', str(root).encode().hex(), ('1' if mode == 'locked' else '0')]
            )
            normalized = [
                line.replace(str(root), 'ROOT').replace(str(root).encode().hex(), b'ROOT'.hex())
                for line in result
            ]
            # Warning messages are hex encoded; normalize only the distinct temporary root.
            fields = normalized[0].split('|')
            fields[-1] = (
                bytes.fromhex(fields[-1]).decode().replace(str(root), 'ROOT').encode().hex()
            )
            observations.append('|'.join(fields))
        filesystem.append(dict(mode=mode, observations=observations))
        assert observations[0] == observations[1], filesystem[-1]
    report = dict(
        passed=not diffs,
        actualFilesystemCases=len(filesystem),
        filesystemObservations=filesystem,
        cases=len(commands),
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Unchanged original count/status/touch/console-clear methods. Controlled I/O, UTC clock instant, logs, ordered dictionary, scene gather/access/service/sleep dependencies. Exact traces/partial state cover per-entry clear failure/continuation and escaped enumeration/warning errors, recursion errors and integer wrap, throttling, cache hits/misses, current-id copying across dictionary mutation, cancellation and exception boundaries. Actual status-file bytes, timestamps, count and warnings are also compared for native filesystem profiles; full production hosting and remaining platform/permission/race behavior remain unverified.',
        differences=diffs[:5],
    )
    (w / 'flotsam-cache-maintenance-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:3], indent=2)
    print(f'PASS: {len(commands)} original maintenance observations')


if __name__ == '__main__':
    main()
