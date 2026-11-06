"""Original complete scene user update/move/remove methods and full item classes."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs'
    util = ROOT / 'OpenSim/Framework/Util.cs'
    generated = w / 'ExtractedUserInventory.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using PermissionMask=OpenSim.Framework.PermissionMask;partial class Scene{'
        + ''.join(
            extract(source.read_text(encoding='utf-8'), sig)
            for sig in [
                'public void UpdateInventoryItem(',
                'public void MoveInventoryItem(',
                'private void RemoveInventoryItem(',
                'private void RemoveInventoryFolder(',
            ]
        )
        + '}\nnamespace OpenSim.Framework{'
        + extract(util.read_text(encoding='utf-8'), 'public enum PermissionMask')
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
    files = [
        generated,
        ROOT / 'OpenSim/Framework/InventoryNodeBase.cs',
        ROOT / 'OpenSim/Framework/InventoryItemBase.cs',
        ROOT / 'native-port/tests/user_inventory_operations_reference.cs',
    ]
    for f in files:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'UserInventoryOperationsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = set(
        [
            0,
            1,
            2,
            4,
            8,
            16,
            32,
            64,
            128,
            256,
            512,
            1024,
            2048,
            4096,
            8192,
            16384,
            32768,
            65536,
            131072,
            262144,
        ]
    )
    modes.update(
        sum(bits)
        for bits in itertools.product(
            [0, 8],
            [0, 16],
            [0, 32],
            [0, 4],
            [0, 1024],
            [0, 2048],
            [0, 4096],
            [0, 8192],
            [0, 16384],
            [0, 256],
        )
    )
    rng = random.Random(812)
    modes.update(rng.randrange(1 << 19) for _ in range(128))
    commands = [f'0 {m} {t} 0' for m in sorted(modes) for t in range(8)] + [
        f'0 {m} 0 {f}' for m in sorted(modes) for f in range(1, 7)
    ]
    commands += [f'1 {m} 0 {f}' for m in range(0, 4096) for f in [0, 1, 5, 7, 8, 9]]
    commands += [f'{k} 0 0 {f}' for k in [2, 3] for f in range(12)]
    outputs = []
    for cmd in [
        [a.dotnet, str(w / 'app/UserInventoryOperationsReference.dll')],
        [a.native, '--probe'],
    ]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        outputs.append(r.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    sources = [source, util, files[1], files[2], ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources
        },
        scope='Complete unchanged scene UpdateInventoryItem/MoveInventoryItem/RemoveInventoryItem/RemoveInventoryFolder with actual full InventoryItemBase/InventoryNodeBase and SDK text, flags and identity. Export validity/forcing/masking, low flags, landmark HG/suffix preservation, zero-next persistence gate, source service mutation, alias identity, transaction/bulk behavior, date, sale and wearable paths; move false/missing/null entries, list mutations before/during enumeration and all dependency throws. Native input strings are valid UTF8 and default culture invariant; other thread cultures injectable.',
        differences=diffs[:2],
    )
    (w / 'user-inventory-operations-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original user inventory operation observations')


if __name__ == '__main__':
    main()
