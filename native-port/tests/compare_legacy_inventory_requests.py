"""Actual AsyncInventorySender/Scene folder flow plus original LLClientView packet methods."""

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
    packet = ROOT / 'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs'
    scene = ROOT / 'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs'
    handlers = ROOT / 'OpenSim/Region/Framework/Scenes/Scene.PacketHandlers.cs'
    sender = ROOT / 'OpenSim/Region/Framework/Scenes/AsyncInventorySender.cs'
    signatures = [
        'public void SendInventoryFolderDetails(',
        'private static InventoryDescendentsPacket.FolderDataBlock CreateFolderDataBlock(',
        'private static InventoryDescendentsPacket.ItemDataBlock CreateItemDataBlock(',
        'private static void AddNullFolderBlockToDecendentsPacket(',
        'private  static void AddNullItemBlockToDescendentsPacket(',
        'private InventoryDescendentsPacket CreateInventoryDescendentsPacket(',
        'public void SendInventoryItemDetails(',
    ]
    generated = w / 'ExtractedLegacyInventory.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using System.Collections.Concurrent;using System.Threading;using OpenMetaverse;using OpenMetaverse.Packets;using IClientAPI=Client;using InventoryFolderImpl=LibraryNode;using Thread=ControlledThread;\npartial class Client{\n'
        + '\n'.join(extract(packet.read_text(), sig) for sig in signatures)
        + '\n}\npartial class LegacyReference{\n'
        + extract(handlers.read_text(), 'public void HandleFetchInventoryDescendents(')
        + '\n'
        + extract(handlers.read_text(), 'void SendInventoryAsync(')
        + '\n'
        + extract(scene.read_text(), 'public void SendInventoryUpdate(')
        + '\n}\npartial class ItemSender{\n'
        + extract(sender.read_text(), 'protected void ProcessQueue(').replace(
            'protected void ProcessQueue(', 'public void ProcessQueue('
        )
        + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/legacy_inventory_requests_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'LegacyInventoryRequestsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = [
        0,
        1,
        2,
        4,
        8,
        16,
        32,
        64,
        128,
        1 | 32,
        2 | 32,
        1 | 4,
        2 | 8,
        16 | 64,
        256,
        512,
        1024 | 1,
        2048 | 2,
    ]
    commands = [
        f'0 {n} 2 {mode} 3' for n, mode in itertools.product([0, 1, 2, 3, 10, 11, 12], modes)
    ]
    commands += [
        f'{kind} 0 {folder} {mode} {flags}'
        for kind, folder, mode, flags in itertools.product([1, 2], [0, 2, 3], modes, range(4))
    ]
    rows = []
    for cmd in [
        [a.dotnet, str(w / 'app/LegacyInventoryRequestsReference.dll')],
        [a.native, '--probe'],
    ]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [
        dict(command=c, source=s[:1500], native=n[:1500])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [
                packet,
                scene,
                handlers,
                sender,
                ROOT / 'bin/OpenMetaverse.dll',
                ROOT / 'bin/OpenMetaverseTypes.dll',
            ]
        },
        scope='Actual AsyncInventorySender.ProcessQueue, Scene.HandleFetchInventoryDescendents/SendInventoryAsync/SendInventoryUpdate and all outgoing LLClientView packet methods unchanged except protected ProcessQueue visibility; actual SDK packet bytes. Controlled FireAndForget scheduling, Thread.Sleep via alias, active-client/property/library/SQLite provider substitutes; one queued request per trace, child/root policy and production queue scheduling remain host scope.',
        differences=diffs[:2],
    )
    (w / 'legacy-inventory-requests-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(
        f'PASS: {len(commands)} original legacy inventory provider/activity/packet/queue-gate traces'
    )


if __name__ == '__main__':
    main()
