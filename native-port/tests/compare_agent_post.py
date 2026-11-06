"""Original create-agent post core with complete original circuit and appearance."""

import argparse, hashlib, json, re, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_asset_model import reference, ROOT
from compare_presence_endpoint import extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    reference(a.dotnet, w / 'assets')
    project = ET.parse(w / 'assets/AssetModelReference.csproj')
    group = project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag == 'Compile' and Path(item.get('Include')).name == 'asset_model_reference.cs':
            group.remove(item)
    ET.SubElement(project.getroot().find('PropertyGroup'), 'StartupObject').text = 'PostReference'
    handler = ROOT / 'OpenSim/Server/Handlers/Simulation/AgentHandlers.cs'
    source = handler.read_text(encoding='utf-8-sig')
    simple = source[
        source.index('public class AgentSimpleHandler') : source.index(
            'public class AgentDestinationData'
        )
    ]
    methods = [
        extract(simple, r'(?:protected virtual|protected) [^\n{]+ ' + n + r'\(')
        .replace('protected virtual', 'public')
        .replace('protected void', 'public void')
        for n in [
            'DoAgentPost',
            'CreateAgentDestinationData',
            'UnpackData',
            'ExtractGatekeeper',
            'CreateAgent',
        ]
    ]
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class PostReference{'
        + ''.join(methods)
        + '}',
        encoding='utf-8',
    )
    version = (ROOT / 'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig')
    stub = w / 'VersionInfo.cs'
    stub.write_text(
        'namespace OpenSim.Framework{public class VersionInfo{'
        + ''.join(
            re.search(r'public readonly static float ' + n + r' = [^;]+;', version).group()
            for n in ['SimulationServiceVersionAcceptedMax', 'SimulationServiceVersionSupportedMax']
        )
        + '}}',
        encoding='utf-8',
    )
    sources = [
        'AgentCircuitData.cs',
        'AvatarAppearance.cs',
        'AvatarWearable.cs',
        'AvatarAttachment.cs',
        'WearableCacheItem.cs',
        'IAssetCache.cs',
        'EntityTransferContext.cs',
        'Culture.cs',
    ]
    for f in [
        wrapper,
        stub,
        *[ROOT / 'OpenSim/Framework' / s for s in sources],
        ROOT / 'native-port/tests/avatar_appearance_reference.cs',
        ROOT / 'native-port/tests/agent_post_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'AgentPost.csproj'
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
    seven = '00000000-0000-0000-0000-000000000007'
    eight = seven[:-1] + '8'
    base = {
        'agent_id': seven,
        'session_id': eight,
        'secure_session_id': seven[:-1] + '9',
        'circuit_code': '123456',
        'first_name': 'First',
        'last_name': 'Last',
        'start_pos': '<1, 2, 30>',
        'caps_path': 'seed',
        'destination_uuid': eight,
        'destination_x': '25600',
        'destination_y': '51200',
        'destination_name': 'Destination',
        'teleport_flags': 128,
    }
    requests = [{}, base]
    fields = {
        'destination_x': [None, 'bad', '2147483648', -2147483648, 12.5, True],
        'destination_y': [None, '-1', '1\0', '1\0 ', []],
        'destination_uuid': [None, 'bad', seven, seven.replace('-', ''), seven + 'tail'],
        'destination_name': [None, '', '試験', True, 12.5, {}, [], {'name': 'value'}],
        'teleport_flags': [None, 0, 128, 1073741824, 1073741952, 4294967295, '-1', 'bad'],
        'context': [
            None,
            [],
            {},
            {'InboundVersion': 0.3, 'OutboundVersion': 0.4, 'WearablesCount': 16},
        ],
        'children_seeds': [[3], [], [{}], [{'handle': '1', 'seed': 'seed'}]],
        'packed_appearance': [None, {}, {'height': 0.25}, {'wearables': [3]}],
        'serviceurls': [None, {}, {'a': 'A', 'b': None}],
        'source_uuid': [None, 'bad', seven, seven + 'tail'],
    }
    for key, values in fields.items():
        for value in values:
            requests.append({**base, key: value})
    source_base = {
        **base,
        'source_uuid': seven,
        'source_x': '-256',
        'source_y': '512',
        'source_name': 'Source',
    }
    requests += [source_base, {**source_base, 'source_server_uri': 'http://source/path'}]
    for key in ['source_x', 'source_y', 'source_name', 'source_server_uri']:
        for value in [None, '', 'bad', '2147483648', True, 12.5, [], {}]:
            requests.append({**source_base, key: value})
    commands = [
        str(profile)
        + ' '
        + json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode().hex()
        for profile in range(64)
        for value in requests
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/AgentPost.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)

    def normalized(row):
        parts = row.split('|')
        parts[-1] = json.loads(bytes.fromhex(parts[-1])) if parts[-1] else None
        return parts

    diffs = [
        {'input': c, 'source': normalized(x), 'native': normalized(y)}
        for c, x, y in zip(commands, *rows)
        if normalized(x) != normalized(y)
    ]
    (w / 'agent-post-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(handler.read_bytes()).hexdigest(),
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:1], indent=2)[:7000] + ' count=' + str(len(diffs)))
    print(f'PASS: {len(commands)} inbound create-agent source observations')


if __name__ == '__main__':
    main()
