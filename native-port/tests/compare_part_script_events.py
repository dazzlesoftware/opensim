"""Unchanged SceneObjectPart event registration, removal and aggregation."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_task_inventory_update import extract

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs'
    group = source.parent / 'SceneObjectGroup.cs'
    s = source.read_text(encoding='utf-8')
    generated = w / 'ExtractedPartScriptEvents.cs'
    generated.write_text(
        'using OpenMetaverse;'
        + extract(group.read_text(encoding='utf-8'), 'public enum scriptEvents')
        + 'partial class Part{'
        + ''.join(
            extract(s, sig)
            for sig in [
                'public void SetScriptEvents(',
                'public void RemoveScriptEvents(',
                'public void aggregateScriptEvents(',
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
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/part_script_events_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'PartScriptEventsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    masks = [
        0,
        1,
        1 << 3,
        1 << 9,
        1 << 16,
        1 << 21,
        1 << 29,
        1 << 30,
        1 << 40,
        (1 << 64) - 1,
        (1 << 14) | (1 << 19) | (1 << 20) | (1 << 24),
        (1 << 9) | (1 << 21),
        (1 << 16) | (1 << 29),
    ]
    commands = [f'{m} {x} {y} 0 0' for m, x, y in itertools.product(range(64), masks, masks)] + [
        f'{m} {x} 0 {action} {failure}'
        for m, x, action, failure in itertools.product(
            [0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 64 | 128, 128 | 256, 256 | 512],
            masks,
            range(4),
            range(1, 8),
        )
    ]
    outputs = []
    for cmd in [[a.dotnet, str(w / 'app/PartScriptEventsReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        outputs.append(r.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, group, ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Complete unchanged SetScriptEvents/RemoveScriptEvents/aggregateScriptEvents, actual scriptEvents enum and SDK PrimFlags. Equal-mask early return, missing key/group/root/scene, stale aggregates, all mask families and high bits, root/child updates, allowed drop, target and optional mailbox effects, dependency exception ordering and callback parent/root mutation. Concurrent CLR dictionary/field schedules and complete root-group physics/event aggregation remain separate.',
        differences=diffs[:2],
    )
    (w / 'part-script-events-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original part script event observations')


if __name__ == '__main__':
    main()
