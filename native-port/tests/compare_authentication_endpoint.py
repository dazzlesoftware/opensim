"""Compare original auth-info methods, model and plain authentication handler."""

import argparse, hashlib, itertools, json, subprocess, urllib.parse, xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    handler = 'OpenSim/Server/Handlers/Authentication/AuthenticationServerPostHandler.cs'
    service = 'OpenSim/Services/AuthenticationService/AuthenticationServiceBase.cs'
    utils = 'OpenSim/Server/Base/ServerUtils.cs'
    model = 'OpenSim/Services/Interfaces/IAuthenticationService.cs'
    data = 'OpenSim/Data/IAuthenticationData.cs'
    source = (ROOT / handler).read_text(encoding='utf-8-sig')
    patterns = [
        r'protected override byte\[\] ProcessRequest\(',
        r'private byte\[\] DoPlainMethods\(',
        r'private byte\[\] DoEncryptedMethods\(',
        r'private byte\[\] SuccessResult\(\)',
        r'private byte\[\] FailureResult\(',
        r'private byte\[\] SuccessResult\(string token\)',
        r'byte\[\] GetAuthInfo\(',
        r'byte\[\] SetAuthInfo\(',
        r'private byte\[\] ResultToBytes\(',
    ]
    methods = [
        extract(source, pattern).replace("protected override", "private") for pattern in patterns
    ]
    wrapper = w / 'Handler.cs'
    wrapper.write_text(
        'using System;using System.IO;using System.Text;using System.Xml;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Server.Base;using Util=AuthEndpointUtil;public partial class AuthenticationHandler {'
        + '\n'.join(methods)
        + '}',
        encoding='utf-8',
    )
    source = (ROOT / service).read_text(encoding='utf-8-sig')
    core = w / 'Info.cs'
    core.write_text(
        'using System;using OpenMetaverse;using OpenSim.Data;using OpenSim.Services.Interfaces;public partial class AuthInfoReference {'
        + '\n'.join(
            extract(source, pattern)
            for pattern in [
                r'public virtual AuthInfo GetAuthInfo\(',
                r'public virtual bool SetAuthInfo\(',
            ]
        )
        + '}',
        encoding='utf-8',
    )
    source = (ROOT / utils).read_text(encoding='utf-8-sig')
    server = w / 'ServerUtils.cs'
    server.write_text(
        'using System;using System.Collections.Generic;using System.Xml;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base {public static class ServerUtils {'
        + '\n'.join(
            extract(source, r'(?:public|private) static [^\n{]+ ' + n + r'\(')
            for n in ['ParseQueryString', 'BuildXmlResponse', 'BuildXmlData']
        )
        + '}}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        StartupObject='AuthenticationHandler',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        wrapper,
        core,
        server,
        ROOT / model,
        ROOT / data,
        ROOT / 'OpenSim/Framework/Culture.cs',
        ROOT / 'native-port/tests/authentication_endpoint_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    r = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'AuthenticationEndpoint.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    config = w / 'config'
    config.mkdir(exist_ok=True)
    for flags in range(8):
        (config / str(flags)).write_text(
            '[AuthenticationService]\n'
            + ''.join(
                k + '=' + str(bool(flags & v)).lower() + '\n'
                for k, v in [
                    ('AllowGetAuthInfo', 1),
                    ('AllowSetAuthInfo', 2),
                    ('AllowSetPassword', 4),
                ]
            ),
            encoding='utf-8',
        )
    cases = [dict(op='get', mode=mode) for mode in range(16)] + [
        dict(op='set', mode=mode, outcome=o)
        for mode, o in itertools.product(range(-1, 16), range(3))
    ]
    seven = '00000000-0000-0000-0000-000000000007'
    bodies = ['', 'LIFETIME=bad', 'METHOD=unknown', 'METHOD[]=authenticate']
    for method, principal, suffix in itertools.product(
        [
            'authenticate',
            'verify',
            'release',
            'getauthinfo',
            'setauthinfo',
            'setpassword',
            'AUTHENTICATE',
        ],
        [
            None,
            'bad',
            seven,
            seven[:-1] + '8',
            seven[:-1] + '9',
            seven[:-1] + '2',
            seven[:-1] + '1',
            seven[:-1] + '4',
            seven[:-1] + '5',
            '00000000-0000-0000-0000-000000000000',
        ],
        [
            '',
            '&PASSWORD=hash&TOKEN=token',
            '&PASSWORD[]=list&TOKEN[]=list&AccountType[]=x&WebLoginKey=',
            '&AccountType=%3C%26%3E&PasswordHash=%E8%A9%A6%E9%A8%93&PasswordSalt=&WebLoginKey=x',
        ],
    ):
        bodies.append(
            'METHOD=' + method + ('' if principal is None else '&PRINCIPAL=' + principal) + suffix
        )
    for lifetime in [
        '-2147483649',
        '-2147483648',
        '-1',
        '0',
        '1',
        '30',
        '31',
        '2147483647',
        '2147483648',
        '',
        '+1',
        '%2B1',
        '%2030%20',
        '30%00',
        '30x',
        '%C2%A030',
        'list',
    ]:
        for method in ['authenticate', 'verify', 'unknown']:
            bodies.append(
                'METHOD='
                + method
                + '&PRINCIPAL='
                + seven
                + '&PASSWORD=x&TOKEN=y&LIFETIME'
                + ('[]' if lifetime == 'list' else '')
                + '='
                + lifetime
            )
    for suffix in [
        '&PASSWORD=first&PASSWORD=last',
        '&PRINCIPAL[]=' + seven,
        '&METHOD[]=verify',
        '&AccountType=one&AccountType=two',
        '&PasswordHash=%FF',
        '&PasswordHash=%uD800',
        '&AccountType=%00%01%0b%1f%ef%bf%be%ef%bf%bf',
    ]:
        bodies.append('METHOD=setauthinfo&PRINCIPAL=' + seven + suffix)
    raw = [s.encode() for s in bodies]
    for encoding, bom in [
        ('utf-8', b'\xef\xbb\xbf'),
        ('utf-16-le', b'\xff\xfe'),
        ('utf-16-be', b'\xfe\xff'),
        ('utf-32-le', b'\xff\xfe\0\0'),
        ('utf-32-be', b'\0\0\xfe\xff'),
    ]:
        raw.append(
            bom
            + ('METHOD=setauthinfo&PRINCIPAL=' + seven + '&AccountType=Ã¨Â©Â¦Ã©Â¨â€œ').encode(
                encoding
            )
        )
    for flags, body in itertools.product(range(8), raw):
        cases.append(dict(op='endpoint', flags=flags, path='/auth/plain', body=body.hex()))
    for path, body in itertools.product(
        [
            '/auth',
            '/auth/',
            '/auth/plain/extra',
            '/authplain',
            '/auth?plain',
            '/auth&plain',
            '/auth//plain',
            '/auth/Plain',
            '/auth/crypt',
            '/auth/crypt/plain',
            '/other/plain',
        ],
        raw[::19],
    ):
        cases.append(dict(op='endpoint', flags=7, path=path, body=body.hex()))
    commands = (
        '\n'.join(json.dumps(c, ensure_ascii=False, separators=(',', ':')) for c in cases) + '\n'
    )
    rows = []
    for command in [[a.dotnet, str(w / 'app/AuthenticationEndpoint.dll')], [a.native, str(config)]]:
        r = subprocess.run(
            command, input=commands, capture_output=True, text=True, encoding='utf-8', timeout=120
        )
        assert r.returncode == 0, r.stdout[-1000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases), (len(rows[0]), len(rows[1]), len(cases))
    differences = [dict(case=c, source=s, native=n) for c, s, n in zip(cases, *rows) if s != n]
    report = dict(
        passed=not differences,
        observations=len(cases),
        authInfoObservations=67,
        endpointObservations=len(cases) - 67,
        sourceHashes={
            f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest()
            for f in [handler, service, utils, model, data]
        },
        scope='Original AuthInfo model, exact extracted GetAuthInfo/SetAuthInfo, handler request/result methods and query/XML helpers. Instrumented service/database/logger callbacks; constructor/plugin lifecycle, exact logger failures and general stream/runtime/platform behavior remain pending.',
        differences=differences[:6],
    )
    (w / 'authentication-endpoint-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:2], indent=2)
    print(f'PASS: {len(cases)} original auth-info/handler observations')


if __name__ == '__main__':
    main()
