"""Original DoQueryAccess, GetParams and actual bundled Vector3.Parse behavior."""

import argparse, hashlib, itertools, json, re, subprocess, xml.etree.ElementTree as ET
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
    handler = 'OpenSim/Server/Handlers/Simulation/AgentHandlers.cs'
    source = (ROOT / handler).read_text(encoding='utf-8-sig')
    query = extract(source, r'protected virtual void DoQueryAccess\(').replace(
        'protected virtual', 'public'
    )
    params = extract(
        (ROOT / 'OpenSim/Server/Handlers/Simulation/Utils.cs').read_text(encoding='utf-8-sig'),
        r'public static bool GetParams\(',
    )
    version = (ROOT / 'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig')
    constants = ''.join(
        re.search(r'public readonly static float ' + n + r' = [^;]+;', version).group()
        for n in [
            'SimulationServiceVersionAcceptedMin',
            'SimulationServiceVersionAcceptedMax',
            'SimulationServiceVersionSupportedMin',
            'SimulationServiceVersionSupportedMax',
        ]
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using GridRegion=OpenSim.Services.Interfaces.GridRegion;using System;using System.Collections.Generic;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using OpenSim.Services.Interfaces;public partial class Reference{'
        + query
        + params
        + '}namespace OpenSim.Framework{public class VersionInfo{'
        + constants
        + '}}',
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
        ROOT / 'native-port/tests/agent_access_reference.cs',
        ROOT / 'OpenSim/Framework/EntityTransferContext.cs',
        ROOT / 'OpenSim/Framework/Culture.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'AgentAccess.csproj'
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

    def h(s):
        return s.encode().hex() or '.'

    versions = [
        None,
        'SIMULATION/0',
        'SIMULATION/0.3',
        'SIMULATION/0.8',
        'SIMULATION/0.9',
        'SIMULATION/NaN',
        'SIMULATION/Infinity',
        'SIMULATION/bad',
        'SIMULATION/0.5/extra',
        'anything',
        '/0,5',
        '/0.0000001',
    ]
    requests = [{}]
    for legacy in versions:
        requests.append({'my_version': legacy})
    values = [0, 0.2, 0.3, 0.5, 0.8, 0.9, 'NaN', 'Infinity', None, True]
    for lo, hi in itertools.product(values, repeat=2):
        requests.append(
            {
                'my_version': 'SIMULATION/0.4',
                'simulation_service_supported_min': lo,
                'simulation_service_supported_max': hi,
                'simulation_service_accepted_min': 0.3,
                'simulation_service_accepted_max': 0.8,
            }
        )
    for lo, hi in itertools.product(values, repeat=2):
        requests.append(
            {
                'simulation_service_supported_min': 0.3,
                'simulation_service_supported_max': 0.8,
                'simulation_service_accepted_min': lo,
                'simulation_service_accepted_max': hi,
            }
        )
    base = {
        'simulation_service_supported_min': 0.3,
        'simulation_service_supported_max': 0.8,
        'simulation_service_accepted_min': 0.3,
        'simulation_service_accepted_max': 0.8,
    }
    seven = '00000000-0000-0000-0000-000000000007'
    for key, options in {
        'position': ['1,2,3', '<1, 2, 3>', 'bad', None, True, [], {}, 'prefix<1.25, 2, 30>tail'],
        'viaTeleport': [False, True, None, 'false', 'FALSE', '1', '0', 0, 1, [], {}],
        'agent_home_uri': [None, '', 'https://home/', '試験', True, {}, []],
        'context': [
            None,
            [],
            {},
            {'InboundVersion': 0.2, 'OutboundVersion': 0.1, 'WearablesCount': 16},
            {'WearablesCount': 'bad'},
        ],
        'features': [None, {}, [], [seven], [seven + 'tail'], ['bad'], [None], [seven, seven]],
    }.items():
        for value in options:
            requests.append({**base, key: value})
    commands = [
        'A ' + str(profile) + ' ' + h(json.dumps(value, separators=(',', ':'), ensure_ascii=False))
        for profile in range(16)
        for value in requests
    ]
    for path_value in [
        '',
        '/',
        '/agent',
        '/agent/',
        '/agent/' + seven,
        'wrong/' + seven + '/bad/action/more',
        '///agent/' + seven + '/' + seven + '/release///',
        '/agent/bad/' + seven,
        '/agent/' + seven.replace('-', '') + '/' + seven,
    ]:
        commands.append('P ' + h(path_value))
    for vector in [
        '',
        '1,2,3',
        '<1,2,3>',
        '1,2,3tail',
        'prefix<1, 2, 3>tail',
        '<1,2,3',
        'bad',
        '<NaN, ∞, -∞>',
        '<1,2,3 4>',
        '<1,2,>',
        '<1, 2, 3>',
        '< 1,2,3>',
        '<1, 2,3>',
        '<1,2, 3>',
    ]:
        commands.append('V ' + h(vector))
    for number in [
        '0',
        '-0',
        '1',
        '+1',
        '.5',
        '1.',
        '1e40',
        '1e-99',
        'NaN',
        'nan',
        '+NaN',
        '-NaN',
        '∞',
        '+∞',
        '-∞',
        'Infinity',
        ' 1 ',
        '\u00a01',
        '1\u00a0',
        '1\0',
        '1\0 ',
        '1\0\0',
        '1 \0',
        '\t1',
        '1\t',
        '\u00851',
        '\u0085NaN',
        '\u00a0∞',
        '−1',
        '1,234',
    ]:
        for position in range(3):
            values = ['1', '2', '3']
            values[position] = number
            commands.append('V ' + h('<' + ','.join(values) + '>'))
    rows = []
    for command in [[a.dotnet, str(w / 'app/AgentAccess.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'agent-access-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256((ROOT / handler).read_bytes()).hexdigest(),
                'differences': diffs[:20],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(str(diffs[:3]) + ' count=' + str(len(diffs)))
    print(f'PASS: {len(commands)} inbound agent access source observations')


if __name__ == '__main__':
    main()
