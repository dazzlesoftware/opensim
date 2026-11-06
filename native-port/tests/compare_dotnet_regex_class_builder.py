"""Exercise stateful actual RegexCharClass builders against the native port."""

from pathlib import Path
import argparse, json, hashlib, random, re, subprocess, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
lab = Path(__file__).resolve().parent
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
ET.SubElement(items, 'Compile', Include=str(lab / 'dotnet_regex_class_builder_reference.cs'))
ET.ElementTree(project).write(work / 'Reference.csproj')
build = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(build.stdout + build.stderr)
assert build.returncode == 0, build.stdout + build.stderr


def enc(s):
    return s.encode('utf-16-be', 'surrogatepass').hex() or '.'


def row(*ops):
    return ';'.join(ops)


def category(name, invert=0, ci=0, pattern='[test]', position=3):
    return f'G 0 {enc(name)} {invert} {ci} {enc(pattern)} {position}'


cases = []
for code in range(65536):
    cases.append(row('N 0', f'A 0 {code}', 'O 0'))
for start, end in [
    (0, 65535),
    (0, 65534),
    (1, 65535),
    (0, 0),
    (65535, 65535),
    (9, 13),
    (97, 122),
    (200, 100),
    (65535, 0),
]:
    for negate in [0, 1]:
        cases.append(
            row('N 0', f'R 0 {start} {end}', f'V 0 {negate}', 'O 0', 'O 0', 'A 0 65', 'O 0')
        )
for code in [0, 9, 32, 65, 73, 97, 105, 0x130, 0x131, 0xD800, 0xFFFF]:
    for name in ['', 'en-US', 'tr-TR', 'az-Latn-AZ']:
        cases.append(
            row(
                'N 0',
                f'A 0 {code}',
                f'E 0 {enc(name)}',
                'O 0',
                f'E 0 {enc("tr-TR" if name=="" else "")}',
                'O 0',
            )
        )
for op in ['W', 'S', 'D']:
    for ecma in [0, 1]:
        for invert in [0, 1]:
            cases.append(row('N 0', f'{op} 0 {ecma} {invert}', 'O 0', 'R 0 0 65535', 'O 0'))
# Pinned data has all property/category names; source decides validity itself.
runtime_data = subprocess.run(
    [a.dotnet, str(work / 'app/Reference.dll'), '--names'],
    capture_output=True,
    text=True,
    check=True,
)
(work / 'runtime-data.json').write_text(runtime_data.stdout)
data = json.loads(runtime_data.stdout)
assert len(data['categories']) == 38 and len(data['properties']) == 112
names = (
    list(data['categories'])
    + list(data['properties'])
    + ['', 'bad', 'lu', 'IsbasicLatin', '__InternalRegexIgnoreCase__', 'é\ud800']
)
for name in names:
    for invert in [0, 1]:
        for ci in [0, 1]:
            cases.append(row('N 0', category(name, invert, ci), 'O 0', 'A 0 73', 'E 0 .', 'O 0'))
# Hole canonicalization includes both category-hit and category-miss paths.
for code in [1, 9, 32, 48, 65, 97, 0x200C, 0xD800, 65534]:
    for name in ['Lu', 'Nd', 'Cs', 'L', 'Z', 'Cc']:
        cases.append(
            row('N 0', f'R 0 0 {code-1}', f'R 0 {code+1} 65535', category(name), 'O 0', 'O 0')
        )
for neg in [0, 1]:
    for subtract in [0, 1]:
        cases.append(
            row(
                'N 0',
                'N 1',
                'N 2',
                'A 0 97',
                'W 1 0 0',
                f'V 1 {neg}',
                *(['X 1 2'] if subtract else []),
                'M 0 1 1',
                'O 0',
                'M 0 1 0',
                'O 0',
                'M 0 0 0',
                'O 0',
            )
        )
cases.extend(
    [
        row(
            'N 0',
            'N 1',
            'N 2',
            'R 0 0 65535',
            'R 1 97 122',
            'X 0 1',
            'O 0',
            'A 1 65',
            'O 0',
            'X 1 2',
            'O 0',
            'X 0 -1',
            'O 0',
        ),
        row('N 0', 'N 1', 'A 1 65', 'X 0 1', 'N 1', 'A 1 66', 'O 0'),
    ]
)
for first, last in [
    (0, 65535),
    (0, 65534),
    (1, 65535),
    (65, 90),
    (0x130, 0x131),
    (0xD800, 0xDFFF),
    (0xFB00, 0xFFFF),
]:
    for name in ['', 'en-US', 'tr-TR']:
        cases.append(
            row(
                'N 0',
                f'R 0 {first} {last}',
                f'E 0 {enc(name)}',
                'O 0',
                f'E 0 {enc("tr-TR")}',
                'O 0',
            )
        )
for name in ['Lu', 'Nd']:
    cases.append(row('N 0', 'R 0 0 64', 'R 0 66 65535', f'B 0 {enc(name)} 65536', 'O 0', 'O 0'))
cases.append(row('N 0', 'U 0', 'O 0'))
runtime_classes = subprocess.run(
    [a.dotnet, str(work / 'app/Reference.dll'), '--classes'],
    capture_output=True,
    text=True,
    check=True,
)
(work / 'runtime-classes.json').write_text(runtime_classes.stdout)
classes = sorted(json.loads(runtime_classes.stdout))
assert len(classes) == 67
for value in classes:
    cases.append(row('P 0 ' + value, 'O 0', 'A 0 73', 'E 0 .', 'O 0'))
for value in [
    '',
    '\0',
    '\0\0',
    '\0\0\0',
    '\0\u0001\0\uffff',
    '\0\u0002\0AZ',
    '\0\0\u0001\u0001',
    '\1\u0002\0AZ\0\0\0',
]:
    cases.append(row('N 0', f'P 0 {enc(value)}', 'O 0'))
for ranges in ['', '\0', '\0\0', '\0\0AZ', 'AZ', '\uffff']:
    for categories in ['', '\0\u0001\u0002\0', '\uffff']:
        cases.append(f'C {enc(ranges)} {enc(categories)}')
rng = random.Random(8031)
for count in [2, 3, 4, 15, 16, 17, 31, 32, 33, 127]:
    for repeat in range(16):
        ops = ['N 0']
        for i in range(count):
            first = rng.randrange(65536) if repeat % 2 else rng.randrange(8) * 100
            last = (
                rng.randrange(65536) if repeat % 3 == 0 else min(65535, first + rng.randrange(256))
            )
            ops.append(f'R 0 {first} {last}')
        cases.append(row(*ops, 'O 0', 'O 0'))
(work / 'input.txt').write_text('\n'.join(cases) + '\n')
outputs = {}
for name, command in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    result = subprocess.run(
        command, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=180
    )
    (work / (name + '.txt')).write_text(result.stdout)
    (work / (name + '-error.txt')).write_text(result.stderr)
    assert result.returncode == 0, (name, result.stderr)
    outputs[name] = result.stdout.splitlines()
    assert len(outputs[name]) == len(cases), (name, len(outputs[name]), len(cases))
differences = [
    dict(index=i, input=cases[i], reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not differences,
    cases=len(cases),
    differences=len(differences),
    firstDifferences=differences[:12],
    applicationParityVerified=False,
    scope='Actual RegexCharClass builder state/serialization/category-property maps/merge and live subtraction graph, culture case expansion and exact parse errors versus native port. UInt16 singletons plus stateful boundary, equal-key/reversed sort, category and alias cases. Regex parser/runner and login std::regex replacement remain incomplete.',
    normalizedOutputSHA256={
        k: hashlib.sha256(('\n'.join(v) + '\n').encode()).hexdigest() for k, v in outputs.items()
    },
)
(work / 'results.json').write_text(json.dumps(report, indent=2))
assert report['passed'], report
print(f'PASS: {len(cases)} actual .NET RegexCharClass builder flows')
