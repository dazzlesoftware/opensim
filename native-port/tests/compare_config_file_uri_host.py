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
    pathlib.Path(__file__).with_name('config_file_uri_host_reference.cs').read_bytes()
)
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '-v:q'], check=True
)
values = [
    '',
    '/',
    '\\',
    '?',
    '#',
    'localhost',
    'LOCALHOST',
    'loopback',
    '1',
    '127.1',
    '127.0.0.1',
    '0x7f.1',
    '0177.1',
    '4294967295',
    '4294967296',
    '1.2.3.4.5',
    'a:b',
    'a@b',
    'a?b',
    'a#b',
    'a/b',
    'a\\b',
    'a_',
    '_a',
    '-a',
    'a..b',
    'a.',
    'a-',
    '[::1]',
    '[::1]:80',
    '[::1]/x',
    '[::ffff:192.168.1.1]',
    '[fe80::1%23]',
    '[::1',
    '[::]',
    '[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]',
]
values += [''.join(s) for n in range(5) for s in itertools.product('a1.:/?#\\', repeat=n)]
if not a.quick:
    values += [chr(c) + '/x' for c in range(65536)]
    values += ['a' + chr(c) + '/x' for c in range(65536)]
    values += [
        'a' * n + s
        for n in [62, 63, 64, 255, 256, 257, 1024]
        for s in ['', '/a', '.x', '-', '_', '?b']
    ]
    r = random.Random(1323)
    values += [
        ''.join(r.choice('a.b:_-123/\\?#\0\ud800\uffff') for n in range(r.randrange(80)))
        for i in range(10000)
    ]
rows = []
for flags in [0, 0x20000000, 0x200000000, 0x220000000, 0x600000000, 0x620000000]:
    for value in values:
        text = 'pfx:' + value
        rows.append('4 ' + str(flags) + ' ' + text.encode('utf-16-be', 'surrogatepass').hex())
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
    'scope': 'actual CheckAuthorityHelper with file syntax; host kind/end/error/NFC rewritten prefix',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
