"""Unchanged original RemoveTaskInventory scene control flow with live task model."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
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
    generated = w / 'ExtractedTaskRemove.cs'
    generated.write_text(
        'using System;using OpenMetaverse;using OpenSim.Framework;using SceneObjectGroup=Group;\npartial class Scene{\n'
        + extract(source.read_text(), 'public void RemoveTaskInventory(')
        + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [generated, model, ROOT / 'native-port/tests/task_inventory_remove_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'TaskInventoryRemoveReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = list(range(512)) + [512, 512 | 32, 1024, 2048, 4096, 4096 | 128, 4096 | 256]
    commands = [' '.join(map(str, c)) for c in itertools.product(modes, range(14))]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/TaskInventoryRemoveReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    files = [source, model, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged original Scene.RemoveTaskInventory with original full TaskInventoryItem and actual SDK enums/identity. Controlled part/group/permissions/Trash lookup/real-move boundary/script events/count/registry removal/properties/logger/alert dependencies. Missing part/group/item/folder, permission denial, success/failure/null error, no-copy already removed, live item type mutation, second lookup replacement, zero item/local IDs, all dependency exception ordering. Actual move-to-user inventory persistence must be supplied before runtime enables removal; this is deliberately not a raw registry delete.',
        differences=diffs[:2],
    )
    (w / 'task-inventory-remove-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original task inventory removal observations')


if __name__ == '__main__':
    main()
