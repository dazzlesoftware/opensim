"""Exact LLClientView.SendInventoryItemCreateUpdate source and actual SDK packet/CRC."""

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
    client = ROOT / 'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs'
    node = ROOT / 'OpenSim/Framework/InventoryNodeBase.cs'
    item = ROOT / 'OpenSim/Framework/InventoryItemBase.cs'
    gen = w / 'ExtractedItemCreate.cs'
    gen.write_text(
        'using System;using OpenMetaverse;using OpenMetaverse.Packets;\npartial class ItemCreateReference{'
        + extract(
            client.read_text(),
            'public void SendInventoryItemCreateUpdate(InventoryItemBase Item, UUID transactionID, uint callbackId)',
        )
        + '}\npartial class InventoryNodeBase{'
        + extract(node.read_text(), 'public virtual string Name')
        + '}\npartial class InventoryItemBase{'
        + ''.join(
            extract(item.read_text(), x)
            for x in [
                'public string Description',
                'public string CreatorId',
                'public UUID CreatorIdAsUuid',
            ]
        )
        + '}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [gen, ROOT / 'native-port/tests/inventory_item_create_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'ItemCreateReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'{s} {m}'
        for s, m in itertools.product([-129, -128, -1, 0, 1, 127, 128, 255, 256, 257], range(256))
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/ItemCreateReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    assert len(rows[0]) == len(rows[1]) == len(commands)
    files = [
        client,
        node,
        item,
        ROOT / 'bin/OpenMetaverse.dll',
        ROOT / 'bin/OpenMetaverseTypes.dll',
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Unchanged original LLClientView.SendInventoryItemCreateUpdate with original item Name/Description/Creator properties, actual SDK packet/InventoryCRC/ZeroEncode. Full fields, signed enum wraps, callback/transaction, full-mask CRC fudge, flagsmask, Unicode/NUL/whitespace/length cases. Actual transport delivery is host responsibility.',
        differences=diffs[:1],
    )
    (w / 'inventory-item-create-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original UpdateCreateInventoryItem packets')


if __name__ == '__main__':
    main()
