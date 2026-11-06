"""Original LLClientView raw chat framing, SDK bounded valid-Unicode encoding adapter."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs'
    s = source.read_text(encoding='utf-8')
    start = s.index('        static private readonly byte[] ChatFromSimulatorHeader')
    end = s.index('        static private readonly byte[] ImprovedInstantMessageHeader', start)
    method = w / 'ExtractedChatPacket.cs'
    method.write_text(
        'using System;using OpenMetaverse;\npartial class ChatPacketReference {\n'
        + s[start:end]
        + '\n}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [method, ROOT / 'native-port/tests/chat_packet_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'ChatPacketReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'{n} {mode}'
        for n in [
            0,
            1,
            2,
            3,
            5,
            83,
            84,
            85,
            86,
            127,
            128,
            253,
            254,
            255,
            256,
            300,
            340,
            341,
            342,
            511,
            512,
            1022,
            1023,
            1024,
            1100,
        ]
        for mode in range(8)
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/ChatPacketReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=30
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diff = [
        dict(command=c, source=s[:160], native=n[:160])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    report = dict(
        passed=not diff,
        packets=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Complete extracted original SendChatMessage raw framing, controlled buffer/socket and valid-Unicode bounded encoding via actual SDK adapter. ASCII/BMP byte boundaries, embedded NUL, empty and long values; invalid Unicode/source unsafe helper semantics and Unix execution remain separate.',
        differences=diff[:3],
    )
    (w / 'chat-packet-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:3], indent=2)
    print(f'PASS: {len(commands)} original chat packet observations')


if __name__ == '__main__':
    main()
