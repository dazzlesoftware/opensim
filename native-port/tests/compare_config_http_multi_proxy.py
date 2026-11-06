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
    Include=str(d / 'config_http_multi_proxy_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)
prefixes = [
    '',
    'http=',
    'https=',
    'HTTP=',
    'http://',
    'https://',
    'HTTP://',
    'https=http://',
    'http=https://',
    'socks=',
    'socks5://',
]
hosts = [
    'proxy',
    'proxy:80',
    'proxy:8080',
    '[::1]:1080',
    'höst.example:1234',
    'proxy/path?query#fragment',
    'bad:port',
    '',
    'user:pass@proxy:81',
    'proxy:0',
    'proxy:65536',
]
values = [
    prefix + host + tail
    for prefix, host, tail in itertools.product(
        prefixes, hosts, ['', '; ', ';http=second:82;https=third:83']
    )
] + [
    '',
    '; \n\r\t',
    ' http=one:1 https=two:2 ',
    'one:1;bad:port;two:2',
    'http=;https=;proxy:80',
    'proxy\u00a0other',
    'proxy\vother',
    'proxy\fother',
]
values += [
    'one:1;two:2;three:3',
    'one:1;one:1;two:2',
    'one:1;ONE:1/;one:1/#fragment',
    'one:1;bad:port;two:2; ',
    'one:1;;;;',
    'https=one:1;http=two:2;three:3',
]
rows = [
    str(lazy)
    + ' '
    + str(secure)
    + ' '
    + str(mode)
    + ' '
    + (value.encode('utf-16-be').hex() if value else '-')
    for lazy, secure, mode, value in itertools.product([0, 1], [0, 1], range(6), values)
] + [
    str(lazy) + ' ' + str(secure) + ' ' + str(mode) + ' null'
    for lazy, secure, mode in itertools.product([0, 1], [0, 1], range(6))
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
cache_outputs = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), '--cache']),
    ('native', [str(Path(a.native).resolve()), '--unit']),
]:
    r = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
    (w / (name + '-cache.txt')).write_text(r.stdout)
    (w / (name + '-cache-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    cache_outputs[name] = r.stdout
assert cache_outputs['native'] == cache_outputs['reference'], cache_outputs
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
report = dict(
    cacheChecksPassed=True,
    cases=len(rows),
    differences=len(diff),
    examples=diff[:5],
    applicationParityVerified=False,
    scope='Actual Windows MultiProxy.ReadNext shared failure cache: absent, first/all failed, equal expiry, expired and max-tick entries. Candidate choice, cursor/final flags, retry failure marking, oldest renewal and final cache contents. Cache expiry interval checked against actual runtime clock bounds; no transport or OS/PAC claim.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(
    json.dumps(dict(cases=len(rows), differences=len(diff), examples=diff[:1]), ensure_ascii=True)
)
raise SystemExit(bool(diff))
