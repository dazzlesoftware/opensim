import argparse, pathlib, subprocess, itertools, random, json

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', default='dotnet')
p.add_argument('--work-dir', required=True)
a = p.parse_args()
w = pathlib.Path(a.work_dir).resolve()
w.mkdir(parents=True, exist_ok=True)
s = pathlib.Path(__file__).with_name('config_file_uri_path_reference.cs')
(w / 'Program.cs').write_bytes(s.read_bytes())
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '-v:q'], check=True
)
values = [''.join(v) for n in range(9) for v in itertools.product('./\\a', repeat=n)]
values += ['a/../' + chr(c) + '/..' for c in range(65536)]
r = random.Random(7981)
values += [''.join(r.choice('./\\ab%?#\0') for _ in range(r.randrange(250))) for n in range(12000)]
values += ['a/' * n + '../' * n for n in [64, 256, 1024, 8192]]
lines = [
    str(mode) + ' ' + s.encode('utf-16-be', 'surrogatepass').hex()
    for mode in range(3)
    for s in values
]
extra = ['%' + chr(c) + chr(d) for c in range(256) for d in range(256)]
extra += [''.join(v) for n in range(7) for v in itertools.product('%2eF/a', repeat=n)]
extra += ['%zz%2e%2F%5c%00%ff' + chr(c) for c in range(65536)]
lines += [
    str(mode) + ' ' + s.encode('utf-16-be', 'surrogatepass').hex() for mode in [3, 4] for s in extra
]
schemes = [chr(c) + ':/path' for c in range(65536)]
schemes += [
    prefix + s
    for prefix in ['', ' ', '\t\r\n']
    for s in [
        '',
        'a',
        'a:',
        'a|',
        'a:/',
        'a|/',
        'a:\\',
        '//host/path',
        '\\\\host/path',
        '/root',
        '\\root',
        'http:x',
        'https:x',
        'file:/a',
        'FILE:///a',
        'file://a',
        'unknown:x',
    ]
]
schemes += [
    s * count + ':/a'
    for s in ['a', '1', 'a.', 'a+', 'a-', 'a_']
    for count in [1, 2, 100, 1023, 1024, 1025, 32767, 65520]
]
lines += ['5 ' + s.encode('utf-16-be', 'surrogatepass').hex() for s in schemes]
early = ['c:/' + chr(c) for c in range(65536)]
early += [
    prefix + s
    for prefix in ['', ' ', '\t\r\n']
    for s in ['c:/a', 'C|\\a', '\\\\server\\a', '//server/a', '\\\\bad@host/a']
]
lines += ['8 ' + s.encode('utf-16-be', 'surrogatepass').hex() for s in early]
lines += [
    '6 ' + s.encode('utf-16-be', 'surrogatepass').hex()
    for s in ['//server/a', ' //server/a', ' \\\\server/a', '\t\\\\server/a']
]
payload = '\n'.join(lines) + '\n'
expected = subprocess.check_output(
    [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll')], input=payload, text=True
).splitlines()
actual = subprocess.check_output(
    [str(pathlib.Path(a.native).resolve())], input=payload, text=True
).splitlines()
if actual != expected:
    for i, (x, y) in enumerate(zip(expected, actual)):
        if x != y:
            print(i, lines[i], x, y)
            break
    raise SystemExit('file-URI compression mismatch')
report = {
    'cases': len(lines),
    'equal': True,
    'scope': 'actual private Uri.Compress three syntax profiles, UnescapeOnly and Windows ParseScheme; not complete file URI parser',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
