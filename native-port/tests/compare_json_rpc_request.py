"""Unchanged JsonRpcRequestManager, real SDK reflection and original profile DTOs."""

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
sources = [
    'native-port/tests/json_rpc_request_reference.cs',
    'OpenSim/Framework/Servers/HttpServer/JsonRpcRequestManager.cs',
    'OpenSim/Framework/UserProfiles.cs',
]
for f in sources:
    ET.SubElement(items, 'Compile', Include=str(root / f))
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / str(name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
commands = [
    f'M {model} {fill} {reply} {args}'
    for model in range(9)
    for fill in range(2)
    for reply in range(19)
    for args in range(10)
]
commands += [
    f'O {kind} 0 {reply} {args}' for kind in range(8) for reply in range(19) for args in range(10)
]
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
    differences=diffs[:5],
    applicationParityVerified=False,
    scope='Unchanged full original JsonRpcRequestManager, actual SDK OSD SerializeMembers/DeserializeMembers and eight original profile/account DTOs. Controlled PostToService outcomes, exact request+wire JSON, debug message/exception text, ref identity, partial updates, null/error/argument/whitespace/UUID generation paths. Generated UUIDs validated as v4 then normalized. General reflection/types/cache-entry/transport/global HTTP factory/platform/application parity incomplete.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' JSON-RPC request differences; see results.json'
print('PASS:', len(commands), 'original JSON-RPC request manager scenarios')
