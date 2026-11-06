"""Original LLClientView inventory send methods/helpers and actual SDK packets/CRC/splitting."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, signature):
    start = s.index(signature)
    brace = s.index('{', start)
    end = brace + 1
    depth = 1
    while depth:
        if s[end] == '{':
            depth += 1
        elif s[end] == '}':
            depth -= 1
        end += 1
    return s[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs'
    s = source.read_text()
    signatures = [
        'public void SendInventoryFolderDetails(',
        'private static InventoryDescendentsPacket.FolderDataBlock CreateFolderDataBlock(',
        'private static InventoryDescendentsPacket.ItemDataBlock CreateItemDataBlock(',
        'private static void AddNullFolderBlockToDecendentsPacket(',
        'private  static void AddNullItemBlockToDescendentsPacket(',
        'private InventoryDescendentsPacket CreateInventoryDescendentsPacket(',
        'public void SendInventoryItemDetails(',
    ]
    generated = w / 'ExtractedInventoryPackets.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.Packets;\npartial class InventoryPacketsReference{\n'
        + '\n'.join(extract(s, signature) for signature in signatures)
        + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/inventory_packets_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'InventoryPacketsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'0 {seed} {ni} 0 {mode} 3'
        for seed, ni, mode in itertools.product(
            [0, 1, 7], [0, 1, 2, 9, 10, 11, 12, 19, 20, 21, 22], [0, 1, 2, 4, 8, 16]
        )
    ]
    commands += [
        f'1 {seed} {ni} {nf} {mode} {flags}'
        for seed, ni, nf, mode, flags in itertools.product(
            [0, 1, 7], [0, 1, 5, 6, 11], [0, 1, 6, 7, 13], [0, 1, 2, 4, 8, 16, 32], range(4)
        )
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/InventoryPacketsReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [
        dict(command=c, source=s[:1000], native=n[:1000])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceNullFailures=sum(r.startswith('NULL') for r in rows[0]),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, ROOT / 'bin/OpenMetaverse.dll', ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Original SendInventoryItemDetails and SendInventoryFolderDetails plus all five folder block/packet helpers unchanged; actual SDK packet Length/ToBytes/ToBytesMultiple1200 and CRC/zeroencode. Asset throttle and automatic split flag, raw packets and encoded split datagrams compared. Original item final-batch uninitialized-array failures and already-emitted prefixes preserved. Valid UTF8/nullable entries; null array references, PacketPool reuse, actual retransmission/throttle delivery and other inventory update methods remain separate.',
        differences=diffs[:2],
    )
    (w / 'inventory-packets-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original inventory packet/emission/error results')


if __name__ == '__main__':
    main()
