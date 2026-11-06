from pathlib import Path
import argparse, subprocess, itertools, json, xml.etree.ElementTree as ET, os

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--stub', required=True)
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
    Include=str(d / 'config_http_windows_proxy_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)


def enc(s):
    return 'null' if s is None else s.encode('utf-16-be').hex() or '-'


rows = []
for (
    mode,
    success,
    detect,
    url,
    manual,
    bypass,
    valid,
    errors,
    (pac, pacb),
    uri,
) in itertools.product(
    ['G', 'M'],
    [0, 1],
    [0, 1],
    [None, 'http://pac.example/script'],
    [None, 'http=insecure:8080;https=secure:8443', 'one:1;two:2'],
    [None, '', '*', '<local>'],
    [0, 1],
    ['0', '12015,0', '12180'],
    [(None, None), ('pac:8088', None), ('pac:8088', ''), ('pac:8088', '*.example')],
    [
        'http://destination.example/path',
        'https://destination.example/path',
        'wss://flat/path',
        'relative',
    ],
):
    rows.append(
        ' '.join(
            [
                mode,
                str(success),
                str(detect),
                enc(url),
                enc(manual),
                enc(bypass),
                str(valid),
                errors,
                enc(pac),
                enc(pacb),
                enc(uri),
            ]
        )
    )
for detect, url, manual, bypass, uri in itertools.product(
    [0, 1],
    [None, 'http://pac.example/script'],
    [None, 'one:1;two:2'],
    [None, '<local>'],
    ['http://destination.example/path', 'relative', None],
):
    rows.append(
        ' '.join(
            [
                'D',
                '1',
                str(detect),
                enc(url),
                enc(manual),
                enc(bypass),
                '1',
                '0',
                enc('pac:8088'),
                'null',
                enc(uri),
            ]
        )
    )
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
out = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), str(Path(a.stub).resolve())]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=240)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    out[name] = r.stdout.splitlines()
    assert len(out[name]) == len(rows), (name, len(out[name]), len(rows))
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
native = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), '--native']),
    ('native', [str(Path(a.native).resolve()), '--native']),
]:
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    (w / (name + '-real-factory.txt')).write_text(r.stdout)
    (w / (name + '-real-factory-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    native[name] = r.stdout
assert native['reference'] == native['native'], native
defaults = []
names = {'http_proxy', 'https_proxy', 'all_proxy', 'no_proxy', 'gateway_interface'}
for label, changes in [
    ('current', None),
    ('clear', {}),
    ('http', {'http_proxy': 'http://user:pass@proxy.example:8080'}),
    ('https', {'HTTPS_PROXY': 'https://proxy.example:8443'}),
    ('all', {'ALL_PROXY': 'socks5://proxy.example:1080'}),
    ('malformed', {'http_proxy': 'http://['}),
]:
    env = dict(os.environ)
    if changes is not None:
        env = {k: v for k, v in env.items() if k.lower() not in names}
        env.update(changes)
    result = {}
    for name, cmd in [
        ('reference', [a.dotnet, str(w / 'app/Reference.dll'), '--default']),
        ('native', [str(Path(a.native).resolve()), '--default']),
    ]:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=30)
        (w / (name + '-default-' + label + '.txt')).write_text(r.stdout)
        (w / (name + '-default-' + label + '-error.txt')).write_text(r.stderr)
        assert r.returncode == 0, (label, name, r.stderr)
        result[name] = r.stdout
    assert result['reference'] == result['native'], (label, result)
    if label in ['http', 'https', 'all']:
        assert result['native'].startswith('environment:'), (label, result)
    defaults.append(dict(case=label, value=result['native'].strip()))
(w / 'default-selection-results.json').write_text(json.dumps(defaults, indent=2))
report = dict(
    cases=len(rows),
    differences=len(diff),
    examples=diff[:5],
    realFactoryAvailabilityMatches=True,
    defaultSelectionCases=len(defaults),
    applicationParityVerified=False,
    scope='Actual HttpWindowsProxy.TryCreate and two GetProxy/GetMultiProxy queries through injected WinHTTP settings/results: factory failure, manual/auto/bypass, nullable PAC bypass, recent auto failure, shared failure cache, credentials identity and disposal/session closure. Real read-only native factory availability matches. Full application, proxy auth/protocols and macOS remain incomplete.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(json.dumps(dict(cases=len(rows), differences=len(diff), examples=diff[:2])))
raise SystemExit(bool(diff))
