"""Actual bundled Nwc.XmlRpc response deserializer and fault accessors."""

from pathlib import Path
import argparse, subprocess, json, hashlib, xml.etree.ElementTree as ET, random

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
sources = ['native-port/tests/nwc_xml_rpc_reference.cs', 'bin/XMLRPC.dll']
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
ET.SubElement(items, 'Compile', Include=str(root / sources[0]))
ref = ET.SubElement(items, 'Reference', Include='XMLRPC')
ET.SubElement(ref, 'HintPath').text = str(root / 'bin/XMLRPC.dll')
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
enc = lambda s: '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


def response(v):
    return '<methodResponse><params><param>' + v + '</param></params></methodResponse>'


def value(v):
    return '<value>' + v + '</value>'


def struct(fields):
    return (
        '<struct>'
        + ''.join('<member><name>' + k + '</name>' + value(v) + '</member>' for k, v in fields)
        + '</struct>'
    )


scalars = [
    '',
    'text',
    ' ',
    '\t\r\n',
    'a&amp;b',
    'a<!--x-->b',
    'a<![CDATA[b]]>c',
    '<![CDATA[x]]>',
    '<string/>',
    '<string></string>',
    '<nil/>',
    '<unknown>x</unknown>',
]
for tag in ['int', 'i4', 'boolean', 'double', 'dateTime.iso8601', 'base64', 'string']:
    for text in [
        '',
        '0',
        '1',
        '2',
        '-1',
        ' +12 ',
        '32767',
        '32768',
        '-32769',
        '2147483647',
        '2147483648',
        '999999999999999999999999',
        'NaN',
        'Infinity',
        '-Infinity',
        '1.5',
        '1,000',
        '1E20',
        '\u200012',
        '20000229T23:59:59',
        '20230229T01:02:03',
        '00010101T00:00:00',
        '99991231T23:59:59',
        '20240101T1:02:03',
        'AA==',
        'AB==',
        'AAA=',
        'AAB=',
        'AAAA',
        'A===',
        'AA==AA==',
        'AA== \t',
        'a&amp;b',
    ]:
        scalars.append(f'<{tag}>{text}</{tag}>')
    scalars.append(f'<{tag}/>')
for year in [0, 1, 1900, 2000, 2023, 9999]:
    for month in [0, 1, 2, 12, 13]:
        for day in [0, 1, 28, 29, 30, 31, 32]:
            scalars.append(
                f'<dateTime.iso8601>{year:04d}{month:02d}{day:02d}T12:03:04</dateTime.iso8601>'
            )
for hour in [0, 12, 23, 24, 99]:
    for minute in [0, 59, 60, 99]:
        for second in [0, 59, 60, 99]:
            scalars.append(
                f'<dateTime.iso8601>20161231T{hour:02d}:{minute:02d}:{second:02d}</dateTime.iso8601>'
            )
scalars += [
    '<array/>',
    '<array></array>',
    '<array><data/></array>',
    '<array><data></data></array>',
    '<struct/>',
    '<struct></struct>',
]
docs = [
    None,
    '',
    ' ',
    '<methodResponse/>',
    '<unrelated/>',
    '<param/>',
    '<param>' + value('x') + '</param>',
    '<methodResponse><params/></methodResponse>',
]
docs += [response(value(x)) for x in scalars]
for x in scalars[::5]:
    docs.extend(
        [
            response(value('<array><data>' + value(x) + value(x) + '</data></array>')),
            response(value(struct([('a', x), ('b', '<string>z</string>')]))),
            response(value(struct([('same', x), ('same', x)]))),
        ]
    )
for x in [
    '<array/>',
    '<array></array>',
    '<struct/>',
    '<struct></struct>',
    '<string/>',
    '<int>7</int>',
    '',
]:
    for y in ['<value/>', value('z'), value('<struct/>'), value('<array/>')]:
        docs.append(response(value(x + y)))
fault_values = [
    struct([('faultCode', '<int>9</int>'), ('faultString', '<string>fault 😀</string>')]),
    '<string>x</string>',
    '<int>7</int>',
    '<array></array>',
    '<struct/>',
    '<struct></struct>',
    struct([('faultCode', '<string>9</string>'), ('faultString', '<int>3</int>')]),
    struct([('faultCode', ''), ('faultString', '')]),
]
for x in fault_values:
    fault = '<fault>' + value(x) + '</fault>'
    param = '<param>' + value('<int>4</int>') + '</param>'
    docs += [
        '<methodResponse>' + fault + '</methodResponse>',
        '<methodResponse>' + fault + '<params>' + param + '</params></methodResponse>',
        '<methodResponse><params>' + param + '</params>' + fault + '</methodResponse>',
    ]
docs.append(
    response(
        value(
            '<array><data>'
            + value('<base64></base64>')
            + value('<base64> </base64>')
            + value('<base64>AA==</base64>')
            + '</data></array>'
        )
    )
)
docs += [
    response(
        value(
            struct(
                [
                    ('', '<string>x</string>'),
                    ('😀', '<string>y</string>'),
                    ('\ue000', '<string>z</string>'),
                ]
            )
        )
    ),
    response(value('<struct><member>' + value('a') + '</member></struct>')),
    response(value('<struct><member><name></name>' + value('a') + '</member></struct>')),
]
for declaration in [
    '<!DOCTYPE methodResponse>',
    '<!DOCTYPE methodResponse [<!ENTITY x "hi">]>',
    '<?xml version="1.0"?>',
    '<?xml version="1.0" encoding="utf-8"?>',
]:
    for x in ['text', '&x;', '<![CDATA[cdata]]>', 'a\rb\r\nc', 'a&#13;b', '<string>\t</string>']:
        docs.append(declaration + response(value(x)))
docs += [
    '<methodResponse>',
    '<methodResponse></x>',
    response(value('<int>3</int>')) + '<tail/>',
    '<methodResponse>\x00</methodResponse>',
    '<a>&missing;</a>',
    '<a attr="a">text</a>',
    response(value('<string>' + chr(0xD800) + '</string>')),
]
commands = []
for i, doc in enumerate(docs):
    for log in [0, -1, 1, 3, -2, -7, -10, -16, -25]:
        for action in ([0, 1, 2, 3] if '<fault>' in (doc or '') else [0, 3]):
            commands.append(f'{log} {action} {enc(doc)} {enc(docs[(i*17+23)%len(docs)])}')
outputs = {}
for label, cmd in [('source', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    r = subprocess.run(
        cmd,
        input='\n'.join(commands) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=180,
    )
    assert r.returncode == 0, r.stderr + r.stdout[-1000:]
    outputs[label] = r.stdout.splitlines()
    assert len(outputs[label]) == len(commands), (label, len(outputs[label]), len(commands))
diffs = [
    dict(command=c, source=s, native=n)
    for c, s, n in zip(commands, outputs['source'], outputs['native'])
    if s != n
]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differenceCount=len(diffs),
    differences=diffs[:30],
    applicationParityVerified=False,
    scope='Actual bundled XMLRPC.dll Nwc.XmlRpc.XmlRpcResponseDeserializer and response fault accessors with invariant culture. Controlled logger only, including exceptions/reentrant parse. Compare typed values/bits/ticks, collection/binary identity, exact errors, complete log order and sequential reset after success/error. Native source-backed XmlTextReader with legacy DTD.Ignore settings; text/CDATA/whitespace and self-closing quirks, unknown roots/tags, nested containers, numeric/date/base64 and fault/param ordering. Wire serializer/HTTP/XMLRPCRequester and full application/macOS remain pending. Hashtable comparison canonicalizes keys by ordinal order; arbitrary Hashtable enumeration and logger global concurrency remain unproved.',
    sourceHashes={s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not diffs, str(len(diffs)) + ' Nwc XML-RPC differences; see ' + str(w / 'results.json')
print('PASS:', len(commands), 'bundled Nwc XML-RPC scenarios')
