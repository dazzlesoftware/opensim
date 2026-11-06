import argparse, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
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
ET.SubElement(items, 'Compile', Include=str(root / 'config_http_windows_certificate_reference.cs'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
r = subprocess.run([a.dotnet, str(w / 'app/Reference.dll')], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
rows = json.loads(r.stdout)
assert len(rows) == 552
lines = []
for row in rows:
    roots = row['roots']
    parts = [
        row['leaf'] or '_',
        str(len(row['extra'])),
        *row['extra'],
        str(int(roots is not None)),
        str(len(roots or [])),
        *(roots or []),
        str(row['time']),
        str(int(row['checkName'])),
        '1',
        row['host'] or '_',
    ]
    lines.append(' '.join(parts))
r = subprocess.run(
    [a.native], input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=120
)
assert r.returncode == 0, r.stdout + r.stderr
values = [int(x) for x in r.stdout.splitlines()]
assert len(values) == len(rows)
observations = [
    dict(
        label=row['label'],
        host=row['host'],
        systemTrust=row['roots'] is None,
        checkName=row['checkName'],
        source=row['expected'],
        native=value,
    )
    for row, value in zip(rows, values)
]
differences = [x for x in observations if x['source'] != x['native']]
(w / 'inputs.json').write_text(json.dumps(rows, indent=2) + '\n')
report = dict(
    passed=not differences,
    cases=len(rows),
    differences=differences,
    observations=observations,
    applicationParityVerified=False,
    scope='Actual pinned Windows runtime CertificateValidation.BuildChainAndVerifyProperties and X509Chain versus native Crypt32: NoCheck revocation, server-auth EKU, fixed verification time, peer ExtraStore, system/custom/empty roots, DNS/IP/wildcard/CN, missing and invalid/expired/not-yet-valid certificates. Temporary in-memory custom stores only; certificate downloads disabled in fixtures; no live TLS/revocation/cancellation proof.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)} actual Windows certificate chain/name policies')
