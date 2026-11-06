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
import struct, math, random

cases = []


def add(b):
    cases.append(b)


def n(i):
    return struct.pack('>i', i)


def sized(tag, b):
    return tag + n(len(b)) + b


values = [
    b'!',
    b'0',
    b'1',
    b'i' + n(-2147483648),
    b'i' + n(2147483647),
    b'u' + bytes(range(16)),
    sized(b'b', bytes(range(256))),
]
for text in [
    b'',
    b'hello',
    b'a\x00b',
    'é中😀'.encode(),
    b'\xff',
    b'\xed\xa0\x80',
    b'\xf0\x80\x80\x80',
    b'\xe2\x82',
    b'\xef\xbb\xbf',
]:
    values.append(sized(b's', text))
for text in [
    b'',
    b'a/b',
    b'http://example.com/',
    b'http://[bad',
    b'urn:test',
    b'http://example.com/a b',
    'http://example.com/中?q=é'.encode(),
]:
    values.append(sized(b'l', text))
for value in [
    0.0,
    -0.0,
    1.0,
    -1.0,
    math.pi,
    1e-7,
    1e-8,
    float('inf'),
    -float('inf'),
    float('nan'),
    -62135596800.0,
    253402300799.99997,
    253402300800.0,
    315537897600.0,
    315537897601.0,
    -62135596800.01,
]:
    values.append(b'r' + struct.pack('>d', value))
    values.append(b'd' + struct.pack('<d', value))
for count in [-2147483648, -1, 0, 1, 2, 17]:
    for end in [b'', b']', b'}', b' ]', b' }']:
        values.extend([b'[' + n(count) + end, b'{' + n(count) + end])
for tag in [b's', b'l', b'b']:
    for size in [-2147483648, -1, 0, 1, 2, 8, 100]:
        values.append(tag + n(size) + b'ab')
for value in values.copy():
    values.append(b'[' + n(1) + value + b']')
    values.append(b'{' + n(1) + sized(b'k', b'x') + value + b'}')
values.extend(
    [
        b'{'
        + n(3)
        + sized(b'k', b'a')
        + b'1'
        + sized(b'k', b'b')
        + b'0'
        + sized(b'k', b'a')
        + b'!',
        b'{' + n(2) + sized(b'k', b'\xff') + b'1' + sized(b'k', b'\xfe') + b'0' + b'}',
    ]
)
for value in values:
    for header in [
        b'',
        b'<? llsd/binary ?>\n',
        b'<?llsd/binary?>',
        b'<? LLSD/BINARY ?>',
        b' \t\r\n<? llsd/binary ?>\n',
    ]:
        add(header + value)
    for i in range(len(value)):
        add(value[:i])
    for suffix in [b'junk', b'\x00', b' ']:
        add(value + suffix)
for marker in range(256):
    add(bytes([marker]))
rng = random.Random(911)
for i in range(3000):
    raw = bytes(rng.randrange(256) for _ in range(rng.randrange(10)))
    add(sized(b's', raw))
    add(b'd' + struct.pack('<d', rng.uniform(-62135596800, 253402300799)))
rows = '\n'.join(x.hex() for x in cases) + '\n'
outputs = {}
for name, cmd in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll'), '--binary']),
    ('native', [a.native, '--binary']),
]:
    r = subprocess.run(cmd, input=rows, capture_output=True, text=True, timeout=180)
    assert r.returncode == 0, r.stderr
    outputs[name] = r.stdout.splitlines()
    (work / (name + '.txt')).write_text(r.stdout, encoding='utf-8')
    assert len(outputs[name]) == len(cases), (name, len(outputs[name]), len(cases))
diffs = [
    dict(index=i, input=cases[i].hex(), reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not diffs,
    scenarios=len(cases),
    differenceCount=len(diffs),
    differences=diffs[:30],
    applicationParityVerified=False,
    scope='Actual bundled SDK DeserializeLLSDBinary(Stream): typed ordered trees, scalar bits, UTF8 replacement, URI, date ticks/kind, headers, whitespace, malformed/truncated input and exceptions. Seekable memory streams; full SDK/platform/application parity incomplete.',
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert not diffs, str(len(diffs)) + ' binary LLSD differences; see results.json'
print('PASS:', len(cases), 'original SDK LLSD binary scenarios')
