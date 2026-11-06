# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
from pathlib import Path
import argparse, subprocess, json, hashlib, random, xml.etree.ElementTree as ET, itertools
from compare_inventory_asset_caps import extract

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
source = root / 'OpenSim/Framework/Console/ConsoleBase.cs'
raw = source.read_bytes().decode('utf-8-sig')
code = (
    (root / 'native-port/tests/console_prompts_reference.cs')
    .read_text()
    .replace('LEVEL', extract(raw, 'public class ConsoleLevel'))
    .replace('BASE', extract(raw, 'public class ConsoleBase'))
)
(work / 'Reference.cs').write_text(code, encoding='utf-8', newline='')
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
ET.SubElement(ET.SubElement(project, 'ItemGroup'), 'Compile', Include=str(work / 'Reference.cs'))
ET.ElementTree(project).write(work / 'Reference.csproj')
run = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
h = lambda value: '~' if value is None else value.encode('utf-16-be', 'surrogatepass').hex()


def ls(values):
    return '~' if values is None else '!' if not values else '@' + ','.join(map(h, values))


def case(op, text=None, fallback=None, chars=None, options=None, echo=True, inputs=()):
    return '|'.join(
        [op, h(text), h(fallback), h(chars), ls(options), '1' if echo else '0', ls(inputs)]
    )


cases = []
values = [
    None,
    '',
    'name',
    'a',
    'A',
    'bad!',
    'x y',
    '\x00',
    'é',
    '中',
    '\ud800',
    '\udc00',
    '😀',
    '\n',
    '[{}]: ',
    'a!a',
]
for op, text, fallback, input_value in itertools.product(
    ['P', 'D', 'X', 'E', 'O'], [None, '', 'Prompt', 'é\n'], [None, '', 'default!'], values
):
    cases.append(
        case(
            op,
            text,
            fallback,
            chars='!a',
            options=[None, '', 'yes', 'default!'],
            inputs=[input_value, 'yes'],
        )
    )
for excluded in [None, '', '!', '!!', 'a!', '\x00', '\ud800', '😀']:
    for value in values:
        for op in ['X', 'E']:
            cases.append(case(op, 'Question', 'default!', excluded, inputs=[value, 'safe']))
for options in [
    None,
    [],
    [None],
    [''],
    ['yes', 'no'],
    ['yes', 'yes'],
    ['A', 'a'],
    ['😀', '\ud800'],
]:
    for value in values:
        cases.append(case('O', 'Choice', None, options=options, inputs=[value, 'yes']))
for unit in range(65536):
    text = chr(unit)
    cases.append(case('E', 'P', 'fallback', text, echo=bool(unit & 1), inputs=[text, '']))
rng = random.Random(82531)
for _ in range(1500):
    text = ''.join(rng.choice('ab! x\x00é中\ud800\udc00') for i in range(rng.randrange(12)))
    excluded = ''.join(rng.choice('ab! x\x00é中\ud800\udc00') for i in range(rng.randrange(5)))
    cases.append(
        case(
            rng.choice(['X', 'E']),
            text,
            rng.choice([None, '', 'default']),
            excluded,
            echo=rng.choice([True, False]),
            inputs=[text, ''],
        )
    )
for op in ['X', 'E']:
    for action in ['cx', 'ca:' + h('!'), 'cc', 'cs:0:' + h('!'), 'cr:0']:
        for phase in [0, 1]:
            base = case(op, 'Question', 'default!', '!!a', inputs=['bad!', 'safe'])
            cases.append(
                base + '|' + (action if phase == 0 else '') + '|' + (action if phase == 1 else '')
            )
for action in ['ox', 'oa:' + h('yes'), 'oa:~', 'oc', 'os:0:' + h('yes'), 'or:0']:
    for phase in [0, 1]:
        for value in ['yes', 'bad', '']:
            base = case('O', 'Question', None, options=['no', 'later'], inputs=[value, 'yes'])
            cases.append(
                base + '|' + (action if phase == 0 else '') + '|' + (action if phase == 1 else '')
            )
payload = '\n'.join(cases) + '\n'
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
    dict(index=i, input=cases[i], reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not differences,
    scenarios=len(cases),
    differenceCount=len(differences),
    differences=differences[:5],
    applicationParityVerified=False,
    scope='All five original ConsoleBase prompt overloads, shared nullable lists with read/diagnostic callback mutations, UTF16 code-unit exclusions, defaults, option order/ordinal membership, null/EOF failure phase, diagnostics and echo arguments. All BMP units. Arbitrary concurrent list mutation, console I/O transport and full application parity remain outside this comparison.',
    sourceSHA256=hashlib.sha256(source.read_bytes()).hexdigest(),
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], str(report['differenceCount']) + ' prompt differences; see results.json'
print('PASS:', len(cases), 'original console prompt scenarios')
