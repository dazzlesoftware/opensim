"""Unchanged Scene agent inventory add/placement including failed-store client notification."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, sig):
    start = s.index(sig)
    i = s.index('{', start) + 1
    d = 1
    while d:
        if s[i] == '{':
            d += 1
        elif s[i] == '}':
            d -= 1
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
    item = ROOT / 'OpenSim/Framework/InventoryItemBase.cs'
    gen = w / 'ExtractedAgentAdd.cs'
    gen.write_text(
        'using System;using OpenMetaverse;\npartial class Scene{'
        + ''.join(
            extract(source.read_text(), s)
            for s in [
                'public bool AddInventoryItem(InventoryItemBase item)',
                'public bool AddInventoryItem(InventoryItemBase item, bool trigger)',
                'private void ChangePlacement(',
                'public void AddInventoryItem(IClientAPI remoteClient, InventoryItemBase item)',
            ]
        )
        + '}\npartial class InventoryItemBase{'
        + extract(item.read_text(), 'public string CreatorIdentification')
        + '}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [gen, ROOT / 'native-port/tests/agent_inventory_add_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AgentAddReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = list(range(128)) + [
        128,
        256,
        512,
        1024,
        2048,
        4096,
        8192,
        16384,
        32768,
        2 | 128,
        2 | 256,
        2 | 512,
        2 | 1024,
        2 | 2048,
        2 | 4096,
        2 | 4 | 8 | 16,
    ]
    commands = [f'{k} {m} 10 {f}' for k, m, f in itertools.product(range(3), modes, range(15))] + [
        f'0 2 {t} 0' for t in range(-256, 512)
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/AgentAddReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    files = [source, item, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged original Scene.AddInventoryItem(item,bool), default/client overloads and ChangePlacement with actual SDK FolderType enum and original CreatorIdentification. Controlled real service/notifications boundary: initial/retry store outcomes, exact enum/sbyte selection, root/system fallback, god/upload trigger, presence/client/active checks, parent/remove/bulk/alert order, item mutations and dependency exceptions. Canonical creator remains unchanged; storage projection preserves creator data. Real database writes and actual packet delivery remain required host callbacks.',
        differences=diffs[:1],
    )
    (w / 'agent-inventory-add-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original agent inventory add observations')


if __name__ == '__main__':
    main()
