"""Unchanged OpenProfile client with actual SDK/models; controlled XMLRPCRequester boundary."""

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
sources = [
    'OpenSim/Services/Interfaces/OpenProfileClient.cs',
    'OpenSim/Framework/UserProfiles.cs',
    'native-port/tests/open_profile_reference.cs',
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
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'log4net']:
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
commands = []


def add(mode=7, flags=0, field=-1, kind=1, payload='', uri='http://profile/'):
    commands.append(f'{mode} {flags} {field} {kind} {enc(payload)} {enc(uri)}')


atoms = [(0, None)] + [
    (1, s)
    for s in [
        None,
        '',
        'text 😀<&',
        'a\x00b',
        chr(0xD800),
        chr(0xDC00),
        '0',
        '-1',
        '2147483647',
        '-2147483648',
        '2147483648',
        '-2147483649',
        ' +12 \t',
        '\u200012',
        '1,000',
        '12.0',
        '1e2',
        'True',
        '1\x00',
        '0\x00z',
        '00000000-0000-0000-0000-000000000009',
        '00000000000000000000000000000009',
        ' 00000000-0000-0000-0000-000000000009suffix',
        'bad uuid',
    ]
]
atoms += [(2, s) for s in ['0', '-1', '2147483647', '-2147483648']] + [
    (3, s) for s in ['True', 'False']
]
atoms += [
    (4, s)
    for s in [
        '0',
        '-0',
        '1.5',
        '-31',
        '1E-5',
        '1E16',
        '1E17',
        '1.2345678901234567',
        '1E200',
        'NaN',
        'Infinity',
        '-Infinity',
    ]
]
atoms += (
    [(5, s) for s in ['0', '-1', '9223372036854775807', '-9223372036854775808']]
    + [(k, '') for k in [6, 7, 8]]
    + [
        (9, s)
        for s in ['0', '1', '621355968000000000', '638500815150000000', '3155378975999999999']
    ]
)
for mode in range(8):
    for flags in [0, 1, 2, 4, 8, 4 | 8, 1 | 4, 2 | 4]:
        for uri in [None, '', 'http://profile/', ' \t', 'http://p/😀\x00']:
            add(mode, flags, uri=uri)
for field in range(-2, 11):
    for kind, payload in atoms:
        for flags in [0, 1, 2, 4, 8]:
            add(field=field, kind=kind, payload=payload, flags=flags)
for mode in [5, 6]:
    for kind, payload in atoms:
        for flags in [0, 4, 8]:
            add(mode, flags, kind=kind, payload=payload)
for field in range(11):
    for flags in [0, 4, 16, 4 | 16, 32, 64, 32 | 16, 64 | 16, 4 | 32, 4 | 64]:
        for payload in [None, '', 'text 😀', '-31', '00000000-0000-0000-0000-000000000009']:
            add(flags=flags, field=field, kind=10, payload=payload)
# Additional numeric syntax and SDK prefix paths at each conversion position.
rng = random.Random(75013)
for i in range(1200):
    field = rng.choice([3, 4, 5, 6, 8])
    payload = rng.choice(
        [
            '',
            ' ',
            '+',
            '-',
            '\u00a0',
            '\t',
            '0' * 40,
            '9' * 40,
            str(rng.randrange(-(2**40), 2**40)),
            '00000000-0000-0000-0000-000000000009',
        ]
    ) + rng.choice(['', '\x00', '\x00\x00', 'suffix', '\r\n'])
    add(field=field, payload=payload, flags=rng.choice([0, 4]))
outputs = {}
for label, cmd in [('source', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    r = subprocess.run(
        cmd,
        input='\n'.join(commands) + '\n',
        text=True,
        encoding='utf-8',
        errors='strict',
        capture_output=True,
        timeout=120,
    )
    assert r.returncode == 0, r.stdout[-1000:] + r.stderr
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
    differences=diffs[:16],
    applicationParityVerified=False,
    scope='Entire unchanged OpenProfileClient with original UserProfileProperties and actual SDK UUID. Controlled XMLRPCRequester and boxed values; exact request identity/URI, null and strict data/row casts, empty rows, staged field resets, partial errors, null/ref replacement by requester and reentrant ToString with CLR receiver evaluation/store order, UUID constructor prefix parsing, invariant boxed ToString and Convert.Int32, primitive/map/array/binary/DateTime and custom throwing/null ToString. CLR culture fixed invariant as native host; arbitrary culture/custom object providers, XMLRPCRequester transport/parser and complete advanced host/application/macOS remain pending.',
    sourceHashes={s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not diffs, str(len(diffs)) + ' OpenProfile differences; see ' + str(w / 'results.json')
print('PASS:', len(commands), 'original OpenProfile scenarios')
