import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    if s is None:
        return '~'
    raw = s.encode('utf-16-le', 'surrogatepass')
    return ''.join(f'{raw[i]|(raw[i+1]<<8):04x}' for i in range(0, len(raw), 2)) or '-'


def call(cmd, rows=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=600,
    )
    assert r.returncode == 0, (r.returncode, r.stderr[-2000:], r.stdout[-2000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    application = (ROOT / 'OpenSim/Region/Application/Application.cs').read_text(
        encoding='utf-8-sig'
    )
    start = application.index('            configSource.Alias.AddAlias("On", true);')
    end = application.index('            configSource.AddConfig("Network");', start) + len(
        '            configSource.AddConfig("Network");'
    )
    early = 'configSource.AddSwitch("Startup", "logconfig");'
    assert early in application
    setup = w / 'ApplicationArgvSetup.cs'
    setup.write_text(
        'using Nini.Config; static class ApplicationArgvSetup { public static void Apply(ArgvConfigSource configSource) { '
        + early
        + '\n'
        + application[start:end]
        + '\n} }',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        Nullable='disable',
    ).items():
        ET.SubElement(props, k).text = v
    g = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(g, 'Compile', Include=str(setup))
    ET.SubElement(g, 'Compile', Include=str(ROOT / 'native-port/tests/nini_argv_reference.cs'))
    r = ET.SubElement(g, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'NiniArgvReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    rows = []

    def add(*s):
        rows.append('|'.join(s))

    def arr(mode, values):
        return mode + ' ' + ' '.join(map(enc, values))

    for c in map(chr, range(65536)):
        add('U ' + enc(c))
        if ord(c) < 256 or ord(c) % 127 == 0:
            add(arr('P', ['--' + c + '=v']), 'G ' + enc(c))
            add('C ' + enc(c))
    for value in [None, '', ' ', '\n', 'a', '"', '\t', '\u0085', '\u200b']:
        add('C ' + enc(value))
    flags = [
        '-x',
        '--x',
        '/x',
        '-x=',
        '-x:value',
        '--x=a=b',
        '--x:a:b',
        '-x"a"',
        "--x='a'",
        '--x\n',
        '--x=\n',
        '--x=a\nb',
        '---x',
        '-/x',
        'x',
        '--',
        '--x\r\n',
        '--x\n\n',
        '--x\x00z',
    ]
    for first, second in itertools.product(flags, flags):
        add(arr('P', [first, second]))
    for first, second in itertools.product(
        ['--x', '--X', '/x', '--İ', '--i', '--K', '--k', '--ẞ', '--ß'], repeat=2
    ):
        add(arr('P', [first, second]))
    for value in ['', "''", '"a"', "'a'", 'a\n', '-', '--', '  foo  ']:
        add(arr('P', ['--x', value, 'second']), 'G ' + enc('x'))
    for config, long, short in itertools.product(
        [None, '', 'Startup'], [None, '', 'x', 'X'], [None, '', 'x', 'xx', 'xxx', 'İ']
    ):
        add(arr('S', ['--x=v']), f'A {enc(config)} {enc(long)} {enc(short)}', 'SN')
    for autosave in [0, 1]:
        add(arr('S', ['--x=v']), f'AS {autosave}', f'A {enc("s")} {enc("x")} ~', 'SN', 'V', 'R')
    add(
        arr('S', ['--x=v']), 'M 0 ' + enc('--x=changed'), 'ARGS', f'A {enc("s")} {enc("x")} ~', 'SN'
    )
    add(arr('S', ['--background=On', '--inifile=test.ini']), 'APP', 'SN')
    add('P !', 'G ' + enc('x'))
    add('S !', 'APP', 'ARGS', 'SN')
    add('P ~')
    add('S ~')
    add(arr('P', [None]))
    add(arr('S', [None]))
    add(arr('P', ['--x=v']), 'G ~')
    for mode in [1, 2, 3]:
        add(arr('S', ['--x=v']), f'EV {mode}', f'A {enc("s")} {enc("x")} ~', 'SN')
    for value in ['value', "'value'", '', '"']:
        add(arr('S', ['--x', value]), f'A {enc("s")} {enc("x")} ~', f'ID {enc("s")} {enc("x")} 1')
    # Raw UTF16, including supplementary scalars and deliberately malformed pairs.
    unicode_values = [
        '\U00010400',
        '\U00010428',
        '\U0001f600',
        '\U00020000',
        '\ud800',
        '\udfff',
        '\ud800x',
        'x\udc00',
        '\ud800\ud800',
        '\udc00\ud800',
        '\ud800\udc00',
        '\udbff\udfff',
        'x\U00010400Y',
        '\U00010400\U00010428',
    ]
    for value in unicode_values:
        for prefix in ['--', '/', '-', '--x=', '--x:', "--x='"]:
            add(arr('P', [prefix + value]), 'G ' + enc(value), 'G ' + enc('x'))
        for wrapped in [value, "'" + value + "'", '"' + value + '"', "''" + value + '"']:
            add(
                arr('S', ['--x', wrapped]),
                f'A {enc("s")} {enc("x")} ~',
                'SN',
                f'ID {enc("s")} {enc("x")} 1',
            )
        add(arr('P', ['--' + value, '--' + value]))
        add('C ' + enc(value))
    spaces = (
        ['\t', '\n', '\v', '\f', '\r', ' ', '\u0085', '\u00a0', '\u1680']
        + [chr(i) for i in range(0x2000, 0x200B)]
        + ['\u2028', '\u2029', '\u202f', '\u205f', '\u3000', '\u200b', '\ufeff']
    )
    for space in spaces:
        for value in [space + 'v', 'v' + space, space + 'v' + space, space]:
            add(arr('P', ['--x', value]), 'G ' + enc('x'))
            add('C ' + enc(value))
            add(arr('P', [value + '--x=1']), 'G ' + enc('x'))
    rng = random.Random(7321)
    alphabet = 'axXY-_/:=\n\r\t\x00\"\' éİKΩ012'
    for _ in range(12000):
        add(
            arr(
                'P',
                [
                    ''.join(rng.choice(alphabet) for _ in range(rng.randrange(15)))
                    for _ in range(rng.randrange(1, 5))
                ],
            )
        )
    expected = call([a.dotnet, str(w / 'app/NiniArgvReference.dll')], rows)
    actual = call([a.native, '--probe'], rows)
    assert len(expected) == len(actual) == len(rows), (len(expected), len(actual), len(rows))
    differences = [
        dict(index=i, row=row, source=s, native=n)
        for i, (row, s, n) in enumerate(zip(rows, expected, actual))
        if s != n
    ]
    (w / 'rows.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8')
    (w / 'source.txt').write_text('\n'.join(expected) + '\n', encoding='utf-8')
    (w / 'native.txt').write_text('\n'.join(actual) + '\n', encoding='utf-8')
    report = dict(
        cases=len(rows),
        passed=not differences,
        niniSha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
        applicationSha256=hashlib.sha256(
            (ROOT / 'OpenSim/Region/Application/Application.cs').read_bytes()
        ).hexdigest(),
        differences=differences[:30],
    )
    (w / 'nini-argv-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, json.dumps(differences[:5])
    print('PASS', len(rows), 'actual bundled Nini argv cases')


if __name__ == '__main__':
    main()
