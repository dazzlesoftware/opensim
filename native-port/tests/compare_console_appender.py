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
source = root / 'OpenSim/Framework/Console/OpenSimAppender.cs'
base = root / 'OpenSim/Framework/Console/ConsoleBase.cs'
template = (root / 'native-port/tests/console_appender_reference.cs').read_text()
code = template.replace(
    'LEVEL', extract(base.read_bytes().decode('utf-8-sig'), 'public class ConsoleLevel')
).replace(
    'APPENDER', extract(source.read_bytes().decode('utf-8-sig'), 'public class OpenSimAppender')
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
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(work / 'Reference.cs'))
ref = ET.SubElement(items, 'Reference', Include='log4net')
ET.SubElement(ref, 'HintPath').text = str(root / 'bin/log4net.dll')
ET.ElementTree(project).write(work / 'Reference.csproj')
run = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
h = lambda value: value.encode('utf-16-be', 'surrogatepass').hex()


def case(level, console, render, writer, text, name='custom'):
    return '|'.join([str(level), str(console), str(render), str(writer), h(text), h(name)])


cases = []
levels = [
    '~',
    '#',
    -2147483648,
    0,
    10000,
    20000,
    30000,
    40000,
    50000,
    59999,
    60000,
    60001,
    69999,
    70000,
    70001,
    110000,
    2147483647,
]
for level, console, render, writer, text in itertools.product(
    levels,
    range(4),
    range(5),
    range(4),
    ['', 'hello', 'line\n', 'line\r\n', 'line\r', '\n', '{0} & <é>', '\ud800'],
):
    cases.append(case(level, console, render, writer, text))
for unit in range(65536):
    cases.append(case(70000 if unit & 1 else 60000, 1, 0, 0, chr(unit), 'é' + chr(unit)))
for unit in range(65536):
    for text in ['prefix\n' + chr(unit), chr(unit) + '\n']:
        cases.append(case(40000, 0, 0, 0, text))
rng = random.Random(81832)
for _ in range(1500):
    text = ''.join(rng.choice('abc{}\n\r\x00é中\ud800\udc00') for i in range(rng.randrange(35)))
    cases.append(
        case(rng.choice(levels), rng.randrange(4), rng.randrange(5), rng.randrange(4), text)
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
    scope='Unchanged OpenSimAppender with actual log4net Level/LoggingEvent/LayoutSkeleton/AnsiColorTerminalAppender and ConsoleLevel; recording ConsoleBase sink. Error/warn numeric equality, newline choice, renderer and output failures, target replacement and all BMP message/name units and BMP suffix units around LF. Fallback NullReferenceException stack frames normalized; arbitrary layout formatting and appender-base filters/thresholds/lifecycle remain outside this consumed Append comparison.',
    sourceSHA256={
        str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [source, base, root / 'bin/log4net.dll']
    },
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], str(report['differenceCount']) + ' appender differences; see results.json'
print('PASS:', len(cases), 'original console appender scenarios')
