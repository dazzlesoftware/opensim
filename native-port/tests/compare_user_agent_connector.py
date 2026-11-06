"""Original user-agent directory methods, actual SDK boxes, controlled HTTP boundary."""

from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET, itertools, xmlrpc.client, hashlib

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
source = 'OpenSim/Services/Connectors/Hypergrid/UserAgentServiceConnector.cs'
s = (root / source).read_bytes().decode('utf-8-sig')


def method(signature):
    start = s.index(signature)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


signatures = [
    'public UserAgentServiceConnector(string url)',
    'public UserAgentServiceConnector(IConfigSource config)',
    'private bool setServiceURL(string url)',
    'private Hashtable CallServer(string methodName, Hashtable hash)',
    'public Dictionary<string,object> GetUserInfo (UUID userID)',
    'public Dictionary<string, object> GetServerURLs(UUID userID)',
    'public string LocateUser(UUID userID)',
    'public string GetUUI(UUID userID, UUID targetUserID)',
    'public UUID GetUUID(String first, String last)',
    'private bool GetBoolResponse(XmlRpcRequest request, out string reason)',
    'public bool VerifyAgent(UUID sessionID, string token)',
    'public bool VerifyClient(UUID sessionID, string token)',
    'public bool IsAgentComingHome(UUID sessionID, string thisGridExternalName)',
    'public void LogoutAgent(UUID userID, UUID sessionID)',
    'public void SetClientToken(UUID sessionID, string token)',
    'public GridRegion GetHomeRegion(UUID userID, out Vector3 position, out Vector3 lookAt)',
    'public List<UUID> StatusNotification(List<string> friends, UUID userID, bool online)',
    'public List<UUID> GetOnlineFriends(UUID userID, List<string> friends)',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections;using System.Collections.Generic;using System.Net;using GridRegion=OpenSim.Services.Interfaces.GridRegion;using OpenMetaverse;using Nwc.XmlRpc;using Nini.Config;using OpenSim.Framework;using HttpClient=Fixture.Client;using XmlRpcRequest=Fixture.Request;public class UserAgentServiceConnector{static Fixture.Log m_log=new();string m_ServerURL;'
    + ''.join(method(sig) for sig in signatures)
    + 'public bool Bool(out string reason){return GetBoolResponse(new XmlRpcRequest("bool",new ArrayList{new Hashtable()}),out reason);}}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [
    root / 'native-port/tests/user_agent_connector_reference.cs',
    root / 'native-port/tests/user_agent_region_reference_stubs.cs',
    root / 'OpenSim/Services/Interfaces/IGridService.cs',
    root / 'OpenSim/Framework/RegionFlags.cs',
    w / 'Extracted.cs',
]:
    ET.SubElement(items, 'Compile', Include=str(f))
for name in ['XMLRPC', 'OpenMetaverse', 'OpenMetaverseTypes']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / (name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
enc = lambda s: '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


def wire(v):
    return xmlrpc.client.dumps((v,), methodresponse=True, allow_none=True)


rows = []


def add(mode, url, xml, config=0, fail='none', first='First', last='Last'):
    rows.append(' '.join(map(str, [mode, config, fail, enc(url), enc(xml), enc(first), enc(last)])))


url = 'HTTP://Example.TEST/Some/Case?TOKEN=Secret#Fragment'
values = [
    {},
    None,
    'text',
    47,
    True,
    3.5,
    [],
    {
        'flags': 47,
        'created': -1,
        'title': 'name',
        'null': None,
        'nested': [True, 47, 'é😀', {'x': 7}],
        'binary': xmlrpc.client.Binary(b'\x00\xff'),
        'date': xmlrpc.client.DateTime('20240229T12:34:56'),
    },
    {
        'SRV_HomeURI': 'https://Home/Case',
        'SRV_': True,
        'SRV_Number': 47,
        'SRV_List': [],
        'SRV_Date': xmlrpc.client.DateTime('20240229T12:34:56'),
        'SRV_Binary': xmlrpc.client.Binary(b'\x00\xff'),
        'SRV_Empty': None,
        'srv_lower': 'skip',
        'S\u00adRV_Soft': 'soft',
        'SRV\u0000_Zero': 'zero',
    },
    {
        'URL': 'HTTP://UPPER/Path',
        'UUI': 'user;name;home',
        'UUID': '00000000-0000-0000-0000-000000000009',
    },
    {'URL': None, 'UUI': None, 'UUID': None},
    {'URL': 47, 'UUI': True, 'UUID': 47},
    {'UUID': 'bad uuid'},
    {'UUID': ' 00000000000000000000000000000009 '},
    {'UUID': '00000000-0000-0000-0000-000000000009 suffix'},
]
values += [
    {
        'S\u00adRV_Name': 'soft',
        'SRV\u200d_A': 'join',
        'S\ufeffRV_Test': 'bom',
        'SR\u034fV_Name': 'combiner',
        'SRV_Good': 9,
    },
    {'SRV\u00ad_foo': 'first', 'SRV__foo': 'second'},
]
xmls = [wire(v) for v in values] + [
    xmlrpc.client.dumps(xmlrpc.client.Fault(7, 'remote fault')),
    '<methodResponse><fault><value><struct/></value></fault></methodResponse>',
    '<methodResponse><params><param><value><int>bad</int></value></param></params></methodResponse>',
    '<bad',
    'NULL',
    None,
]
# NUL in XML member names is rejected by both actual readers, retaining logs.
for mode, xml, fail in itertools.product(
    range(5), xmls, ['none', 'factory', 'send', 'close', 'debug']
):
    add(mode, url, xml, fail=fail)
for address in [
    None,
    '',
    'relative',
    'HTTP://EXAMPLE.test',
    'https://EXAMPLE.test/UPPER/',
    'https://EXAMPLE.test/Path?Q=UPPER#FRAG',
    ' https://EXAMPLE.test/Path ',
    'ftp://HOST/Case',
    'custom:Case',
    'C:\\Folder\\Name',
    '/tmp/Path',
    'http://[bad/',
    'http://host:bad/',
    'http://ÉXAMPLE.test/İΣ😀',
    'http://host/' + chr(0xD800),
    'http://host/' + 'a' * 65520,
]:
    for config in [0, 1]:
        for mode in [0, 5]:
            add(mode, address, wire({'title': 'x'}), config)
for text in ['', ' ', '\t\r\n', '\u0085', '\u00a0', '\u2000', '\u200b', '\u202f', '\ufeff']:
    for fail in ['none', 'debug', 'error']:
        add(5, text, wire({}), 1, fail)
for first, last in [(None, None), ('', ''), ('é😀', chr(0xD800))]:
    add(4, url, wire({'UUID': '00000000-0000-0000-0000-000000000009'}), first=first, last=last)
bools = [
    wire({'result': v})
    for v in [
        None,
        'True',
        'true',
        'FALSE',
        ' false ',
        'TrUe',
        'bad',
        '',
        47,
        True,
        3.5,
        [],
        {},
        xmlrpc.client.Binary(b'a'),
        xmlrpc.client.DateTime('20240229T12:34:56'),
    ]
] + xmls
for t in ['&#0;True&#0;', '&#x85;True&#x2000;', 'True&#0;x', 'falſe', 'ＴＲＵＥ', ' True &#9;']:
    bools.append(
        '<methodResponse><params><param><value><struct><member><name>result</name><value><string>'
        + t
        + '</string></value></member></struct></value></param></params></methodResponse>'
    )
for mode, xml, fail in itertools.product(
    range(6, 12), bools, ['none', 'factory', 'send', 'close', 'debug', 'error', 'warn']
):
    add(mode, url, xml, fail=fail)
for mode, token in itertools.product([7, 8, 9, 11], [None, '', 'token', 'é😀', chr(0xD800)]):
    add(mode, url, wire({'result': 'True'}), first=token)
home = {
    'result': 'True',
    'uuid': '00000000-0000-0000-0000-000000000009',
    'x': '-2147483648',
    'y': '2147483647',
    'size_x': '512',
    'size_y': '1024',
    'region_name': 'home é😀',
    'hostname': 'HOST.test',
    'http_port': '4294967295',
    'server_uri': 'http://HOST.test/Path',
    'internal_port': '65535',
    'position': '<1.25, -2.5, 3.75>',
    'lookAt': '<0, 1, 0>',
}
homes = [wire({'result': 'True'}), wire(home)] + xmls + bools
for key in home:
    for value in [
        None,
        '',
        'bad',
        '0',
        '-0',
        '+47',
        ' 47 ',
        '2147483648',
        '4294967296',
        '-1',
        '65536',
        47,
        True,
        3.5,
        [],
        {},
    ]:
        row = dict(home)
        row[key] = value
        homes.append(wire(row))
for vector in [
    '1, 2, 3',
    '<NaN, Infinity, -Infinity>',
    '<1, 2, 3>trailing',
    '<1, 2>',
    '<1, x, 3>',
    '1.0,2.0,3.0',
    '<0, -0, 1e-45>',
]:
    row = dict(home)
    row['position'] = vector
    homes.append(wire(row))
for xml, fail in itertools.product(homes, ['none', 'factory', 'send', 'close', 'debug']):
    add(12, url, xml, fail=fail)
friends = [
    wire(
        {
            'friend_0': '00000000-0000-0000-0000-000000000009',
            'friend_1': 'bad',
            'friend_2': None,
            'friend_3': '00000000000000000000000000000008',
            'not_friend': '00000000000000000000000000000007',
        }
    ),
    wire(
        {
            'friend_0': '00000000-0000-0000-0000-000000000009',
            'friend_1': '__throw__',
            'friend_2': '00000000-0000-0000-0000-000000000008',
        }
    ),
    wire({'friend_0': '__null__', 'friend_1': '00000000-0000-0000-0000-000000000008'}),
    wire(
        {'f\u00adriend_0': '00000000-0000-0000-0000-000000000009', 'friend_1': 47, 'friend_2': True}
    ),
] + xmls
for mode, xml, fail in itertools.product(
    [13, 14, 15], friends, ['none', 'factory', 'send', 'close', 'debug', 'error', 'text']
):
    add(mode, url, xml, fail=fail)
for mode, first, last in itertools.product(
    [13, 14, 15], [None, 'empty', '', 'friend é😀'], [None, '', 'other']
):
    add(mode, url, friends[0], first=first, last=last)


def run(cmd):
    r = subprocess.run(
        cmd, input='\n'.join(rows) + '\n', capture_output=True, text=True, encoding='utf-8'
    )
    assert r.returncode == 0, r.stdout + r.stderr
    out = r.stdout.splitlines()
    assert len(out) == len(rows), (len(out), len(rows))
    return out


expected = run([a.dotnet, str(w / 'app/Reference.dll')])
actual = run([a.native])
for i, (e, n) in enumerate(zip(expected, actual)):
    if e != n:
        (w / 'failure.json').write_text(
            json.dumps(dict(index=i, input=rows[i], expected=e, actual=n), indent=2)
        )
        raise AssertionError(
            f'mismatch {i}: {rows[i][:200]}\nexpected {e[:2000]}\nactual {n[:2000]}'
        )
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=True,
            cases=len(rows),
            scope='Unchanged original URI/config constructors, setServiceURL, CallServer/five directory methods plus GetBoolResponse/VerifyAgent/VerifyClient/IsAgentComingHome/LogoutAgent/SetClientToken. GetHomeRegion uses unchanged full GridRegion class and actual SDK Vector3 with exact region/default/endpoint/vector/partial-out state; StatusNotification/GetOnlineFriends preserve partial ordered UUID lists under controlled Hashtable.Keys enumeration and custom throwing/null ToString boxes. Boolean reasons and logging/cast/dispose precedence, actual Nwc response parser/boxes and SDK UUID; controlled global client/failure/logger boundaries. Exact outcomes, retained boxed references, server URL and call/log/dispose traces. Full connector/HTTP/host parity not claimed',
            sourceSHA256=hashlib.sha256((root / source).read_bytes()).hexdigest(),
        ),
        indent=2,
    )
)
print(f'User-agent connector {len(rows)} cases passed')
