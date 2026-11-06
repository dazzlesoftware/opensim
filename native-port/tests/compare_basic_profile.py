"""Compare all operations of the unchanged original UserProfilesService."""

from pathlib import Path
import argparse, subprocess, json, hashlib, xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
sources = [
    'native-port/tests/basic_profile_reference.cs',
    'OpenSim/Region/CoreModules/Avatar/Profile/BasicProfileModule.cs',
    'OpenSim/Region/Framework/Interfaces/IProfileModule.cs',
]
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
for f in sources:
    ET.SubElement(items, 'Compile', Include=str(root / f))
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData', 'Nini']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / str(name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr


def text(value):
    return '-' if value is None else value.encode('utf-16-le', 'surrogatepass').hex() or '.'


names = [
    (None, None),
    ('First', 'Last'),
    ('é 😀', 'second'),
    ('x\x00y', 'z'),
    ('x\ud800q', 'last'),
    ('\udc00', '\ud800'),
    ('A' * 260, 'B' * 260),
]
flows = [
    'Q I A B E V Q R Q A E V C V P L',
    'J A E V Q I B F V R Q',
    'I N N Q U A E V B F V R T Q Z',
    'I A A E E V R A E V T Q',
]
commands = [
    f'{mode} {flags} {created} {text(first)} {text(last)} ' + flow
    for mode in range(3)
    for flags in [0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 24, 384]
    for created in [0, -1, -2147483648, 2147483647, 951827696]
    for first, last in names
    for flow in flows
]
byte_inputs = [
    None,
    '',
    '\x00',
    '\x00\x00',
    'abc',
    'a\x00b',
    'a\x00b\x00',
    'a\ud800b',
    '\udc00',
    '\ud800',
    '\ud800\udc00',
    '\udc00\x00',
]
byte_inputs += [chr(c) for c in range(65536)]
commands += ['B ' + text(value) for value in byte_inputs]
outputs = {}
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    with (w / (name + '.txt')).open('w', encoding='utf-8') as output:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
            timeout=180,
        )
    assert r.returncode == 0, r.stderr
    outputs[name] = (w / (name + '.txt')).read_text(encoding='utf-8').splitlines()
    assert len(outputs[name]) == len(commands), (name, len(outputs[name]), len(commands))
diffs = [
    dict(command=c, source=x, native=y)
    for c, x, y in zip(commands, outputs['reference'], outputs['native'])
    if x != y
]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differenceCount=len(diffs),
    differences=diffs[:12],
    applicationParityVerified=False,
    scope='Unchanged BasicProfileModule and IProfileModule with actual SDK/Nini. Consumed Scene/client/account/event interfaces controlled, exact lifecycle/retained subscriptions/first-scene account routing, properties/interests send order/fields, born dates and raw SDK membership bytes. Direct SDK StringToBytes compared across all BMP code units plus surrogate/NUL/name cases. Native viewer UDP/scene binding and generic managed graph collection remain pending; full application/macOS incomplete.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' basic profile differences; see results.json'
print('PASS:', len(commands), 'original basic profile scenarios')
