"""Compare the native RegexCharClass matching kernel with actual .NET8.0.31."""

from pathlib import Path
import argparse, hashlib, json, random, subprocess, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native')
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--prepare', action='store_true')
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
ET.SubElement(items, 'Compile', Include=str(lab / 'dotnet_regex_char_class_reference.cs'))
ET.ElementTree(project).write(work / 'Reference.csproj')
result = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
reference = [a.dotnet, str(work / 'app/Reference.dll')]
subprocess.run(reference + ['--export-category', str(work / 'categories.bin')], check=True)
if a.prepare:
    raise SystemExit(0)
assert a.native, '--native is required unless preparing the category dump'


def encode(text):
    return text.encode('utf-16-be', 'surrogatepass').hex() or '.'


def cls(ranges=(), categories=(), negative=False, subtraction=''):
    return (
        chr(int(negative))
        + chr(len(ranges))
        + chr(len(categories))
        + ''.join(chr(v & 65535) for v in [*ranges, *categories])
        + subtraction
    )


def run(command, rows, name):
    result = subprocess.run(
        command, input='\n'.join(rows) + '\n', capture_output=True, text=True, timeout=180
    )
    (work / (name + '.txt')).write_text(result.stdout)
    (work / (name + '-error.txt')).write_text(result.stderr)
    assert result.returncode == 0, (name, result.stderr)
    output = result.stdout.splitlines()
    assert len(output) == len(rows), (name, len(output), len(rows))
    return output


patterns = [
    r'[a-z]',
    r'[a-z-[aeiou]]',
    r'[^a-z-[A-Z]]',
    r'\w',
    r'\W',
    r'\d',
    r'\D',
    r'\s',
    r'\S',
    r'[\p{Lu}\p{Ll}]',
    r'[\p{L}-[A-Z]]',
    r'[^\p{L}-[\p{N}]]',
    r'[\p{IsBasicLatin}\p{IsGreek}]',
    r'[\uD800-\uDFFF]',
    r'[\x00-\uffff]',
    r'[a-z-[d-f-[e]]]',
    r'[^\p{M}\p{Zs}]',
] + [
    r'\p{' + name + '}'
    for name in [
        'Lu',
        'Ll',
        'Lt',
        'Lm',
        'Lo',
        'Mn',
        'Mc',
        'Me',
        'Nd',
        'Nl',
        'No',
        'Pc',
        'Pd',
        'Ps',
        'Pe',
        'Pi',
        'Pf',
        'Po',
        'Sm',
        'Sc',
        'Sk',
        'So',
        'Zs',
        'Zl',
        'Zp',
        'Cc',
        'Cf',
        'Cs',
        'Co',
        'Cn',
        'L',
        'M',
        'N',
        'P',
        'S',
        'Z',
        'C',
    ]
]
parser_rows = [
    f'P {encode(pattern)} {options}' for pattern in patterns for options in [0, 1, 256, 257]
]
parsed = run(reference, parser_rows, 'parser')
sets = set()
for row in parsed:
    assert row.startswith('P:'), row
    for value in row[2:].split(':'):
        if value:
            sets.add(bytes.fromhex(value).decode('utf-16-be', 'surrogatepass'))
parser_set_count = len(sets)
for value in range(1, 31):
    for sign in [1, -1]:
        sets.add(cls(categories=[sign * value]))
for value in [100, -100]:
    sets.add(cls(categories=[value]))
for values in [
    [0, 1, 2, 3, 4, 5, 0],
    [0, -1, -2, -3, -4, -5, 0],
    [1, 2, 3],
    [0, 1, 2, 0, 0, -19, -20, -21, 0],
]:
    sets.add(cls(categories=values))
    sets.add(cls(categories=values, negative=True))
for bounds in [
    [],
    [0],
    [0, 1],
    [0, 65535],
    [65535],
    [1, 2, 3, 4],
    [0xD800, 0xE000],
    [ord('A'), ord('Z') + 1],
]:
    for negative in [False, True]:
        sets.add(cls(bounds, negative=negative))
        sets.add(cls(bounds, categories=[10], negative=negative))
all_chars = cls([0])
empty = cls()
vowels = cls([97, 98, 101, 102, 105, 106, 111, 112, 117, 118])
for base in [
    all_chars,
    cls([97, 123]),
    cls(categories=[0, 1, 2, 3, 4, 5, 0]),
    cls(categories=[100]),
]:
    for subtract in [empty, all_chars, vowels, cls([97, 123], subtraction=vowels)]:
        sets.add(base + subtract)
rng = random.Random(890031)
for i in range(24):
    bounds = sorted(rng.sample(range(65536), rng.randrange(1, 18)))
    sets.add(
        cls(
            bounds,
            negative=bool(i % 2),
            subtraction=cls(categories=[rng.randrange(1, 31)]) if i % 3 == 0 else '',
        )
    )
sets = sorted(sets)
rows = ['W'] + ['C ' + encode(value) for value in sets]
for set in sets[:20]:
    for code in [0, 15, 16, 31, 65, 127, 128, 0xFFFF]:
        for initial in ['-', '.', ','.join(['00000000'] * 8)]:
            rows.append(f'L {code} {encode(set)} {initial}')
# Malformed serialized classes, including category-group and slice failures.
bad = [
    '',
    '\0',
    '\0\0',
    cls(categories=[0]),
    cls(categories=[0, 1]),
    '\0\0\u0002\u0001',
    '\0\u0002\0a',
]
for value in bad:
    for code in [0, 65, 127, 128]:
        for initial in ['-', '.', ','.join(['00000000'] * 8)]:
            rows.append(f'L {code} {encode(value)} {initial}')
# Cache lookup precedes Unicode dispatch and can bypass invalid class data.
for code, length, index, word in [
    (0, 1, 0, 3),
    (15, 1, 0, 0xC0000000),
    (128, 9, 8, 3),
    (65535, 4096, 4095, 0xC0000000),
]:
    words = [0] * length
    words[index] = word
    rows.append(f'L {code} . ' + ','.join(f'{v:08x}' for v in words))
for set in [all_chars, empty, vowels, cls(categories=[100]), cls(categories=[0, 1, 2, 3, 4, 5, 0])]:
    rows.append('T ' + encode(set))
(work / 'input.txt').write_text('\n'.join(rows) + '\n')
expected = run(reference, rows, 'reference')
actual = run([str(Path(a.native).resolve())], rows, 'native')
differences = [
    dict(index=i, input=rows[i], reference=x, native=y)
    for i, (x, y) in enumerate(zip(expected, actual))
    if x != y
]
report = dict(
    passed=not differences,
    classCount=len(sets),
    parserProducedClassCount=parser_set_count,
    classMembershipObservations=len(sets) * 65536,
    unicodeWordObservations=65536,
    cacheAndErrorFlows=len(rows) - len(sets) - 1,
    concurrentCacheFlows=5,
    differences=len(differences),
    firstDifferences=differences[:12],
    applicationParityVerified=False,
    scope='Actual pinned RegexCharClass serialized membership/subtraction, Unicode/ECMA/boundary predicates, ASCII lazy cache and malformed input errors. Native kernel registered for future parser/runtime; full regex and login std::regex replacement still incomplete.',
    categorySHA256=hashlib.sha256((work / 'categories.bin').read_bytes()).hexdigest(),
)
(work / 'results.json').write_text(json.dumps(report, indent=2))
assert report['passed'], report
print(
    f'PASS: {len(sets)*65536} .NET class membership observations, {len(rows)-len(sets)-1} cache/error flows'
)
