from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET
import http.server, threading

p = argparse.ArgumentParser()
p.add_argument('--reference-source', type=Path)
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
    Include=str(
        a.reference_source.resolve()
        if a.reference_source
        else d / 'config_xml_native_proxy_reference.cs'
    ),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)
payload = (
    '\n'.join(
        [
            'normal',
            'bypass',
            'bthrow',
            'gthrow',
            'noproxy',
            'null',
            'cthrow',
            'relative',
            'ftp',
            'default',
            'alternate',
        ]
    )
    + '\n'
)
(w / 'inputs.txt').write_text(payload)
out = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=45)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    out[name] = r.stdout
assert out['native'] == out['reference'], out
(w / 'results.json').write_text(
    json.dumps(
        dict(
            cases=11,
            queries=44,
            passed=True,
            applicationParityVerified=False,
            scope='Actual SocketsHttpHandler default/explicit proxy and private pool credential snapshot: first-use capture, mutation after capture, getter exceptions/reinitialization, bypass/lookup exception fallback, nullable credentials/default fallback, unsupported/relative proxy URIs. Connect callbacks stop before external sockets. Protocol authentication remains incomplete.',
        ),
        indent=2,
    )
)
print('PASS 11 native proxy binding cases, 44 handler queries')
