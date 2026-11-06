"""Complete unchanged Scene.UpdateTaskInventory, original task model and SDK flags."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, sig):
    start = s.index(sig)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs'
    model = ROOT / 'OpenSim/Framework/TaskInventoryItem.cs'
    generated = w / 'ExtractedTaskUpdate.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;\npartial class Scene{\n'
        + extract(source.read_text(), 'public void UpdateTaskInventory(')
        + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [generated, model, ROOT / 'native-port/tests/task_inventory_update_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'TaskInventoryUpdateReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = set([0, 1, 2, 4, 16777216, 8388608])
    for bits in itertools.product(
        [0, 8192],
        [0, 16384],
        [0, 32768],
        [0, 65536],
        [0, 131072],
        [0, 4096],
        [0, 524288 | 1048576 | 4194304],
        [0, 262144],
    ):
        modes.add(sum(bits))
    for bits in itertools.product(
        [0, 8],
        [0, 16],
        [0, 32],
        [0, 64],
        [0, 128],
        [0, 256],
        [0, 512],
        [0, 1024],
        [0, 2097152],
        [0, 33554432],
    ):
        modes.add(4 + sum(bits))
    rng = random.Random(4837)
    modes.update(rng.randrange(1 << 26) for _ in range(256))
    commands = [
        ' '.join(map(str, c)) for c in itertools.product(sorted(modes), range(2), range(21))
    ] + [f'{m} {mask} 0' for m in sorted(modes) for mask in range(2, 8)]
    outputs = []
    for cmd in [[a.dotnet, str(w / 'app/TaskInventoryUpdateReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        outputs.append(r.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    files = [source, model, ROOT / 'bin/OpenMetaverse.dll', ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged Scene.UpdateTaskInventory with actual TaskInventoryItem and SDK permission/group flags. Controlled task/user/library lookup, permission/drop/god/group rules, group insertion, optional remote-asset and transaction modules, actual returned-item mutation, inventory update/notifications/removal boundaries, alias identity and injected dependency exceptions. Actual SDK random UUID is normalized to nonzero in add trace; RNG exceptions are not covered. Packet decoder is separately native-tested and uses source-tested FieldToString.',
        differences=diffs[:2],
    )
    (w / 'task-inventory-update-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original task inventory update observations')


if __name__ == '__main__':
    main()
