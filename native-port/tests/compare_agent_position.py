"""Original complete AgentPosition/legacy child model, actual SDK and context."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Framework/ChildAgentDataUpdate.cs'
    text = source.read_text(encoding='utf-8-sig')
    classes = ''.join(
        extract(text, pattern)
        for pattern in [
            r'public class ChildAgentDataUpdate',
            r'public interface IAgentData',
            r'public class AgentPosition',
        ]
    )
    wrapper = w / 'Models.cs'
    wrapper.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;namespace OpenSim.Framework{'
        + classes
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
    for f in [
        wrapper,
        ROOT / 'native-port/tests/agent_position_reference.cs',
        ROOT / 'OpenSim/Framework/EntityTransferContext.cs',
        ROOT / 'OpenSim/Framework/Culture.cs',
        ROOT / 'OpenSim/Framework/VersionInfo.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'Position.csproj'
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
    seven = '00000000-0000-0000-0000-000000000007'
    requests = [
        {},
        {
            'region_handle': '18446744073709551615',
            'circuit_code': '4294967295',
            'agent_uuid': seven,
            'session_uuid': seven[:-1] + '8',
            'position': '<1, 2, 30>',
            'far': 256.5,
            'god_data': {'ViewerUiIsGod': True},
            'throttles': [0, 1, 2, 255],
            'children_seeds': [{'handle': '3', 'seed': 'seed'}],
        },
    ]
    numbers = [
        None,
        True,
        False,
        0,
        1,
        -1,
        12.5,
        '',
        'bad',
        '+1',
        '-0',
        '1\0',
        '1\0 ',
        ' 42 ',
        '18446744073709551615',
        '18446744073709551616',
        '4294967295',
        '4294967296',
        {},
        [],
        [1, 2],
    ]
    fields = {
        'region_handle': numbers,
        'circuit_code': numbers,
        'agent_uuid': [None, seven, seven.replace('-', ''), seven + 'tail', 'bad', 0, {}, []],
        'session_uuid': [None, seven, 'bad', True, {}],
        'far': [None, 0, 1.25, -2, 'NaN', 'Infinity', '-Infinity', 'bad', '1,234', {}, []],
        'changed_grid': [None, True, False, 'false', 'False', '0', '1', {}, [], [0]],
        'god_data': [
            None,
            {},
            [],
            True,
            12,
            '',
            {'ViewerUiIsGod': True},
            {'ViewerUiIsGod': False},
            {'ViewerUiIsGod': 'false'},
            {'ViewerUiIsGod': []},
            {'ViewerUiIsGod': [0]},
        ],
        'throttles': [
            None,
            [],
            [0, 1, 2, 255],
            [256, -1, 12.5],
            True,
            False,
            1,
            12.5,
            'AQID',
            {},
            [None, {}, []],
        ],
        'children_seeds': [
            None,
            {},
            [],
            [3],
            [{}],
            [{'seed': 'missing handle'}],
            [{'handle': 'bad', 'seed': 'bad'}],
            [{'handle': '0'}],
            [{'handle': '-0', 'seed': None}],
            [{'handle': '3', 'seed': 'first'}, {'handle': '3', 'seed': 'second'}],
            [
                {'handle': '9', 'seed': 'nine'},
                {'handle': '8', 'seed': 'eight'},
                {'handle': '7', 'seed': None},
            ],
            [{'handle': '18446744073709551615', 'seed': 'max'}],
        ],
    }
    for key in ['position', 'velocity', 'center', 'size', 'at_axis', 'left_axis', 'up_axis']:
        fields[key] = [
            None,
            '',
            'bad',
            '1,2,3',
            '<1, 2, 3>',
            'prefix<1.25, 2, 30>tail',
            '<NaN, Infinity, -Infinity>',
            True,
            [],
            {},
        ]
    for key, values in fields.items():
        for value in values:
            requests.append({key: value})
    requests += [
        {'god_data': {'ViewerUiIsGod': True}, 'replace_god': True},
        {'throttles': {}, 'children_seeds': [], 'god_data': {}, 'far': 23.5},
    ]
    commands = [
        str(profile)
        + ' '
        + json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode().hex()
        for profile in range(64)
        for value in requests
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/Position.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=120
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'agent-position-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original AgentPosition Pack/Unpack/CopyFrom and legacy child fields with actual SDK; JSON-representable inputs plus typed binary/shared GodData mutation, scene/runtime and other ChildAgentDataUpdate.cs models remain pending',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2], indent=2) + ' count=' + str(len(diffs)))
    print(f'PASS: {len(commands)} agent position source observations')


if __name__ == '__main__':
    main()
