"""Actual default BCL Directory enumeration and internal NormalizeInputs oracle."""

import argparse, hashlib, itertools, json, os, random, subprocess, sys, tempfile, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


def call(cmd, rows=None, cwd=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        cwd=cwd,
        timeout=180,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr[-2000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.native = str(Path(a.native).resolve())
    a.dotnet = str(Path(a.dotnet).resolve())
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        group, 'Compile', Include=str(ROOT / 'native-port/tests/directory_entries_reference.cs')
    )
    cs = w / 'DirectoryEntriesReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    source = [a.dotnet, str(w / 'app/DirectoryEntriesReference.dll')]
    container = Path(tempfile.mkdtemp(prefix='directory-entries-files-', dir=w))
    root = container / 'root'
    root.mkdir()
    names = [
        'alpha.txt',
        'ALPHA.TXT',
        'alpha',
        'a.b.c',
        'ab1',
        'file.fac',
        'FILE.FAC',
        'file.facx',
        '.hidden',
        'semi;name',
        '[x]',
        'éclair',
        '配置😀',
        'ſ',
        's',
        'S',
        'k',
        'K',
        'K',
    ]
    if os.name != 'nt':
        names += ['back\\slash', '"quoted"', '<literal>', 'literal?', 'literal*', 'raw-\ud800']
    for name in names:
        if '\ud800' not in name:
            (root / name).write_bytes(b'x')
    for name in ['sub', 'SUBUP', 'dir.ext', '.hidden-dir', '配置Dir', 'empty', 'many']:
        (root / name).mkdir()
    for name in ['a.txt', 'B.TXT', 'noext']:
        (root / 'sub' / name).write_bytes(b'x')
    for i in range(180):
        (root / 'many' / f'entry-{i:03}-配置.txt').write_bytes(b'x')
    symlinks = {}
    for name, target, is_dir in [
        ('link-file', 'alpha.txt', False),
        ('link-dir', 'sub', True),
        ('dangling', 'missing', False),
        ('dangling-dir', 'missing-dir', True),
        ('loop', 'loop', False),
    ]:
        try:
            os.symlink(target, root / name, target_is_directory=is_dir)
            symlinks[name] = True
        except OSError as e:
            symlinks[name] = str(e)
    attributes = {}
    if os.name == 'nt':
        junction = subprocess.run(
            ['cmd', '/d', '/c', 'mklink', '/J', str(root / 'junction-dir'), str(root / 'sub')],
            capture_output=True,
        )
        symlinks['junction-dir'] = dict(
            exitCode=junction.returncode,
            stdoutHex=junction.stdout.hex(),
            stderrHex=junction.stderr.hex(),
        )
        import ctypes

        attributes['hidden-system-file.fac'] = bool(
            ctypes.windll.kernel32.SetFileAttributesW(str(root / 'file.fac'), 2 | 4)
        )
        for name in ['trailing.', 'trailing ', 'raw-\ud800']:
            path = '\\\\?\\' + str(root / name)
            fd = os.open(path, os.O_CREAT | os.O_WRONLY, 0o666)
            os.close(fd)
    else:
        os.mkfifo(root / 'named-pipe')
        raw = os.fsencode(root) + b'/invalid-\xff'
        fd = os.open(raw, os.O_CREAT | os.O_WRONLY, 0o666)
        os.close(fd)
        os.symlink(b'sub', os.fsencode(root) + b'/invalid-link-\xff')
    long_root = (
        root
        / ('long-' + 'x' * 65)
        / ('more-' + 'x' * 65)
        / ('deep-' + 'x' * 65)
        / ('last-' + 'x' * 65)
    )
    long_root.mkdir(parents=True)
    (long_root / 'long-配置😀.txt').write_bytes(b'x')
    rows = []

    def add(mode, path, pattern, target=0, point=0, nth=1):
        rows.append(f'{mode} {target} {enc(path)} {enc(pattern)} {point} {nth}')

    paths = [
        None,
        '',
        ' ',
        'x',
        'a/',
        'a//',
        'a\\',
        'a\\\\',
        'a\0b',
        '/',
        'C:',
        'C:\\',
        '\\\\server\\share\\',
        '配置😀',
        '\ud800',
    ]
    patterns = [
        None,
        '',
        '*',
        '*.*',
        '.',
        '..',
        '?',
        '??',
        '*.txt',
        '*.TXT',
        '*a*',
        'a?.',
        '*a?b',
        'sub/*',
        'sub/',
        'sub//',
        'sub///x',
        'sub\\*',
        'sub\\\\*',
        '../*',
        './*',
        '/abs',
        '\\abs',
        'C:abc',
        '1:abc',
        'a\0b',
        '/a\0b',
        '\\a\0b',
        '<',
        '>',
        '"',
        '\\',
        'a\\b?',
        '配置😀',
        '\ud800',
    ]
    for path, pattern in itertools.product(paths, patterns):
        add('N', path, pattern)
    for code in range(65536):
        add('N', 'root', chr(code))
    for pattern, target, point, nth in itertools.product(
        patterns, range(2), range(5), [1, 2, 5, 14]
    ):
        add('P', 'original//', pattern, target, point, nth)
    actual_paths = [
        None,
        '',
        ' ',
        '.',
        './',
        './.',
        'sub',
        'sub/',
        'sub//',
        'sub/..',
        'many',
        str(root),
        str(root) + os.sep,
        str(root / 'alpha.txt'),
        str(root / 'missing'),
        str(root / 'missing' / 'child'),
        str(long_root),
    ]
    actual_patterns = patterns + [
        '*.fac',
        '*.FAC',
        '*fac*',
        'alpha*',
        'ALPHA*',
        '[x]',
        '*.*.*',
        'a.*',
        'a*.*',
        '*s',
        '*S',
        's',
        'S',
        'k',
        'K',
        '*k',
        '*K',
        'many/*.txt',
        'empty/*',
        './sub/*.txt',
        'sub/../*.txt',
    ]
    for path, pattern, target in itertools.product(actual_paths, actual_patterns, range(2)):
        add('F', path, pattern, target)
    for path, target in itertools.product(actual_paths, [2, 3]):
        add('F', path, None, target)
    if os.name == 'nt':
        for path, pattern, target in itertools.product(
            [
                '\\\\?\\' + str(root),
                '\\\\.\\' + str(root),
                '\\\\?\\' + str(long_root),
                '\\\\.\\' + str(long_root),
                os.path.relpath(long_root, root),
            ],
            ['*', '*.txt', 'sub/*'],
            range(2),
        ):
            add('F', path, pattern, target)
    rng = random.Random(55018)
    alphabet = 'ab/*?\\"<>: .\x00\ud800\u017f'
    for _ in range(2000):
        add('N', rng.choice(paths), ''.join(rng.choices(alphabet, k=rng.randrange(20))))
    outputs = [call(source, rows, root), call([a.native, '--probe'], rows, root)]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    diff = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]
    counts = {mode: sum(row.startswith(mode + ' ') for row in rows) for mode in ['N', 'P', 'F']}
    runtime = call(source + ['--runtime'])
    report = dict(
        bclVersion=runtime[0],
        bclSha256=hashlib.sha256(Path(runtime[1]).read_bytes()).hexdigest(),
        passed=not diff,
        executedPlatform=sys.platform,
        cases=len(rows),
        counts=counts,
        symlinks=symlinks,
        attributes=attributes,
        fixtureRoot=str(root),
        scope='Actual BCL Directory.GetFiles/GetDirectories nonrecursive compatibility enumeration including exact order/spelling, attributes and symlinks where fixture creation succeeds. Actual internal BCL NormalizeInputs identity/errors. Source-backed controlled outer-pipeline provider faults use actual NormalizeInputs/Path.Join/FileSystemName with recording cursors. Only reported host executed; recursion/options APIs excluded.',
        differences=diff[:5],
    )
    (w / 'directory-entries-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:1])
    print('PASS:', len(rows), 'Directory cases', counts)


if __name__ == '__main__':
    main()
