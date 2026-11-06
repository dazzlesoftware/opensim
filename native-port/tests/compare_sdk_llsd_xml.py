# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
from pathlib import Path
import argparse, subprocess, json, hashlib, random, itertools, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for key, value in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, key).text = value
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(root / 'native-port/tests/sdk_llsd_xml_reference.cs'))
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / str(name + '.dll'))
ET.ElementTree(project).write(work / 'Reference.csproj')
run = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
cases = []


def add(value):
    cases.append(value.encode('utf-8') if isinstance(value, str) else value)


# The SDK reads one token past a value, without resolving that token's attributes.
for completed in [
    '<array/>',
    '<array></array>',
    '<array><string>A</string></array>',
    '<map/>',
    '<string>x</string>',
]:
    for attribute in [
        'bad="&unknown;"',
        'xmlns="&unknown;"',
        'xml:space="&unknown;"',
        'bad="A&unknown;Z"',
        'bad="&amp;"',
    ]:
        for ending in ['>', '/>', '></string>']:
            add('<llsd>' + completed + '<string ' + attribute + ending)
scalars = [
    '<undef/>',
    '<undef></undef>',
    '<undef> </undef>',
    '<undef>text</undef>',
    '<foo/>',
    '<foo></foo>',
    '<foo>text</foo>',
]
for tag in ['boolean', 'integer', 'real', 'uuid', 'date', 'string', 'binary', 'uri']:
    scalars.append('<' + tag + '/>')
    for value in [
        '',
        ' ',
        '0',
        '1',
        'true',
        'True',
        'false',
        '-1',
        '2147483648',
        '1,234',
        'NaN',
        'Infinity',
        '∞',
        '+∞',
        '-∞',
        '+nan',
        '-nan',
        '1e309',
        '-1e400',
        'é',
        'a&amp;b',
        '2020-02-29T12:34:56.1234567Z',
        '00000000-0000-0000-0000-000000000001',
        'a/b',
        'http://example.com/中?q=é',
        'aGVsbG8=',
        'http://[bad',
    ]:
        scalars.append('<' + tag + '>' + value + '</' + tag + '>')
for value in scalars:
    for wrapper in [
        '<llsd>{}</llsd>',
        '<other>{}</other>',
        '<?xml version="1.0"?><llsd>{}</llsd>',
        '<!--comment--><llsd>{}</llsd>',
        '<?pi data?><llsd>{}</llsd>',
        '<llsd><!--comment-->{}<!--end--></llsd>',
    ]:
        add(wrapper.format(value))
    add('<llsd><map><key>x</key>' + value + '</map></llsd>')
    add('<llsd><array>' + value + '</array></llsd>')
for body in [
    '',
    'bad',
    '{}',
    '<llsd/>',
    '<llsd><map/></llsd>',
    '<llsd><array/></llsd>',
    '<llsd><map><key/><string>v</string></map></llsd>',
    '<llsd><map><key></key><string>v</string></map></llsd>',
    '<llsd><map><key>x</key><integer>1</integer><key>x</key><integer>2</integer></map></llsd>',
    '<llsd><map/><array/></llsd>',
    '<llsd><map><key>a<![CDATA[b]]>c</key><string>x<![CDATA[y]]>z</string></map></llsd>',
    '<!DOCTYPE llsd [<!ENTITY x "y">]><llsd><string>&x;</string></llsd>',
]:
    add(body)
    for encoding in ['utf-8-sig', 'utf-16', 'utf-32', 'utf-16-be', 'utf-32-be']:
        raw = body.encode(encoding)
        if encoding.endswith('-be'):
            raw = (b'\xfe\xff' if encoding == 'utf-16-be' else b'\x00\x00\xfe\xff') + raw
        add(raw)
for count in [1, 2, 5, 20, 70]:
    add(
        '<llsd>'
        + '<array>' * count
        + '<map><key>x</key><integer>3</integer></map>'
        + '</array>' * count
        + '</llsd>'
    )
for unit in range(65536):
    for body in [
        '<string>&#' + str(unit) + ';</string>',
        '<map><key>&#'
        + str(unit)
        + ';</key><integer>1</integer><key>&#65533;</key><string>v</string></map>',
        '<uri>&#' + str(unit) + ';</uri>',
    ]:
        add('<llsd>' + body + '</llsd>')
payload = '\n'.join(body.hex() for body in cases) + '\n'
(work / 'input.txt').write_text(payload, encoding='utf-8')
outputs = {}
for name, command in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    run = subprocess.run(
        command, input=payload, capture_output=True, text=True, encoding='utf-8', timeout=150
    )
    assert run.returncode == 0, (name, run.stderr)
    (work / (name + '.txt')).write_text(run.stdout, encoding='utf-8')
    outputs[name] = run.stdout.splitlines()
    assert len(outputs[name]) == len(cases), (name, len(outputs[name]), len(cases))
differences = [
    dict(index=i, input=cases[i].hex(), reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not differences,
    scenarios=len(cases),
    differenceCount=len(differences),
    differences=differences[:8],
    applicationParityVerified=False,
    scope='Actual installed SDK OSDParser.DeserializeLLSDXml(Stream), typed tree/order/null/numeric bits/date raw ticks+kind+binary/URI string. Native legacy XmlTextReader and native URI/date providers. BOM/DTD/cursor/comment/PI/CDATA/duplicate/unknown/malformed forms; OS locale invariant. Full SDK and application parity remain incomplete.',
    sourceSHA256={
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ['bin/OpenMetaverse.StructuredData.dll', 'bin/OpenMetaverseTypes.dll']
    },
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], str(report['differenceCount']) + ' SDK XML differences; see results.json'
print('PASS:', len(cases), 'original SDK LLSD XML scenarios')
