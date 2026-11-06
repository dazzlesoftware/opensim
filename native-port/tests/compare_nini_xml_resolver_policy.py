import argparse, pathlib, subprocess, json, itertools

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', default='dotnet')
p.add_argument('--work-dir', '--work', dest='work_dir', required=True)
a = p.parse_args()
w = pathlib.Path(a.work_dir).resolve()
w.mkdir(parents=True, exist_ok=True)
(w / 'Program.cs').write_bytes(
    pathlib.Path(__file__).with_name('nini_xml_resolver_policy_reference.cs').read_bytes()
)
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><RuntimeFrameworkVersion>8.0.31</RuntimeFrameworkVersion><RollForward>Disable</RollForward></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '-v:q'], check=True
)
rows = [''.join(v) for n in range(8) for v in itertools.product('nNdDg', repeat=n)]
payload = '\n'.join(rows) + '\n'
reference = subprocess.check_output(
    [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll')], input=payload, text=True
).splitlines()
native = subprocess.check_output(
    [str(pathlib.Path(a.native).resolve()), "--probe"], input=payload, text=True
).splitlines()
if reference != native:
    for i, (x, y) in enumerate(zip(reference, native)):
        if x != y:
            print('MISMATCH', i, rows[i], x, y)
            break
    raise SystemExit(1)
report = {
    'cases': len(rows) + 1,
    'equal': True,
    'scope': 'actual System.Xml networking switch getter/cache under public AppContext switch changes; private cache reset isolates flows',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
