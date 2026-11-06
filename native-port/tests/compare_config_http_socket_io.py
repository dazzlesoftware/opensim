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
ET.SubElement(items, 'Compile', Include=str(root / 'config_http_socket_io_reference.cs'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
r = subprocess.run([a.dotnet, str(w / 'app/Reference.dll')], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
source = [json.loads(line) for line in r.stdout.splitlines()]
assert len(source) == 40
codes = [10013, 10049, 10050, 10051, 10053, 10054, 10058, 10060, 10061, 10065]
rows = [x for x in source if not x['write'] or x['headers']]
payload = ''.join(f"{codes.index(x['code'])} {int(x['write'])} {int(x['headers'])}\n" for x in rows)


def token(e):
    value = ';'.join(
        [
            e['type'],
            e['Message'].encode('utf-16-be', errors='surrogatepass').hex(),
            str(e['HResult'] & 0xFFFFFFFF),
            str(e['requestError'] if e['requestError'] is not None else -1),
            str(e['nativeError'] if e['nativeError'] is not None else -1),
            str(e['socketError'] if e['socketError'] is not None else -1),
        ]
    )
    return value + ('|' + token(e['inner']) if e['inner'] else '')


r = subprocess.run([a.native], input=payload, capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
native = r.stdout.splitlines()
assert len(native) == len(rows) == 30
observations = [
    dict(code=x['code'], write=x['write'], headers=x['headers'], source=token(x['error']), native=n)
    for x, n in zip(rows, native)
]
differences = [x for x in observations if x['source'] != x['native']]
report = dict(
    passed=not differences,
    cases=len(rows),
    differences=differences,
    observations=observations,
    scope='Actual pinned runtime NetworkStream.WrapException and HttpConnection.MapSendException, non-cancelled path, ten mapped native socket codes. Read body/header and request-write failures only; no claim of live timeout, cancellation or retry parity.',
    applicationParityVerified=False,
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)} actual-runtime socket I/O exception chains')
