"""Original friends handler methods and original form/XML response helpers."""

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
    source_path = ROOT / 'OpenSim/Server/Handlers/Friends/FriendsServerPostHandler.cs'
    source = source_path.read_text(encoding='utf-8-sig')
    names = [
        'ProcessRequest',
        'GetFriends',
        'GetFriendsString',
        'PackageFriends',
        'StoreFriend',
        'DeleteFriend',
        'DeleteFriendString',
        'SuccessResult',
        'FromKeyValuePairs',
    ]
    methods = [
        extract(
            source, r'(?:protected override|private|)\s*(?:byte\[\]|void) ' + name + r'\('
        ).replace('protected override', 'public')
        for name in names
    ]
    methods += [
        extract(source, r'private byte\[\] FailureResult\(\)'),
        extract(source, r'private byte\[\] FailureResult\(string'),
    ]
    wrapper = w / 'Handler.cs'
    wrapper.write_text(
        'using System;using System.IO;using System.Xml;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Server.Base;using FriendInfo=OpenSim.Services.Interfaces.FriendInfo;using Util=EndpointUtil;public partial class FriendsHandler{'
        + ''.join(methods)
        + '}',
        encoding='utf-8',
    )
    utils = (ROOT / 'OpenSim/Server/Base/ServerUtils.cs').read_text(encoding='utf-8-sig')
    server = w / 'ServerUtils.cs'
    server.write_text(
        'using System;using System.Collections.Generic;using System.Xml;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base{public static class ServerUtils{'
        + ''.join(
            extract(utils, r'(?:public|private) static [^\n{]+ ' + n + r'\(')
            for n in ['ParseQueryString', 'BuildXmlResponse', 'BuildXmlData']
        )
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
        server,
        ROOT / 'OpenSim/Services/Interfaces/IFriendsService.cs',
        ROOT / 'native-port/tests/friends_endpoint_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'FriendsEndpoint.csproj'
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
    bodies = ['', 'METHOD=unknown', 'METHOD[]=getfriends']
    for method in [
        'getfriends',
        'getfriends_string',
        'storefriend',
        'deletefriend',
        'deletefriend_string',
    ]:
        for principal in [
            None,
            '',
            'bad',
            seven,
            seven[:-1] + '0',
            seven[:-1] + '1',
            seven[:-1] + '8',
            seven[:-1] + '9',
            seven.replace('-', ''),
        ]:
            for suffix in [
                '',
                '&FRIEND=friend&MyFlags=7',
                '&Friend=friend&MyFlags=-2147483648',
                '&PRINCIPALID[]=bad&FRIEND[]=a&FRIEND[]=b',
                '&PrincipalID=string-id&Friend=a%26b&MyFlags=2147483648',
                '&PrincipalID[]=one&PrincipalID[]=two&Friend[]=a&MyFlags[]=7',
            ]:
                bodies.append(
                    'METHOD='
                    + method
                    + ('' if principal is None else '&PRINCIPALID=' + principal)
                    + suffix
                )
    raw = [s.encode() for s in bodies]
    body = 'METHOD=getfriends&PRINCIPALID=' + seven
    raw += [
        b'\xef\xbb\xbf' + body.encode(),
        b'\xff\xfe' + body.encode('utf-16-le'),
        b'\xfe\xff' + body.encode('utf-16-be'),
        b'\xff\xfe\0\0' + body.encode('utf-32-le'),
        b'\0\0\xfe\xff' + body.encode('utf-32-be'),
    ]
    commands = [s.hex() for s in raw]
    rows = []
    for command in [[a.dotnet, str(w / 'app/FriendsEndpoint.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=45
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    differences = [
        {'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y
    ]
    (w / 'friends-endpoint-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source_path.read_bytes()).hexdigest(),
                'differences': differences,
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if differences:
        raise AssertionError(str(differences[:2]))
    print(f'PASS: {len(commands)} friends handler source observations')


if __name__ == '__main__':
    main()
