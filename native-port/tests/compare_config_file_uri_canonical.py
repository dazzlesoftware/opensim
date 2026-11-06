import argparse, pathlib, subprocess, json, itertools, random

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', default='dotnet')
p.add_argument('--work-dir', required=True)
p.add_argument('--quick', action='store_true')
a = p.parse_args()
w = pathlib.Path(a.work_dir).resolve()
w.mkdir(parents=True, exist_ok=True)
(w / 'Program.cs').write_bytes(
    pathlib.Path(__file__).with_name('config_file_uri_canonical_reference.cs').read_bytes()
)
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '-v:q'], check=True
)
values = [''.join(s) for n in range(5) for s in itertools.product('./\\%?#a', repeat=n)]
values += ['%' + format(c, '02X') for c in range(256)]
if not a.quick:
    values += [chr(c) for c in range(65536)]
    r = random.Random(8513)
    values += [
        ''.join(r.choice('./\\%?#a\ud800\udc00\uffff\0{[') for n in range(r.randrange(100)))
        for i in range(10000)
    ]
rows = [
    f'{flags} {delimiter} ' + (s.encode('utf-16-be', 'surrogatepass').hex() or '-')
    for flags in range(4)
    for delimiter in [63, 35, 65535, 65534, 47, 64]
    for s in values
]
payload = '\n'.join(rows) + '\n'
reference = subprocess.check_output(
    [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll')], input=payload, text=True
).splitlines()
native = subprocess.check_output(
    [str(pathlib.Path(a.native).resolve())], input=payload, text=True
).splitlines()
if reference != native:
    for i, (x, y) in enumerate(zip(reference, native)):
        if x != y:
            print('MISMATCH', i, rows[i], x, y)
            (w / 'mismatch.json').write_text(
                json.dumps({'row': rows[i], 'reference': x, 'native': y})
            )
            break
    raise SystemExit(1)
report = {
    'cases': len(rows),
    'equal': True,
    'quick': a.quick,
    'scope': 'actual file-syntax CheckCanonical flags/position with raw UTF16 and delimiter rules',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
