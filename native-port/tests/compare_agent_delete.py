"""Compare original DoAgentDelete with controlled release/close and scheduler seams."""

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
    source = ROOT / 'OpenSim/Server/Handlers/Simulation/AgentHandlers.cs'
    text = source.read_text(encoding='utf-8-sig')
    text = text[text.index('public class AgentSimpleHandler') :]
    method = extract(text, r'protected void DoAgentDelete\(').replace(
        'protected void', 'public void'
    ) + extract(text, r'protected override void ProcessRequest\(').replace(
        'protected override void', 'public void'
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class DeleteReference{'
        + method
        + '}public static class Utils{public static byte[] falseStrBytes=System.Text.Encoding.UTF8.GetBytes("false");public static OpenMetaverse.StructuredData.OSDMap DeserializeJSONOSMap(IOSHttpRequest r)=>throw new Exception();'
        + extract(
            (ROOT / 'OpenSim/Server/Handlers/Simulation/Utils.cs').read_text(encoding='utf-8-sig'),
            r'public static bool GetParams\(',
        )
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
    for f in [wrapper, ROOT / 'native-port/tests/agent_delete_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'AgentDelete.csproj'
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
    eight = seven[:-1] + '8'
    commands = []
    for profile, action, auth in itertools.product(
        range(64),
        [None, '', 'release', 'Release', 'release/', 'close', '試験'],
        [None, '', 'token', 'a+b%20/試験'],
    ):
        value = {'agent': seven, 'region': eight, 'action': action, 'auth': auth}
        commands.append(
            str(profile)
            + ' '
            + json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode().hex()
        )
    agent = '00000000-0000-0000-0000-000000000007'
    region = agent[:-1] + '8'
    base = '/agent/' + agent + '/' + region + '/'
    targets = [
        base,
        base + '?auth=',
        base + '?auth=one&Auth=two',
        base + '?Auth=one&auth=two',
        base + 'release/?auth=ignored',
        base + 'Release/?auth=token',
        '/bad/',
        '/agent/bad/',
        '/agent/' + agent + '/',
        '/agent/00000000-0000-0000-0000-000000000000/' + region + '/',
        '/agent/' + agent + '/00000000-0000-0000-0000-000000000000/',
        base + 'extra/more',
        base + '?auth=%u0041',
        base + '?auth=試験',
    ]
    for profile, target in itertools.product([*range(32), *range(64, 96)], targets):
        commands.append(
            str(profile)
            + ' '
            + json.dumps({'target': target}, ensure_ascii=False, separators=(',', ':'))
            .encode()
            .hex()
        )
    rows = []
    for command in [[a.dotnet, str(w / 'app/AgentDelete.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'agent-delete-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original DoAgentDelete plus ProcessRequest DELETE branch and GetParams; controlled scheduler/service/request projections, destination ID only; other methods/full host and Util scheduler/scene integration pending',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2], indent=2))
    print(f'PASS: {len(commands)} agent release/close source observations')


if __name__ == '__main__':
    main()
