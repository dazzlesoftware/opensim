"""Original LLClientView outgoing wearable and appearance methods + real SDK."""

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
    start = s.index('        public void SendWearables(AvatarWearable[] wearables, int serial)')
    end = s.index('        static private readonly byte[] AvatarAnimationHeader', start)
    extracted = w / 'ExtractedAvatarPackets.cs'
    extracted.write_text(
        'using System;using OpenMetaverse;using OpenMetaverse.Packets;using OpenSim.Framework;\npartial class AvatarPacketsReference {\n'
        + s[start:end]
        + '\n}\n',
        encoding='utf-8',
    )
    wearable = ROOT / 'OpenSim/Framework/AvatarWearable.cs'
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [extracted, wearable, ROOT / 'native-port/tests/avatar_packets_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AvatarPacketsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [f'{seed} {mode}' for seed in range(256) for mode in [0, 1]]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/AvatarPacketsReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=30
        )
        assert r.returncode == 0, r.stdout[-1000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diff = [
        dict(command=c, source=s[:120], native=n[:120])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    files = [source, wearable, ROOT / 'bin/OpenMetaverse.dll', ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diff,
        packets=len(commands),
        bytes=sum(len(row) // 2 for row in rows[0]),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Original complete outgoing SendWearables and SendAppearance methods, original AvatarWearable and actual SDK encoder. Pool/socket allocation controlled. AvatarFactory baking/cache/save/input handling and Unix execution remain pending.',
        differences=diff[:4],
    )
    (w / 'avatar-packets-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:4], indent=2)
    print(
        f'PASS: {report["packets"]} original wearable/appearance packets, {report["bytes"]} exact bytes'
    )


if __name__ == '__main__':
    main()
