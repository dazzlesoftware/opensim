"""Original JSON-RPC response class and BaseHttpServer dispatch/registration methods."""

from pathlib import Path
import argparse, json, hashlib, subprocess, xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
source = root / 'OpenSim/Framework/Servers/HttpServer/BaseHttpServer.cs'
text = source.read_text(encoding='utf-8-sig')


def extract(signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end = brace + 1
    depth = 1
    while depth:
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
        end += 1
    return text[start:end]


methods = [
    extract(s)
    for s in [
        'private void HandleJsonRpcRequests(',
        'public bool AddJsonRPCHandler(',
        'public JsonRPCMethod GetJsonRPCHandler(',
        'public List<string> GetJsonRpcHandlerKeys(',
        'public void RemoveJsonRPCHandler(',
    ]
]
methods[0] = methods[0].replace(
    'OSDParser.DeserializeJson(request.InputStream)', 'Decode(request.InputStream)'
)
generated = w / 'Extracted.cs'
generated.write_text(
    'using System;using System.Net;using System.Collections.Generic;using OpenMetaverse.StructuredData;using OpenSim.Framework.Servers.HttpServer;partial class Wrapper{'
    + ''.join(methods)
    + '}'
)
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
inputs = [
    root / 'native-port/tests/json_rpc_reference.cs',
    generated,
    root / 'OpenSim/Framework/Servers/HttpServer/JsonRpcResponse.cs',
    root / 'OpenSim/Framework/Servers/HttpServer/JsonRPCMethod.cs',
]
for f in inputs:
    ET.SubElement(items, 'Compile', Include=str(f))
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
bodies = [
    'bad',
    'true',
    'false',
    '"text"',
    '0',
    '-0',
    '1 ',
    '[1,]',
    '{"x":}',
    '{"x":"\\q"}',
    '\x00',
    '[9223372036854775808]',
    '{"jsonrpc":"2.0","method":"test"}junk',
    'nullX',
    'null0',
    'null true',
    'null{}',
    'null:',
    '{}',
    '[]',
    'null',
    '',
    '{"jsonrpc":"2.0","method":"test","id":1}',
    '{"jsonrpc":"2.0","method":"test"}',
    '{"jsonrpc":"2.0","method":"test","id":null}',
    '{"jsonrpc":"2.0","method":"missing","id":"abc"}',
    '{"jsonrpc":"1.0","method":"test","id":"abc"}',
    '{"jsonrpc":2.0,"method":"test","id":true}',
    '{"jsonrpc":"2.0"}',
    '{"jsonrpc":"2.0","method":"   "}',
    '{"jsonrpc":"2.0","method":{}}',
    '{"jsonrpc":"2.0","method":"again"}',
]
commands = [f'S {profile} {mode} -' for profile in range(16) for mode in range(16)]
for profile in range(10):
    for mode in range(3):
        for body in bodies:
            for encoding, bom in [
                ('utf-8', b''),
                ('utf-16-le', b'\xff\xfe'),
                ('utf-32-be', b'\x00\x00\xfe\xff'),
            ]:
                commands.append(
                    f'D {profile} {mode} ' + ((bom + body.encode(encoding)).hex() or '-')
                )
rows = []
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    r = subprocess.run(
        cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
    )
    assert r.returncode == 0, r.stderr
    rows.append(r.stdout.splitlines())
    (w / (name + '.txt')).write_text(r.stdout)
    assert len(rows[-1]) == len(commands)
diffs = [dict(command=c, source=x, native=y) for c, x, y in zip(commands, *rows) if x != y]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differences=diffs[:8],
    applicationParityVerified=False,
    scope='Original full JsonRpcResponse/Error and extracted BaseHttpServer JSON-RPC handler+registration methods. Actual SDK parses valid JSON/BOMs; controlled injected JsonException/unexpected parse failure, stream-disposal callback and ten handler outcomes including replacement/null/error/logging. Stateful repeated response serialization and dictionary slot order. Native production lexer/parser diagnostics, NUL messages and UInt64 cast failures are compared directly in added malformed cases; full reader APIs/culture/depth, transport/platform/application parity unverified.',
    sourceHashes={
        str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
        for f in inputs
        if f != generated
    },
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(diffs[:2])
print('PASS:', len(commands), 'original JSON-RPC scenarios')
