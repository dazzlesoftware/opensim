"""Complete InventoryAccessModule.CreateNewInventoryItem against real source/SDK."""

import argparse, hashlib, itertools, json, random, re, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Region/CoreModules/Framework/InventoryAccess/InventoryAccessModule.cs'
    constants = ROOT / 'OpenSim/Framework/Constants.cs'
    util = ROOT / 'OpenSim/Framework/Util.cs'
    c = constants.read_text(encoding='utf-8')
    fields = ''
    for name in ['DefaultScript', 'EmptyNotecard', 'DefaultMaterial']:
        fields += (
            re.search(r'public static readonly string ' + name + r' = .*?;', c).group()
            + f'public static readonly UUID {name}ID = new UUID({name});'
        )
    generated = w / 'ExtractedInventoryNew.cs'
    generated.write_text(
        'using System;using System.Globalization;using OpenMetaverse;using OpenSim.Framework;using PermissionMask=OpenSim.Framework.PermissionMask;partial class InventoryAccessModule{'
        + extract(source.read_text(encoding='utf-8'), 'public void CreateNewInventoryItem(')
        + '}partial class BasicInventoryAccess{'
        + extract(source.read_text(encoding='utf-8'), 'protected virtual string GenerateLandmark(')
        + '}namespace OpenSim.Framework{static class Constants{'
        + fields
        + '}'
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
    for f in [generated, ROOT / 'native-port/tests/inventory_new_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'InventoryNewReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    types = [-128, -1, 0, 3, 5, 7, 10, 13, 27, 56, 57, 96, 100, 127]
    modes = list(range(512)) + [
        512,
        1024,
        2048,
        4096,
        8192,
        16384,
        32768,
        65536,
        131072,
        2 | 8,
        2 | 16,
        16384 | 65536,
        16384 | 8192,
    ]
    commands = [
        f'{m} {it} {at} {sub} {next} 0'
        for m, it, at, sub, next in itertools.product(
            modes, [3, 10, 27], [-1, 5, 7, 10, 13, 56, 57], [0, 255], [0, -1]
        )
    ]
    commands += [f'2 {it} 10 1 -1 0' for it in range(-128, 128)]
    commands += [
        f'{m} {it} {at} 123 -2147483648 {failure}'
        for m, it, at, failure in itertools.product(
            [0, 2, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072],
            [3, 10],
            types,
            range(1, 15),
        )
    ]
    rng = random.Random(1389)
    bits = [
        0,
        0x80000000,
        1,
        0x7F800000,
        0xFF800000,
        0x7FC00000,
        0x3F800000,
        0x38D1B717,
        0x4E6E6B28,
    ] + [rng.randrange(1 << 32) for _ in range(2000)]
    commands += [
        f'L {x} {bits[(j+1)%len(bits)]} {bits[(j+2)%len(bits)]} {rng.randrange(1<<64)}'
        for j, x in enumerate(bits)
    ]
    outputs = []
    for cmd in [[a.dotnet, str(w / 'app/InventoryNewReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=120,
        )
        assert r.returncode == 0, r.stderr
        outputs.append(r.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    files = [source, constants, util, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged inventory creation module method, actual SDK FolderType/InventoryType/AssetType and osUTF8.GetASCIIBytes. Permission/folder fallback and owner checks, original folder ID passed on, transaction module ordering and bypass, presence-null distinction, landmark precedence and raw UTF16 low bytes, optional environment/default UUIDs, subtype flags, source asset store result ignored and post-store mutated ID/type, null strings and injected dependency failures. Complete unchanged base GenerateLandmark with original invariant Single formatting across special/finite float bits and full uint64 handles. HG override, arbitrary invalid native UTF8, persistence and network binding separate.',
        differences=diffs[:2],
    )
    (w / 'inventory-new-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original inventory creation observations')


if __name__ == '__main__':
    main()
