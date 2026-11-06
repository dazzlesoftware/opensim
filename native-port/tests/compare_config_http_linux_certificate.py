import argparse, json, os, subprocess, hashlib, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--cached-stores', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parent
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(root / 'config_http_linux_certificate_reference.cs'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
env = os.environ.copy()
home = w / 'isolated-home'
home.mkdir(exist_ok=True)
empty = w / 'empty-ca'
empty.mkdir(exist_ok=True)
env.update(HOME=str(home), SSL_CERT_FILE=str(w / 'system.pem'), SSL_CERT_DIR=str(empty))
r = subprocess.run(
    [a.dotnet, str(w / 'app/Reference.dll')], capture_output=True, text=True, env=env, timeout=120
)
assert r.returncode == 0, r.stdout + r.stderr
rows = json.loads(r.stdout)


def certs(values):
    return [str(len(values)), *values]


lines = []
for row in rows:
    roots = row['roots']
    parts = [
        row['leaf'] or '_',
        *certs(row['extra']),
        *certs(row['trusted']),
        *certs(row['userIntermediates']),
        *certs(row['systemIntermediates']),
        str(int(roots is not None)),
    ]
    if roots is not None:
        parts += certs(roots)
    parts += [
        str(row['time']),
        str(int(row['checkName'])),
        row['host'] or '_',
        str(len(row['application'])),
        *row['application'],
        str(len(row['certificate'])),
        *row['certificate'],
        *certs(row['issuers']),
    ]
    lines.append(' '.join(parts))
r = subprocess.run(
    [a.native, *(['--cached-stores'] if a.cached_stores else [])],
    input='\n'.join(lines) + '\n',
    capture_output=True,
    text=True,
    env=env,
    timeout=120,
)
assert r.returncode == 0, r.stdout[-1000:] + r.stderr
values = r.stdout.splitlines()
assert len(values) == len(rows), (len(values), len(rows))
differences = [
    dict(
        case=i,
        label=row['label'],
        host=row['host'],
        custom=row['roots'] is not None,
        checkName=row['checkName'],
        application=row['application'],
        source=row['expected'],
        native=value,
    )
    for i, (row, value) in enumerate(zip(rows, values))
    if row['expected'] != value
]
(w / 'inputs.json').write_text(json.dumps(rows, indent=2) + '\n')
report = dict(
    passed=not differences,
    cases=len(rows),
    differences=differences,
    applicationParityVerified=False,
    scope='Actual Linux runtime8.0.31 chain/name policy with NoCheck, disabled downloads, fixed UTC time and isolated HOME/SSL_CERT_FILE/SSL_CERT_DIR versus native explicit store-snapshot evaluator. Compares exact selected chain DER, ordered per-element/overall status flags, SSL policy flags and outer exceptions. Covers supplied/system/user/My intermediates, custom/nonself/empty roots, signatures, policies and issuer continuation. Native store loading/caching/AIA/revocation/TLS installation remain separate unfinished work; no machine/user real trust stores changed.',
)
report['inputSHA256'] = hashlib.sha256(('\n'.join(lines) + '\n').encode()).hexdigest()
report['outputSHA256'] = {
    'source': hashlib.sha256(
        ('\n'.join(row['expected'] for row in rows) + '\n').encode()
    ).hexdigest(),
    'native': hashlib.sha256(('\n'.join(values) + '\n').encode()).hexdigest(),
}
report['nativeStoreLoading'] = a.cached_stores
if a.cached_stores:
    report['scope'] = (
        'Actual pinned Linux runtime NoCheck/no-download chain policy versus native cached system/root/CA/My composition and chain evaluator. Native parses actual isolated SSL_CERT_FILE/SSL_CERT_DIR and HOME PFX stores; supplied store arrays in the input are ignored. Full chosen DER, status order, errors and continuation trace compared. AIA downloads/default live TLS integration remain unfinished.'
    )
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)} Linux chain/name/status scenarios')
