"""Original outgoing CreateObject overloads with real SDK serialization and controlled callbacks."""

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
        extract(text, pattern)
        for pattern in [
            r'public bool CreateObject\(GridRegion destination, Vector3',
            r'public bool CreateObject\(GridRegion destination, UUID',
        ]
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class RemoteObject{'
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
    for f in [wrapper, ROOT / 'native-port/tests/simulation_object_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'RemoteObject.csproj'
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
    replies = [
        None,
        {},
        {'success': True},
        {'success': False},
        {'Success': True},
        {'success': 'true'},
        {'success': 'false'},
        {'success': 1},
    ]
    profiles = [
        {},
        {'custom': True},
        {'null_destination': True},
        {'null_object': True},
        {'null_state': True},
        {'null_name': True},
        {'mutate': True},
        {'inventory': True},
    ] + [{'failure': f} for f in ['path', 'uuid', 'xml', 'extra', 'modified', 'state', 'request']]
    cases = [
        {
            **profile,
            'reply': reply,
            'state': state,
            'extra': 'extra',
            'modified': True,
            'local': local,
        }
        for profile, reply, state, local in itertools.product(
            profiles, replies, ['', 'scripts', '試験'], [False, True]
        )
    ]
    commands = [
        json.dumps(c, ensure_ascii=False, separators=(',', ':')).encode().hex() for c in cases
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/RemoteObject.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-1000:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'simulation-object-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original outgoing CreateObject overloads with actual SDK JSON and controlled scene object, region and transport. Full world object serialization and general CLR runtime/platform remain pending.',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2], indent=2) + ' count=' + str(len(diffs)))
    print(f'PASS: {len(cases)} outgoing object connector source observations')


if __name__ == '__main__':
    main()
