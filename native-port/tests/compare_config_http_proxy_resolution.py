from pathlib import Path
import argparse, subprocess, itertools, json, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
d = Path(__file__).resolve().parent
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
    Nullable='enable',
).items():
    ET.SubElement(props, k).text = v
ET.SubElement(
    ET.SubElement(project, 'ItemGroup'),
    'Compile',
    Include=str(d / 'config_http_proxy_resolution_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)
values = [
    scheme + '://' + host + path
    for scheme, host, path in itertools.product(
        ['http', 'https', 'socks4', 'socks4a', 'socks5', 'HTTP', 'ftp', 'file', 'custom'],
        ['proxy.example:8080', '[::1]:1080', 'höst.example:1234'],
        ['/', '/path?query#fragment'],
    )
] + [
    'relative',
    '/relative',
    'http://proxy.example',
    'https://proxy.example',
    'socks5://proxy.example',
    'socks4://proxy.example',
    'socks4a://proxy.example',
    'http://proxy.example:0',
    'socks5://proxy.example:0',
    'ftp://proxy.example',
    'custom://proxy.example',
    'file:///tmp/proxy',
]
rows = [
    mode + ' ' + value.encode('utf-16-be').hex()
    for mode, value in itertools.product(
        ['none', 'use', 'bypass', 'bthrow', 'gthrow', 'null'], values
    )
]
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
out = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=60)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    out[name] = r.stdout.splitlines()
    assert len(out[name]) == len(rows), (name, len(out[name]), len(rows))
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
report = dict(
    cases=len(rows),
    differences=len(diff),
    examples=diff[:5],
    applicationParityVerified=False,
    scope='Actual SocketsHttpHandler with injected ConnectCallback observes single-IWebProxy bypass, ignored lookup exceptions, null/direct decisions, unsupported scheme and relative URI failures, chosen IDN/IPv6 endpoint. No external sockets or proxy protocol/authentication claim.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(
    json.dumps(dict(cases=len(rows), differences=len(diff), examples=diff[:1]), ensure_ascii=True)
)
raise SystemExit(bool(diff))
