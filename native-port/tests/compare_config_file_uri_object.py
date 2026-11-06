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
    pathlib.Path(__file__).with_name('config_file_uri_object_reference.cs').read_bytes()
)
(w / 'reference.csproj').write_text(
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>'
)
subprocess.run(
    [a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '-v:q'], check=True
)
values = [None] + [
    'file:' + v
    for v in [
        '',
        'a',
        '/a',
        '//a',
        '///a',
        '////a',
        'c:/a',
        'c|/a',
        'c:a',
        '//c:/a',
        '///c:/a',
        '////c:/a',
        '//localhost/x',
        '//LOCALHOST/x',
        '//loopback/x',
        '//LOOPBACK/x',
        '//bad@host/x',
        '//[::1]/x',
        '//[::1%23]/x',
        '//127.1/x',
        '//0x7f.1/x',
        '/a?b#c',
        '//a?b#c',
        '///a?b#c',
        '///',
        '//',
        '/',
        '\\',
        '/\\',
        '\\/',
    ]
]
values += [
    prefix + s
    for prefix in ['file:///', 'file://host/', 'C:/', 'C|/', '\\\\server\\']
    for s in [
        '',
        'a',
        'a//b',
        'a/./b',
        'a/../b',
        '%41',
        '%2e/%2E/x',
        'a%2f../x',
        '%zz',
        '%FF%80',
        '%5c',
        'a%3Fb%23c',
        'a?b#c',
        'a?b#c#d',
        'a #b',
        'é中',
        'a\ud800x',
        'a\0b',
        '.../x',
        '../x',
        './x',
        'a.',
        'a\t\r\n',
        'a ',
    ]
]
values += [' file:///%41', '   file:///%41', ' file:///%zz', ' file:///é', ' C:/é', ' file://AÉ/x']
if not a.quick:
    values += [
        prefix + chr(c) + suffix
        for c in range(65536)
        for prefix, suffix in [('file:///a', '/x'), ('file://a', '/x')]
    ]
    values += [
        'file:' + prefix + name + '/x'
        for prefix in ['//', '///', '////']
        for name in [
            '[::]',
            '[::1]',
            '[0:0:0:0:0:0:0:1]',
            '[fe80::1%en0]',
            '[::ffff:127.0.0.1]',
            '[::127.0.0.1]',
            '[::ffff:0:127.0.0.1]',
            '[2001:db8:0:0:1:0:0:1]',
            '[::1]/64',
            '127.1',
            '2130706433',
            '0x7f000001',
            '0177.0.0.1',
            '4294967295',
            '4294967296',
            '1.2.3.4.5',
            'a_',
            '_a',
            'a..b',
            'a.',
            'é',
            'É',
            'aÉ',
            'AÉ',
            'localhost',
            'LOCALHOST',
            'loopback',
            'LOOPBACK',
        ]
    ]
    values += [' ' + s for s in values if s is not None and len(s) < 80][:200]
    r = random.Random(98712)
    values += [
        r.choice(['file:///', 'file://host/', 'C:/', '\\\\server\\'])
        + ''.join(r.choice('a.b:_-123/\\?#%\0\ud800\uffff') for n in range(r.randrange(80)))
        for i in range(10000)
    ]
rows = [
    f'{attempt} {kind} ' + ('~' if s is None else s.encode('utf-16-be', 'surrogatepass').hex())
    for attempt in range(2)
    for kind in [0, 1, 2, -1, 3]
    for s in values
]
# Bound memory while comparing repeated getters and reference identities.
for start in range(0, len(rows), 4096):
    batch = rows[start : start + 4096]
    payload = '\n'.join(batch) + '\n'
    reference = subprocess.check_output(
        [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll')], input=payload, text=True
    ).splitlines()
    native = subprocess.check_output(
        [str(pathlib.Path(a.native).resolve())], input=payload, text=True
    ).splitlines()
    if reference != native:
        for i, (x, y) in enumerate(zip(reference, native)):
            if x != y:
                print('MISMATCH', start + i, batch[i], x, y)
                (w / 'mismatch.json').write_text(
                    json.dumps({'index': start + i, 'row': batch[i], 'reference': x, 'native': y})
                )
                break
        raise SystemExit(1)
report = {
    'cases': len(rows),
    'equal': True,
    'quick': a.quick,
    'scope': 'actual new Uri/TryCreate file dispatch and retained getter values/reference identities/error type/HResult/message/parameter; Windows',
}
(w / 'report.json').write_text(json.dumps(report, indent=2))
print(report)
