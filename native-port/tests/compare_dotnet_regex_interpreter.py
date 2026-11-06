"""Actual private RegexInterpreter/RegexRunner execution and capture history."""

from pathlib import Path
import argparse, hashlib, json, random, subprocess, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
args = p.parse_args()
work = args.work.resolve()
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
ET.SubElement(
    items,
    'Compile',
    Include=str(Path(__file__).with_name('dotnet_regex_interpreter_reference.cs').resolve()),
)
ET.ElementTree(project).write(work / 'Reference.csproj')
result = subprocess.run(
    [args.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
assert result.returncode == 0, result.stdout + result.stderr


def enc(value):
    return '-' if value is None else value.encode('utf-16-be', 'surrogatepass').hex() or '.'


cases = []


def add(
    pattern,
    body,
    options=0,
    culture='',
    begin=0,
    length=None,
    start=None,
    previous=-1,
    mode=2,
    timeout=-10000,
    force=0,
    op='M',
):
    size = len(body.encode('utf-16-le', 'surrogatepass')) // 2 if body is not None else 0
    if length is None:
        length = size - begin
    if start is None:
        start = begin + length if options & 64 else begin
    cases.append(
        f'{op} {options} {enc(culture)} {enc(pattern)} {enc(body)} {begin} {length} {start} {previous} {mode} {timeout} {force}'
    )


patterns = [
    '',
    'a',
    'abc',
    'a|b',
    'ab|ac',
    'a*',
    'a+',
    'a?',
    'a*?',
    'a+?',
    'a??',
    'a{2,4}',
    'a{2,4}?',
    '(?:ab)*',
    '(?:ab)*?',
    '(?:ab){2,4}',
    '(?:ab){2,4}?',
    '(?:a|ab)+b',
    '(?:ab|a)+?b',
    '(a)+',
    '((a)b)+',
    '(?<x>a)+',
    '(?<5>a)(?<x>b)+',
    '(a)?b\\1',
    '(a*)\\1',
    '(?i)(a+)\\1',
    '(?i)(i+)\\1',
    '(?<x>a)(?<-x>b)',
    '(?<x>a)+(?<y-x>b)+(?(x)(?!))',
    '(?<x>a)(?<y-x>b)',
    '(a)(?(1)b|c)',
    '(a)?(?(1)b|c)',
    '(?(?=a)a|b)',
    '(?(?!a)a|b)',
    '(?=ab)a',
    '(?!ab)a',
    '(?<=a)b',
    '(?<!a)b',
    '(?>a*)a',
    '(?>ab|a)b',
    '(a|)*',
    '(?:a?)*',
    '(?:a?)*?',
    '(?:a?){2,4}',
    '^a$',
    '\\Aa\\z',
    '\\Ga',
    '\\ba\\b',
    '\\Ba\\B',
    '(?m)^a$',
    'a\\Z',
    'a.',
    '[a-z]+',
    '[^a]*a',
    '\\d+',
    '\\w+',
    '\\s+',
    '[a-z-[aeiou]]+',
    '\\p{L}+',
    '(?i)İIıi',
    'a.*b',
    'a.*?b',
    '[a-z]*Z',
    '[a-z]*[XYZ]',
    '(?<open>\\()+(?<-open>\\))+(?(open)(?!))',
    '(a)(?<2>b)(?<x>c)',
]
bodies = [
    '',
    'a',
    'b',
    'ab',
    'abc',
    'aa',
    'aaaab',
    'ababab',
    'abba',
    'aac',
    'aabbb',
    'baab',
    'xaaay',
    'a\na\n',
    '12345',
    '  \t\n',
    'İIıi',
    'iIiI',
    '((()))',
    'aZ',
    'zzXYZ',
]
for pattern in patterns:
    for body in bodies:
        for options in [0, 1, 64, 256]:
            add(pattern, body, options)
for pattern in patterns:
    for body in ['', 'aaababb', 'a\nab\n', 'İIıi']:
        for options in [0, 1, 64]:
            add(pattern, body, options, op='A')
for culture in ['', 'en-US', 'tr-TR', 'az-Latn-AZ']:
    for body in ['ii', 'iI', 'Iı', 'İi', 'iİ', 'İİ', 'ıI', 'ſS', 'Kk', 'kK']:
        for pattern in ['(?i)(i)\\1', '(?i)(I)\\1', '(?i)(s)\\1', '(?i)(k)\\1']:
            for options in [0, 64, 256]:
                add(pattern, body, options, culture)
for pattern in ['', 'a', 'a*', '(?:ab)+', '(a)+', '(?<x>a)(?<-x>b)', '\\Ga', '^a$']:
    for options in [0, 64]:
        for begin, length, start in [
            (0, 5, 0),
            (1, 3, 1),
            (1, 3, 4),
            (2, 0, 2),
            (0, 5, 5),
            (1, 3, 0),
            (1, 3, 5),
            (-1, 3, 0),
            (0, -1, 0),
            (0, 6, 0),
            (4, 3, 4),
            (0, 5, -1),
            (0, 5, 6),
        ]:
            for mode in [0, 1, 2]:
                for previous in [-1, 0, 1]:
                    add(
                        pattern,
                        'aabaa',
                        options,
                        begin=begin,
                        length=length,
                        start=start,
                        mode=mode,
                        previous=previous,
                    )
rng = random.Random(574088)
atoms = [
    'a',
    'b',
    'ab',
    '[ab]',
    '[^a]',
    '\\d',
    '(a)',
    '(ab)',
    '(?:a|ab)',
    '(?:a?)',
    '(?=a)',
    '(?<!b)',
    '(?<x>b)',
]
for trial in range(4500):
    pattern = ''.join(
        rng.choice(atoms) + rng.choice(['', '', '?', '*', '+', '+?', '{1,3}', '{0,2}?'])
        for unused in range(rng.randint(1, 4))
    )
    body = ''.join(rng.choice('ab01 \n') for unused in range(rng.randrange(12)))
    add(pattern, body, rng.choice([0, 1, 8, 64, 256, 1024]), mode=rng.choice([0, 1, 2]))
for ticks in [
    -9223372036854775808,
    -10001,
    -10000,
    -9999,
    -1,
    0,
    1,
    9999,
    10000,
    15000,
    21474836460000,
    21474836460001,
    9223372036854775807,
]:
    cases.append(f'T {ticks}')
for pattern, body in [
    ('a', 'a'),
    ('a', 'b'),
    ('', ''),
    ('(?=a)a', 'a'),
    ('(a+)+$', 'a' * 18 + '!'),
]:
    for ticks in [1, 9999, 10000, 15000, 25000, -10000]:
        for force in [1, 2]:
            add(pattern, body, timeout=ticks, force=force)
# Grow all three runner stacks and capture arrays, including balancing undo.
for pattern, body in [
    ('(a)+', 'a' * 180),
    ('((a)b)+', 'ab' * 100),
    ('(?<x>a)+(?<-x>b)+', 'a' * 90 + 'b' * 70),
    ('(?<x>a)+(?<y-x>b)+(?(x)(?!))', 'a' * 80 + 'b' * 80),
    ('(?<x>a)(?<x-x>b)', 'ab'),
    ('(?:(a)|b)+c', 'ab' * 70 + 'c'),
    ('(?:a?){100,130}b', 'a' * 120 + 'b'),
    ('(?:a?){100,130}?b', 'a' * 120 + 'b'),
    ('(?<x>a)+(?<y-x>b)+c', 'a' * 70 + 'b' * 71 + 'c'),
    ('(?<2147483647>a)(b)\\k<2147483647>', 'aba'),
]:
    for options in [0, 1, 64, 256]:
        for op in ['M', 'A']:
            add(pattern, body, options, op=op)
for depth in [1, 5, 20, 45]:
    for options in [0, 64]:
        add('(' * depth + 'a' + ')' * depth, 'a', options)
for pattern in ['(?:ab)+', '(a)\\1', '(?=ab)ab', '(?<=a)b', '(?:|ab)', '(?:a|b)*']:
    for options in [0, 64]:
        for begin, length, start in [(0, 3, 3), (1, 1, 0), (1, 1, 3), (2, 0, 0), (2, 0, 3)]:
            add(pattern, 'aab', options, begin=begin, length=length, start=start)
for body in [None, '', 'a']:
    for pattern in [None, '', 'a']:
        add(pattern, body)
for pattern in ['(?i)(a)\\1', '(?i)(.)\\1', '(?i)(i)\\1']:
    for body in ['aa', 'aA', 'Aa', 'ii', 'iI', 'Iı', 'II', 'ſS', 'Kk']:
        add(pattern, body)
        fields = cases[-1].split(' ')
        fields[2] = '_'
        cases.append(' '.join(fields))
assert len(cases) > 10000
(work / 'input.txt').write_text('\n'.join(cases) + '\n', encoding='utf-8')
outputs = {}
for name, command in [
    ('reference', [args.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(args.native).resolve())]),
]:
    result = subprocess.run(
        command, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=180
    )
    (work / (name + '.txt')).write_text(result.stdout, encoding='utf-8')
    (work / (name + '-error.txt')).write_text(result.stderr, encoding='utf-8')
    assert result.returncode == 0, (name, result.stderr)
    outputs[name] = result.stdout.splitlines()
    assert len(outputs[name]) == len(cases), (name, len(outputs[name]), len(cases))
differences = [
    dict(index=i, input=cases[i], reference=left, native=right)
    for i, (left, right) in enumerate(zip(outputs['reference'], outputs['native']))
    if left != right
]
report = dict(
    passed=not differences,
    flows=len(cases),
    differences=len(differences),
    firstDifferences=differences[:20],
    applicationParityVerified=False,
    scope='Original private RegexInterpreter built from actual RegexWriter code/RegexTree; RegexRunner InitializeForScan, Scan and Match.Tidy are invoked directly. Tests target the backtracking interpreter even with raw NonBacktracking metadata, not the separate symbolic engine. Compares modes, subranges, prior empty matches, full capture histories, balancing, culture getter counts and forced deadline timeout fields. High-level Regex/symbolic/replacement evaluation and native host stack provider remain incomplete.',
    outputSHA256={
        name: hashlib.sha256(('\n'.join(rows) + '\n').encode()).hexdigest()
        for name, rows in outputs.items()
    },
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], report
print(f'PASS: {len(cases)} actual .NET interpreter flows')
