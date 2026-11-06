import argparse, itertools, json, random, subprocess, hashlib, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(v):
    return '~' if v is None else v.encode('utf-16-be', 'surrogatepass').hex() or '-'


def call(cmd, rows=None):
    p = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        text=True,
        capture_output=True,
        encoding='utf-8',
        timeout=180,
    )
    assert p.returncode == 0, (p.returncode, p.stdout[-1000:], p.stderr[-1000:])
    return p.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', default='dotnet')
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--full', action='store_true')
    a = p.parse_args()
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
    ).items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(ROOT / 'native-port/tests/config_http_location_reference.cs'),
    )
    cs = w / 'LocationReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'])
    rows = []
    values = [
        None,
        '',
        ' ',
        '\t',
        'foo',
        '1:b',
        './a:b',
        ':foo',
        'http:foo',
        'http:',
        'http:/x',
        'http:?q',
        'http:#f',
        'http://',
        'http:///x',
        'http://x',
        'HTTP://X:80/a%20b',
        'http://u%20x@x/a',
        'http://u x@x/a',
        'http://u%41@x/Ã©',
        'http://@x/a',
        'http://user:@x/a',
        'http://:pass@x/a',
        'http://Ã©.com/a%41?x=%2F#Ã©',
        'http://[::1]/a',
        'http://x:65536/',
        '/x',
        '/Ã©%41',
        'Ã©%41',
        'a%41%20%GG',
        '?q',
        '#f',
        '%GG',
        '%00',
        'x\ud800y',
    ]
    for value in values:
        rows += ['P ' + enc(value), 'D ' + enc(value)]
    for value, kind in itertools.product(values, [0, 1, 2]):
        # Absolute rooted slash paths belong to the coordinated file URI provider.
        if value is not None and value.startswith('/') and kind == 1:
            continue
        rows.append('C ' + enc(value) + ' ' + str(kind))
    for value, kind in itertools.product(
        [None, '', 'x', 'http://x/'], [-2147483648, -1, 0, 1, 2, 3, 2147483647]
    ):
        rows.append('C ' + enc(value) + ' ' + str(kind))
        rows.append('T ' + enc(value) + ' ' + str(kind))
    bases = [
        'http://x/a/b',
        'http://x/a/b#base',
        'https://x/a/b?q#base',
        'http://user:@x:80/a',
        'http://:pass@x/a',
        'http://Ã©.com/a%20b',
        'http://[::1]:81/a',
    ]
    relative = [
        '',
        ' ',
        'foo',
        '/next',
        '?q',
        '#f',
        '#',
        'http:foo',
        'http:?q',
        'http:#f',
        'http:',
        '../x',
        'Ã©%41',
        '/Ã©%41',
        'http://x/next',
        'https://x/next',
        'http://x:65536/',
    ]
    for base, value in itertools.product(bases, relative):
        rows.append('O ' + enc(base) + ' ' + enc(value))
    for base, fragment in itertools.product(
        bases, [None, '', '#', 'frag', '#frag', 'Ã©%41', 'a b', 'a#b', '?x', '\ud800']
    ):
        rows.append('B ' + enc(base) + ' ' + enc(fragment))
    for base, status, value in itertools.product(
        bases, [0, 200, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 404, 999], relative
    ):
        rows.append('R ' + enc(base) + ' ' + str(status) + ' ' + enc(value))
    for first, last in itertools.product(['', '/first', 'http://', 'http:next'], repeat=2):
        rows.append('R ' + enc(bases[0]) + ' 302 ' + enc(first) + ' ' + enc(last))
    for request, status, value in itertools.product(
        [None, 'relative'], [200, 300, 302, 308], ['foo', 'http://x/a', None]
    ):
        rows.append('R ' + enc(request) + ' ' + str(status) + ' ' + enc(value))
    if a.full:
        for c in map(chr, range(65536)):
            for value in [
                'x' + c + 'y',
                '/x' + c + 'y',
                'http://x/a' + c + 'b',
                'http://x/?a' + c + 'b',
                'http://x/#a' + c + 'b',
            ]:
                if ':' in value and not value.startswith('http:'):
                    continue
                rows.append('P ' + enc(value))
                rows.append('R ' + enc(bases[1]) + ' 302 ' + enc(value))
            rows.append('D ' + enc('a' + c + 'b'))
            rows.append('C ' + enc('x' + c + 'y') + ' 0')
            rows.append('B ' + enc(bases[0]) + ' ' + enc('a' + c + 'b'))
        rng = random.Random(61017)
        parts = [
            'a',
            '%20',
            '%41',
            '%GG',
            '%C3%A9',
            '\ud800',
            '\udc00',
            'Ã©',
            'ðŸ˜€',
            '?',
            '#',
            ';',
            '@',
            ' ',
            '\t',
            '\\',
            '.',
            '..',
            '%2F',
        ]
        for _ in range(10000):
            value = 'x' + ''.join(rng.choice(parts) for _ in range(rng.randrange(12)))
            rows.append('P ' + enc(value))
            rows.append('R ' + enc(bases[rng.randrange(len(bases))]) + ' 302 ' + enc(value))
    for v in [
        'http://x/a#é%20',
        'http://x/a#é%09',
        'http://x/a#%C3%A9%20',
        'http://x/a%20',
        'http://x/a?é%20',
        '  http://u%41@X/a/../b?q#f  ',
    ]:
        rows.append('P ' + enc(v))
    source = call([a.dotnet, str(w / 'app/LocationReference.dll')], rows)
    native = call([a.native, '--probe'], rows)
    assert len(source) == len(native) == len(rows)
    for name, lines in [('inputs.txt', rows), ('actual-bcl.txt', source), ('native.txt', native)]:
        (w / name).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    diff = [
        dict(index=i, row=r, source=s, native=n)
        for i, (r, s, n) in enumerate(zip(rows, source, native))
        if s != n
    ]
    (w / 'location-results.json').write_text(
        json.dumps(
            dict(
                cases=len(rows),
                passed=not diff,
                differences=diff,
                hashes={
                    name: hashlib.sha256((w / name).read_bytes()).hexdigest()
                    for name in ["inputs.txt", "actual-bcl.txt", "native.txt"]
                },
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    print(len(rows), len(diff))
    print([(d['index'], d['row'][:70], d['source'][:70], d['native'][:70]) for d in diff[:10]])
    assert not diff


if __name__ == '__main__':
    main()
