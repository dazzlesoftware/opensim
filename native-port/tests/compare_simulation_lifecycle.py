"""Original remote release/close methods, controlled HTTP and region projection."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs'
    text = source.read_text(encoding='utf-8-sig')
    methods = ''.join(
        extract(text, r'public bool ' + name + r'\(') for name in ['ReleaseAgent', 'CloseAgent']
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using OpenMetaverse;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class LifecycleReference{'
        + methods
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
    for f in [wrapper, ROOT / 'native-port/tests/simulation_lifecycle_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'Lifecycle.csproj'
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
    commands = []
    for profile, op, value in itertools.product(
        range(8),
        ['release', 'close'],
        [
            None,
            '',
            'http://sim/agent/release/',
            'token',
            'a+b%20&other=x',
            '試験',
            '\r\n',
            '?auth=x#fragment',
        ],
    ):
        args = {'op': op, 'uri': value, 'auth': value}
        commands.append(
            str(profile)
            + ' '
            + json.dumps(args, ensure_ascii=False, separators=(',', ':')).encode().hex()
        )
    rows = []
    for command in [[a.dotnet, str(w / 'app/Lifecycle.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'simulation-lifecycle-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original ReleaseAgent and CloseAgent with controlled WebUtil and ServerURI projection; real HTTP tested separately',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2], indent=2))
    print(f'PASS: {len(commands)} remote agent lifecycle source observations')


if __name__ == '__main__':
    main()
