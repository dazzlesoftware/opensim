"""Compare native parsing with actual .NET8.0.31 private parser methods."""

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
    Include=str(Path(__file__).with_name('dotnet_regex_parser_reference.cs').resolve()),
)
ET.ElementTree(project).write(work / 'Reference.csproj')
build = subprocess.run(
    [args.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(build.stdout + build.stderr, encoding='utf-8')
assert build.returncode == 0, build.stdout + build.stderr


def enc(value):
    return '-' if value is None else value.encode('utf-16-be', 'surrogatepass').hex() or '.'


cases = []


def add(pattern, options=0, culture='', op='P'):
    cases.append(f'{op} {options} {enc(culture)} {enc(pattern)}')


patterns = [
    None,
    '',
    'a',
    'ab',
    'abc',
    'a|b',
    'ab|ac',
    'abc|abd|xyz',
    'a*',
    'a+',
    'a?',
    'a{2}',
    'a{2,4}',
    'a{2,}',
    'a{2147483647}',
    'a{2147483648}',
    'a{4,2}',
    'a**',
    '{',
    '[',
    '[^',
    '[]',
    '[^]',
    '[a-z]',
    '[z-a]',
    '[a-z-[d-f]]',
    '[a-\\d]',
    '[\\w-]',
    '[\\p{Lu}]',
    '[\\p{NoSuch}]',
    '[a-[b]c]',
    '\\',
    '\\q',
    '\\c',
    '\\ca',
    '\\c?',
    '\\x0',
    '\\xzz',
    '\\u1234',
    '\\p{Lu}',
    '\\p{Ll}',
    '\\p{IsGreek}',
    '\\p{bad}',
    '\\p',
    '\\pA',
    '\\p{}',
    '\\d\\w\\d',
    '\\s\\S\\d\\D\\w\\W',
    '\\A^.$\\Z\\z\\b\\B\\G',
    '(a)',
    '(?:a)',
    '(?i)a',
    '(?i:a)',
    '(?i:a)B',
    '(?-i:a)',
    '(?n)(a)(?<b>b)',
    '(?<name>a)\\k<name>',
    "(?'name'a)\\k'name'",
    '(?<5>a)(b)(?<x>c)',
    '(?<2147483647>a)',
    '(?<0>a)',
    '(?<1x>a)',
    '(?<x>a)(?<-x>b)',
    '(?<x>a)(?<y-x>b)',
    '(?<x-y>a)',
    '(a)\\1',
    '\\1(a)',
    '\\10',
    '\\123',
    '(a)\\11',
    '(a)\\k<1>',
    '\\k<x>',
    '(?(1)a|b)(a)',
    '(?<x>a)(?(x)b|c)',
    '(?(?=a)a|b)',
    '(?(?!a)a|b)',
    '(?(a)a|b)',
    '(?(?<x>a)b|c)',
    '(?(?#x)a|b)',
    '(a|b|)',
    '(?>abc|abd|ax)',
    '(?=a*)',
    '(?!a*)',
    '(?<=ab)c',
    '(?<!ab)c',
    '(?x) a #comment\n b',
    '(?#comment)a',
    '(?#unfinished',
    '(',
    ')',
    '(?',
    '(?a)',
    '(?i',
    '(?<',
    '(?(1',
    '[\\-a]',
    '[a-\\-]',
    '[a--]',
    '\u0130iI\u0131\u212aKk',
    '(?i)\u0130iI\u0131\u212aKk',
    '(?i)(I)\\1',
    'a*?b',
    'a*?',
    'a*b',
    '.*a',
    '.*',
    '.*?a',
    '(?:ab|ac){2}',
    '(a|)??b',
    '(?x)a { 2 }',
    '(?x)a{2} ?',
    'a\0b',
    '\ud800\udfff',
]
for pattern in patterns:
    for options in [0, 1, 2, 4, 16, 32, 64, 256, 1024, 65, 257]:
        add(pattern, options)
for code in range(65536):
    value = chr(code)
    add(value, op='E')
    add('\\' + value, op='U')
for pattern in patterns:
    for culture in ['', 'en-US', 'tr-TR', 'az-Latn-AZ', None]:
        add(pattern, 1, culture)
for number in [
    '0',
    '1',
    '2',
    '5',
    '9',
    '10',
    '99',
    '2147483646',
    '2147483647',
    '2147483648',
    '999999999999',
]:
    for form in ['(?<{}>a)', 'a{{{}}}', 'a{{{},}}', 'a{{1,{}}}', '\\{}', '\\k<{}>', '(?({})a|b)']:
        add(form.format(number))
rng = random.Random(867332)
atoms = [
    'a',
    'b',
    'ab',
    'abc',
    'i',
    'I',
    '\u0130',
    '[a-z]',
    '[^a]',
    '[\\w]',
    '\\d',
    '\\w',
    '\\b',
    '.',
    '^',
    '$',
    '(?:a|b)',
    '(?<x>a)',
    '(?=ab)',
    '(?<!c)',
]
for trial in range(8000):
    pattern = ''
    for i in range(rng.randint(1, 5)):
        pattern += rng.choice(atoms) + rng.choice(
            ['', '', '', '*', '+', '?', '{2}', '{0,3}', '+?', '??']
        )
        if rng.randrange(8) == 0:
            pattern += '|'
    add(pattern, rng.choice([0, 0, 1, 32, 64, 256, 1024]), rng.choice(['', 'en-US', 'tr-TR']))
for trial in range(4000):
    add(
        ''.join(
            rng.choice('ab()[]{}?*+|\\012k<>:-^$xinp# ') for unused in range(rng.randint(1, 20))
        ),
        rng.choice([0, 1, 32, 64, 256]),
    )
assert len(cases) == 144957, len(cases)
for pattern in patterns:
    for operation in ['E', 'U']:
        add(pattern, op=operation)
pattern_flows = len(cases)
for replacement in [
    None,
    '',
    'abc',
    'I\u0130i\u0131',
    '$',
    '$$',
    '$&',
    "$`",
    "$'",
    '$+',
    '$_',
    '$0',
    '$1',
    '$2',
    '$5',
    '$9',
    '$10',
    '$12',
    '$01',
    '${1}',
    '${x}',
    '${y}',
    '${a}',
    '${b}',
    '${0}',
    '${}',
    '${x',
    '${1x}',
    '${-1}',
    '$2147483648',
    '${2147483648}',
    'abc$1def$2ghi',
    '$$$&$`$+$_${x}',
    '\\a$1',
]:
    for context in range(7):
        for options in [0, 1, 64, 256, 512, 257]:
            cases.append(f'R {options} {context} {enc(replacement)}')
for trial in range(2500):
    cases.append(
        f'R {rng.choice([0,1,64,256,512])} {rng.randrange(7)} '
        + enc(''.join(rng.choice("abc$01259{}&_`'+x") for unused in range(rng.randrange(30))))
    )
parser_flows = len(cases)
# Emit actual bytecodes for every pattern flow, including original parse errors.
for original in list(cases):
    if original.startswith('P '):
        cases.append('W' + original[1:])
bytecode_flows = len(cases) - parser_flows
for opcode in list(range(-1024, 2049)) + [-2147483648, 2147483647]:
    cases.append(f'T {opcode} . .')
for context in range(9):
    for slot in [-2147483648, -3, -2, -1, 0, 1, 2, 3, 5, 9, 10, 2147483647]:
        for operation in ['M', 'G']:
            cases.append(f'{operation} 0 {context} {enc(str(slot))}')
for options in list(range(2048)) + [-1, -2147483648, 2147483647]:
    for culture in ['', 'en-US', 'tr-TR']:
        cases.append(f'C {options} {enc(culture)} .')
utility_flows = len(cases) - parser_flows - bytecode_flows
for original in list(cases):
    if original.startswith('P '):
        cases.append('X' + original[1:])
prefix_flows = len(cases) - parser_flows - bytecode_flows - utility_flows
for original in list(cases):
    if original.startswith('P '):
        cases.append('O' + original[1:])
find_patterns = [
    '(?m)^',
    '(?m)^a*',
    '(?m)^$',
    '\\G',
    'a\\z',
    '\\b',
    '(?=a)',
    'a*?Z',
    '[^a]*Z',
    '',
    'a',
    'abc',
    'a*',
    '^a',
    '(?m)^a',
    '\\A',
    '\\G',
    '\\z',
    '\\Z',
    'abc\\z',
    'abc$',
    '[a-z]',
    '[^a-z]',
    '(?i)ab',
    '(?i)[ab]cd',
    '[a-z]*Z',
    '[a-z]+Z',
    '[^a]+XYZ',
    '[a-z]*[XYZ]',
    '[a-z]*[aXYZ]',
    'a.b',
    '[a-z]..X',
    '[a-z]{3}X',
    '[a-z]*?Z',
    '(?:abc|dbe)X',
    '[a-z]' * 24,
    '[^a-z]' * 24,
]
for pattern in find_patterns:
    for options in [0, 1, 8, 64, 72, 1024]:
        for body in [
            '',
            None,
            'abc',
            'ABC',
            'ab\ncd\n',
            'xxabcXYZzz',
            'aaaZ',
            'zzzzXYZ',
            '12345',
            '\u017f\u0131\u212aab',
            'aa\ud800\udfffbb',
        ]:
            size = len(body) if body is not None else 0
            for position in sorted(set([-1, 0, 1, size // 2, size, size + 1, 2147483647])):
                for direction in ['l', 'r']:
                    cases.append(
                        f'F {options} . {enc(pattern)} {enc(body)} {position} 0 {direction}'
                    )
# ASCII ordinal-ignore-case prefix against every UTF16 input code unit.
for code in range(65536):
    cases.append(f'F 1 . {enc("ab")} {enc(chr(code)+"b")} 0 0 l')
for pattern in ['\\G', '(?m)^', '(?m)^a*', '(?i)ab', '[a-z]*Z']:
    for body in ['', 'abc', 'a\n', 'a\nb', 'zzZ']:
        for position in [-2, -1, 0, 1, len(body), len(body) + 1]:
            for start in [-1, 0, 1, len(body), len(body) + 1]:
                for direction in ['l', 'r']:
                    cases.append(f'F 0 . {enc(pattern)} {enc(body)} {position} {start} {direction}')
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
    patternAndEscapeFlows=pattern_flows,
    replacementFlows=parser_flows - pattern_flows,
    bytecodeFlows=bytecode_flows,
    utilityFlows=utility_flows,
    prefixFlows=prefix_flows,
    findFlows=len(cases) - parser_flows - bytecode_flows - utility_flows - prefix_flows,
    differences=len(differences),
    firstDifferences=differences[:20],
    applicationParityVerified=False,
    scope='Actual pinned RegexParser.Parse root graph/capture metadata; every UTF16 escape/unescape and string identity. Replacement compiled strings/rules compared with actual runtime class. Writer codes, string table, track count and initial cache slots are compared to the actual runtime writer; Prefix analysis, optimizer metadata, search positions and cache effects are compared to the original runtime. Execution remains incomplete. Explicit true policy for bounded acyclic parser trees, without stack-boundary equivalence.',
    outputSHA256={
        name: hashlib.sha256(('\n'.join(rows) + '\n').encode()).hexdigest()
        for name, rows in outputs.items()
    },
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], report
print(f'PASS: {len(cases)} actual .NET regex parser flows')
