"""Original connector control flow with actual circuit/appearance/context packing.
Region property inputs and HTTP results are injected; transport is exercised separately.
"""

import argparse, hashlib, json, re, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference, ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    base, paths = reference(a.dotnet, w / 'base')
    project = ET.parse(base)
    props = project.getroot().find('PropertyGroup')
    ET.SubElement(props, 'StartupObject').text = 'ConnectorReference'
    group = project.getroot().find('ItemGroup')
    source_path = 'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs'
    source = (ROOT / source_path).read_text(encoding='utf-8-sig')
    methods = [
        extract(source, r'(?:public|protected virtual) [^\n{]+ ' + n + r'\(')
        for n in ['AgentPath', 'PackData', 'QueryAccess', 'CreateAgent']
    ]
    wrapper = w / 'Connector.cs'
    wrapper.write_text(
        'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using OpenSim.Services.Interfaces;using GridRegion=OpenSim.Services.Interfaces.GridRegion;using WebUtil=ConnectorWeb;public partial class ConnectorReference {'
        + '\n'.join(methods)
        + '}',
        encoding='utf-8',
    )
    util = 'OpenSim/Framework/WebUtil.cs'
    source = (ROOT / util).read_text(encoding='utf-8-sig')
    canonical = w / 'Canonical.cs'
    canonical.write_text(
        'using System;using OpenMetaverse.StructuredData;public static partial class ConnectorWeb{'
        + extract(source, r'private static OSDMap CanonicalizeResults\(')
        + '}',
        encoding='utf-8',
    )
    version = w / 'base/VersionInfo.cs'
    s = version.read_text(encoding='utf-8')
    vs = (ROOT / 'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig')
    s = (
        s[:-2]
        + ''.join(
            re.search(r'public readonly static float ' + n + r' = [^;]+;', vs).group()
            for n in ['SimulationServiceVersionAcceptedMin', 'SimulationServiceVersionSupportedMin']
        )
        + '}}'
    )
    version.write_text(s, encoding='utf-8')
    for f in [
        wrapper,
        canonical,
        ROOT / 'native-port/tests/simulation_connector_reference.cs',
        ROOT / 'OpenSim/Framework/AgentCircuitData.cs',
        ROOT / 'OpenSim/Framework/Culture.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    path = w / 'SimulationConnector.csproj'
    project.write(path, encoding='utf-8', xml_declaration=True)
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

    def h(value):
        return json.dumps(value, separators=(',', ':')).encode().hex()

    replies = [
        {},
        {'success': True},
        {'success': False},
        {'Message': 'custom failure'},
        {'Message': 'Service request failed: [MethodNotAllowed] MethodNotAllowed'},
        {'_Result': None},
        {'_Result': []},
        {'_Result': {}},
        {'_Result': {'success': True, 'reason': 'ok'}},
        {'_Result': {'success': False, 'reason': 'denied'}},
        {'_Result': {'success': True, 'reason': 'old', 'version': 'SIMULATION/0.4'}},
        {'_Result': {'success': True, 'version': 'SIMULATION/0.5'}},
        {'_Result': {'success': True, 'version': 'SIMULATION/0.6'}},
        {'_Result': {'success': True, 'version': 'SIMULATION/bad'}},
        {'_Result': {'success': True, 'version': '0.5'}},
        {
            '_Result': {
                'success': True,
                'negotiated_inbound_version': 0.5,
                'negotiated_outbound_version': 0.7,
            }
        },
        {'_Result': {'success': True, 'negotiated_inbound_version': 0.5}},
        {
            '_Result': {
                'success': True,
                'negotiated_inbound_version': None,
                'negotiated_outbound_version': 0.8,
            }
        },
        {'success': True, 'features': ['00000000-0000-0000-0000-000000000005']},
        {'_Result': {'success': True}, 'features': ['00000000-0000-0000-0000-000000000005', 'bad']},
        {'_Result': {'success': True}, 'features': ['00000000-0000-0000-0000-000000000005tail']},
    ]
    commands = [
        'Q ' + str(profile) + ' ' + reply
        for profile in range(64)
        for reply in [*[h(x) for x in replies], 'throw']
    ]
    commands += [
        'A ' + str(profile) + ' ' + first + ' ' + second
        for profile in range(32)
        for first, second in [
            (h(x), h(y))
            for x, y in [
                (replies[8], replies[9]),
                ({'success': True, **replies[8]}, replies[9]),
                ({}, {'success': True, **replies[8]}),
                ({'success': True, '_Result': False}, {'success': True, **replies[9]}),
                ({}, {}),
                ({'Message': 'first'}, {'Message': 'last'}),
            ]
        ]
        + [('throw', h({})), (h({}), 'throw')]
    ]
    canonical = [
        'true',
        'TRUE',
        'false',
        'False',
        ' true ',
        'null',
        '[]',
        '1',
        '{"success":true}',
        '{"reason":"denied","success":false}',
        '{"x":""}',
        'not json',
        '<llsd><map><key>success</key><boolean>true</boolean></map></llsd>',
    ]
    for value in [
        'true',
        'false',
        '{"x":1,"s":"é中😀"}',
        '{"\\ud800":1,"\\udc00":2}',
        '<llsd><map><key>success</key><boolean>true</boolean></map></llsd>',
        "<? llsd/notation ?>{'success':true,'reason':'notation'}\0\0\0",
        '<? llsd/binary ?>{\x00\x00\x00\x00}',
    ]:
        canonical.extend([value, value.upper(), value.lower(), "\0" + value, "\ufeff" + value])
    commands += ['C 0 ' + s.encode().hex() for s in canonical]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/SimulationConnector.dll')], [a.native]]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=60,
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-500:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)

    def normalized(line):
        if '|' not in line:
            return json.loads(bytes.fromhex(line))
        fields = line.split('|')
        fields[-1] = json.loads(bytes.fromhex(fields[-1]))
        return fields

    diffs = [
        {'profile': c, 'source': normalized(x), 'native': normalized(y)}
        for c, x, y in zip(commands, *rows)
        if normalized(x) != normalized(y)
    ]
    (w / 'simulation-connector-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHashes': {
                    s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                    for s in [source_path, util, 'OpenSim/Framework/AgentCircuitData.cs']
                },
                'differences': diffs[:3],
            },
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:1], indent=2)[:8000])
    gatekeeper = subprocess.run(
        [a.native], input='G 0\n', capture_output=True, text=True, check=True, timeout=10
    )
    assert (
        gatekeeper.stdout.strip()
        == 'http://127.0.0.1:9000/base/foreignagent/00000000-0000-0000-0000-000000000007/'
    ), gatekeeper.stdout
    print(f'PASS: {len(commands)} simulation connector source observations')


if __name__ == '__main__':
    main()
