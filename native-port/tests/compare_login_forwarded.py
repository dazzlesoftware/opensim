# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Original Util.GetClientIPFromXFF against the native XML-RPC HTTP login path."""

from pathlib import Path
import argparse, subprocess, json, hashlib, random, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
utilpath = root / 'OpenSim/Framework/Util.cs'
source = utilpath.read_text(encoding='utf-8-sig')
start = source.index('public static IPEndPoint GetClientIPFromXFF(')
brace = source.index('{', start)
end = brace + 1
depth = 1
while depth:
    if source[end] == '{':
        depth += 1
    if source[end] == '}':
        depth -= 1
    end += 1
code = 'using System;using System.Net;\nclass Log {public void Warn(string value){}}\nclass Util {static Log m_log=new();public static readonly char[] SplitCommaArray={\',\'};METHOD}\nclass Program {static void Main(){string row;while((row=Console.ReadLine())!=null){var value=System.Text.Encoding.UTF8.GetString(Convert.FromHexString(row));Console.WriteLine(Util.GetClientIPFromXFF(value)?.Address.ToString()??"192.0.2.9");}}}\n'.replace(
    'METHOD', source[start:end]
)
(work / 'Reference.cs').write_text(code, encoding='utf-8')
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
build = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert build.returncode == 0, build.stdout + build.stderr
base = [
    '',
    '127.0.0.1',
    '127.1',
    '0x7f000001',
    '0177.0.0.1',
    '0',
    '4294967295',
    '4294967296',
    '192.0.2.7',
    '::1',
    '2001:db8::7',
    '[::1]',
    '[::1]:80',
    '::ffff:192.0.2.7',
    'fe80::1%2',
    'invalid',
    'localhost',
    '256.0.0.1',
    '1.2.3.4:80',
]
cases = [
    value + suffix
    for value in base
    for suffix in ['', ' ', '\t', '\r', '\n', '\0', ',203.0.113.7', ', 203.0.113.7', ',invalid']
]
cases += [prefix + value for value in base for prefix in [' ', '\t', '\n', ',', 'invalid,']]
rng = random.Random(9042)
for _ in range(3000):
    value = '.'.join(str(rng.randrange(0, 400)) for _ in range(rng.randrange(1, 6)))
    cases.append(value + rng.choice(['', ', 203.0.113.7', ' ', '\0']))
for _ in range(1000):
    cases.append(
        ':'.join(format(rng.randrange(65536), 'x') for _ in range(8))
        + rng.choice(['', ',127.0.0.1', '%2'])
    )
payload = '\n'.join(value.encode('utf-8').hex() for value in cases) + '\n'
(work / 'input.txt').write_text(payload, encoding='utf-8')
outputs = {}
for name, command in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    run = subprocess.run(
        command, input=payload, capture_output=True, text=True, encoding='utf-8', timeout=120
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
    cases=len(cases),
    differences=differences[:40],
    differenceCount=len(differences),
    applicationParityVerified=False,
    scope='Original XFF first-address utility versus real native XML-RPC LoginApplication to ILoginService; full transport, LLSD and simulator parity not proved.',
    sourceSHA256=hashlib.sha256(utilpath.read_bytes()).hexdigest(),
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], report
print(f'PASS: {len(cases)} original forwarded login observations')
