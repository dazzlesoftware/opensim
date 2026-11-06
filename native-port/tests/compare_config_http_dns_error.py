from pathlib import Path
import argparse, subprocess, itertools, json, sys, os, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument("--injector", type=Path)
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
    Include=str(d / 'config_http_dns_error_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)
outcomes = ['noname', 'again', 'fail', 'nodata'] + (
    ['addrfamily', 'family', 'badflags', 'service', 'memory', 'system']
    if sys.platform.startswith('linux')
    else []
)
rows = [
    host.encode('utf-16-be').hex() + ' ' + str(port) + ' ' + outcome
    for host, port, outcome in itertools.product(
        [
            'localhost',
            '127.0.0.1',
            'example.com',
            'xn--hst-sna.example',
            '::1',
            '[::1]',
            'fe80::1%zone',
            '[fe80::1%zone]',
        ],
        [1, 80, 81, 443, 65535],
        outcomes,
    )
]
if a.injector:
    assert sys.platform.startswith('linux')
    rows = [
        (name + '.opensim-parity.invalid').encode('utf-16-be').hex() + ' ' + str(port) + ' ' + name
        for name, port in itertools.product(outcomes, [80, 81])
    ]
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
out = {}
env = os.environ.copy()
env.update({'LD_PRELOAD': str(a.injector.resolve())} if a.injector else {})
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')] + (['--resolve'] if a.injector else [])),
    ('native', [str(Path(a.native).resolve()), '--resolve' if a.injector else '--probe']),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, env=env, timeout=60)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    out[name] = r.stdout.splitlines()
    assert len(out[name]) == len(rows)
    if a.injector:
        assert len([x for x in r.stderr.splitlines() if x.startswith('RESOLVER:')]) == len(rows), (
            name,
            r.stderr,
        )
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
report = {
    'cases': len(rows),
    'actualBclXmlDownloadManager': bool(a.injector),
    'resolverInjection': bool(a.injector),
    'differences': len(diff),
    'examples': diff[:5],
    'applicationParityVerified': False,
    'scope': 'Distinct DNS outcomes: actual SocketException +ConnectHelper.CreateWrappedException, including RetryOnNextProxy construction at canonical HTTP authority endpoints, native/managed socket codes,HResult,message/inner tree. Linux resolver fault injection separately verifies consumed HTTP mappings; macOS, other resolver codes and full application remain incomplete.',
}
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(
    json.dumps(
        {
            'cases': len(rows),
            'actualBclXmlDownloadManager': bool(a.injector),
            'resolverInjection': bool(a.injector),
            'differences': len(diff),
        }
    )
)
raise SystemExit(bool(diff))
