import argparse, json, os, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

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
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(
    items,
    'Compile',
    Include=str(Path(__file__).resolve().with_name('config_http_linux_user_store_reference.cs')),
)
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
env = os.environ.copy()
env['HOME'] = str(w / 'home')
r = subprocess.run(
    [a.dotnet, str(w / 'app/Reference.dll')], capture_output=True, text=True, env=env
)
assert r.returncode == 0, r.stdout + r.stderr
source = json.loads(r.stdout)
rows = source['rows']
differences = []
for row in rows:
    r = subprocess.run([a.native, row['directory']], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    actual = r.stdout.splitlines()
    if actual != row['expected']:
        differences.append(dict(label=row['label'], source=row['expected'], native=actual))
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=not differences,
            cases=len(rows),
            sourceCacheAssertions=source['cacheAssertions'],
            differences=differences,
            scope='Pinned actual Linux runtime private CurrentUser directory-store loader; isolated HOME. Exact DER/order/duplicate preservation. Source private stopwatch forced at 999/1000/29999/30000ms verifies the same cache rules covered by the separate native controlled-clock test. System stores and default TLS installation pending.',
        ),
        indent=2,
    )
    + '\n'
)
assert not differences, differences
print(f'PASS: {len(rows)} actual Linux user-store loading scenarios')
