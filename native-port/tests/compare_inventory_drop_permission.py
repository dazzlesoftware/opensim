"""Unchanged PermissionsModule.CanDropInObjectInv and actual SDK enums."""

import argparse, hashlib, json, random, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Region/CoreModules/World/Permissions/PermissionsModule.cs'
    generated = w / 'ExtractedDropPermission.cs'
    generated.write_text(
        'using System.Reflection;using OpenMetaverse;partial class Permissions{'
        + extract(
            source.read_text(),
            'private bool CanDropInObjectInv(InventoryItemBase item, ScenePresence sp, SceneObjectPart destPart)',
        )
        + '}'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/inventory_drop_permission_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'InventoryDropPermissionReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [f'{m} 0' for m in range(32768)]
    modes = [0] + [1 << n for n in range(15)] + [8192 | 16384, 512 | 1024, 2048 | 4096]
    rng = random.Random(417)
    modes += [rng.randrange(32768) for _ in range(256)]
    commands += [f'{m} {f}' for m in modes for f in range(1, 5)]
    outputs = []
    for cmd in [
        [a.dotnet, str(w / 'app/InventoryDropPermissionReference.dll')],
        [a.native, '--probe'],
    ]:
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
        sourceHash=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Complete unchanged ScenePresence overload of CanDropInObjectInv. All combinations of missing/deleted presence/part/item/group, bypass/god/locked owner, foreign actor/item/LSL, Transfer, AllowInventoryDrop and effective Modify; preserves debug and permission/root-flag dependency exception ordering. Effective aggregate/object permissions remain real scene providers, not implemented by this predicate.',
        differences=diffs[:2],
    )
    (w / 'inventory-drop-permission-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original inventory drop permission observations')


if __name__ == '__main__':
    main()
