"""Actual simulation stream/GZipStream/OSDMap decoder with real SDK parser."""

import argparse, gzip, hashlib, json, subprocess, zlib, xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Server/Handlers/Simulation/Utils.cs'
    method = extract(
        source.read_text(encoding='utf-8-sig'), r'public static OSDMap DeserializeJSONOSMap\('
    )
    wrapper = w / 'Body.cs'
    wrapper.write_text(
        'using System;using System.IO;using System.IO.Compression;using OpenMetaverse.StructuredData;public static partial class Reference{'
        + method
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [wrapper, ROOT / 'native-port/tests/simulation_body_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'SimulationBody.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    payloads = [
        b'',
        b'{}',
        b'{"a":1,"b":true}',
        b'{"empty":"","a":[1,2,3]}',
        b'[]',
        b'null',
        b'bad json',
        b"{'a':'single'}",
        '{"name":"試験 😀"}'.encode(),
        b'\xef\xbb\xbf{}',
        b'\xff\xfe' + '{"a":1}'.encode('utf-16-le'),
        b'\0\0\xfe\xff' + '{"a":1}'.encode('utf-32-be'),
    ]
    wires = []
    for payload in payloads:
        wires += [payload, zlib.compress(payload)]
        for level in [0, 1, 9]:
            value = gzip.compress(payload, compresslevel=level, mtime=0)
            wires += [
                value,
                value[:-1],
                value[:-8],
                value[:10],
                value + b'trailing',
                value + gzip.compress(b' ', mtime=0),
            ]
            corrupt = bytearray(value)
            corrupt[-8] ^= 1
            wires.append(bytes(corrupt))
    commands = [str(mode) + ' ' + (wire.hex() or '.') for mode in range(8) for wire in wires]
    rows = []
    for command in [[a.dotnet, str(w / 'app/SimulationBody.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'simulation-body-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'differences': diffs[:8],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(str(diffs[:3]) + ' count=' + str(len(diffs)))
    print(f'PASS: {len(commands)} simulation body source observations')


if __name__ == '__main__':
    main()
