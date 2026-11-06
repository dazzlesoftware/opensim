"""Original XferDownLoad and registry methods, actual raw SendXferPacket framing."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Region/CoreModules/Agent/Xfer/XferModule.cs'
    client = ROOT / 'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs'
    s = source.read_text(encoding='utf-8')
    c = client.read_text(encoding='utf-8')
    signatures = [
        'private class FileData',
        'public bool AddNewFile(',
        'public void expireFiles(',
        'private void RemoveOrDecrementFile(',
        'public void transfersTimeTick(',
        'public void RequestXfer(',
        'public void AckPacket(',
        'public void AbortXfer(',
        'public class XferDownLoad',
    ]
    start = c.index('        static private readonly byte[] ReplyTaskInventoryHeader')
    end = c.index('        static private readonly byte[] AbortXferHeader', start)
    generated = w / 'ExtractedXfer.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using System.Threading;using OpenMetaverse;\npartial class XferReference{\n'
        + '\n'.join(extract(s, sig) for sig in signatures)
        + '\n}\npartial class Client{\n'
        + c[start:end]
        + '\n}\n',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/xfer_download_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'XferReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'0 {size} {burst} {ping} {mode}'
        for size in [0, 1, 2, 1023, 1024, 1025, 2047, 2048, 2049, 8192, 34000]
        for burst in [-1, 0, 1, 4, 32]
        for ping in [20, 201, 2500]
        for mode in range(8)
    ]
    commands += [
        f'1 {size} {throttle} {ping} {mode}'
        for size in [0, 1, 1024, 1025, 8192]
        for throttle in [0, 1024, 3000000]
        for ping in [20, 201, 2500]
        for mode in range(4)
    ]
    commands += [
        f'2 {size} {serial} 0 0'
        for size in [0, 1, 51, 255, 256, 257, 511]
        for serial in [-32768, -1, 0, 1, 32767]
    ]
    commands += [
        f'3 {size} {throttle} {ping} 0'
        for size in [0, 1, 1024, 1025, 8192]
        for throttle in [0, 1024, 3000000]
        for ping in [20, 201, 2500]
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/XferReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (
        len(rows[0]),
        len(rows[1]),
        len(commands),
    )
    diffs = [
        dict(command=c, source=s[:2500], native=n[:2500])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, client, ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Complete unchanged original XferDownLoad and file registry request/ACK/abort/expiry/time-tick methods plus raw SendXferPacket framing, actual SDK integer encoding. Controlled clock, client ping/throttle, buffer/sink and single-thread scheduler; module event threading and adversarial invalid-offset sends remain separate. Packet bodies are exact hex.',
        differences=diffs[:2],
    )
    (w / 'xfer-download-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original Xfer packet/retry/ACK/registry traces')


if __name__ == '__main__':
    main()
