"""Complete original presence state machine with reentrant diagnostic properties and owner retention."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/ScenePresenceStateMachine.cs'
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [source, ROOT / 'native-port/tests/presence_state_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverse.StructuredData')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverse.StructuredData.dll')
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    path = w / 'PresenceState.csproj'
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
    profiles = [
        {},
        {'null_owner': True},
        {'null_name': True},
        {'null_scene': True},
        {'null_scene_name': True},
        {'failure': 'name'},
        {'failure': 'scene'},
    ] + [
        {'mutation': m}
        for m in [
            'name_pre',
            'name_remove',
            'name_removed',
            'scene_pre',
            'scene_remove',
            'scene_removed',
            'name_release',
            'scene_release',
        ]
    ]
    cases = [
        {**profile, 'steps': list(steps)}
        for profile, steps in itertools.product(profiles, itertools.product(range(4), repeat=4))
    ]
    cases += [
        {'steps': prefix + [n]}
        for prefix, n in itertools.product(
            [[], [1], [2], [2, 3]], [-2147483648, -1, 4, 255, 2147483647]
        )
    ]
    commands = [json.dumps(c, separators=(',', ':')).encode().hex() for c in cases]
    rows = []
    for command in [[a.dotnet, str(w / 'app/PresenceState.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=120
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-1000:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'presence-state-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original enum/state machine with controlled scene/name properties, reentrant diagnostics and retained identity. Exact transition/failure text and state checked; CLR monitor/scheduling/GC and full ScenePresence lifetime remain partial. Native uses atomic state to avoid C++ data races while preserving source validation/assignment lock boundary.',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2], indent=2) + ' count=' + str(len(diffs)))
    print(f'PASS: {len(cases)} presence state source observations')


if __name__ == '__main__':
    main()
