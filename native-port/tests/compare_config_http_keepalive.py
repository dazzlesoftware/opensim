import argparse, json, itertools, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, k).text = v
ET.SubElement(
    ET.SubElement(project, 'ItemGroup'),
    'Compile',
    Include=str(root / 'native-port/tests/config_http_keepalive_reference.cs'),
)
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
enc = lambda s: s.encode('utf-16be', 'surrogatepass').hex() or '-'
rows = []


def add(*values):
    rows.append('|'.join(map(enc, values)))


values = [
    '',
    '0',
    '00',
    '0001',
    '1',
    '2',
    '3',
    '20',
    '2147483647',
    '2147483648',
    '2147484',
    '4294968',
    '+1',
    '-0',
    '-1',
    '1.0',
    '"0"',
    '"2"',
    '""',
    'abc',
    '0\x00',
    ' 2 ',
    '\t2\t',
]
for name, value, prefix, suffix in itertools.product(
    ['timeout', 'TIMEOUT', 'max', 'MAX', 'x'],
    values,
    ['', ' ', '\t'],
    ['', ', timeout=1', ', max=0', '; timeout=1', ','],
):
    add(prefix + name + '=' + value + suffix)
for c in map(chr, range(65536)):
    for value in [
        'timeout=2' + c,
        'timeout' + c + '=1',
        'x="' + c + '",timeout=1',
        'timeout=' + c + '1',
    ]:
        add(value)
for first, last in itertools.product(
    [
        '',
        'timeout=20',
        'timeout=2',
        'max=0',
        'timeout=1',
        'timeout=bad',
        'timeout=2,',
        'timeout=2;max=0',
    ],
    repeat=2,
):
    add(first, last)
rng = random.Random(7541)
parts = [
    'x',
    'timeout',
    'max',
    '0',
    '1',
    '2',
    '20',
    ' ',
    '\t',
    ',',
    ';',
    '=',
    '"',
    '\\',
    '\r',
    '\n',
    '\x00',
    'é',
    '😀',
]
for _ in range(4000):
    add(''.join(rng.choice(parts) for _ in range(rng.randrange(1, 12))))
payload = '\n'.join(rows) + '\n'
outputs = {}
for label, command in [
    ('source', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [a.native, '--probe']),
]:
    r = subprocess.run(
        command, input=payload, capture_output=True, text=True, encoding='utf-8', timeout=180
    )
    assert r.returncode == 0, (label, r.stdout[-1000:], r.stderr)
    outputs[label] = r.stdout.splitlines()
    assert len(outputs[label]) == len(rows)
diffs = [
    dict(index=i, input=rows[i], source=s, native=n)
    for i, (s, n) in enumerate(zip(outputs['source'], outputs['native']))
    if s != n
]
report = dict(
    passed=not diffs,
    cases=len(rows),
    differences=diffs[:100],
    differenceCount=len(diffs),
    scope='Actual .NET8.0.31 private HttpConnection.ProcessKeepAliveHeader state transitions, raw UTF16 units, duplicate fields, token/quoted parameter grammar and numeric boundaries; connection scheduling verified separately.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not diffs, f'{len(diffs)} differences; see {w}/results.json'
print(f'PASS: {len(rows)} Keep-Alive state cases')
