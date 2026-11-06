"""Verbatim Util.Glob source with recording Path/Directory providers and actual filesystem."""

import argparse, ast, hashlib, itertools, json, os, random, subprocess, sys, tempfile, xml.etree.ElementTree as ET
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


def source_method():
    original = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    start = original.index('public static string[] Glob(string path)')
    opening = original.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if original[end] == '{':
            depth += 1
        elif original[end] == '}':
            depth -= 1
        end += 1
    return original[start:end]


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
    method = source_method()
    generated = w / 'UtilGlobSource.cs'
    generated.write_text(
        'using System;\nusing System.Collections.Generic;\nusing Path=Shim.Path;\nusing Directory=Shim.Directory;\npublic static class SourceUtil {\n'
        + method
        + '\n}\n',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for file in [ROOT / 'native-port/tests/glob_reference.cs', generated]:
        ET.SubElement(group, 'Compile', Include=str(file))
    cs = w / 'GlobReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    source = [a.dotnet, str(w / 'app/GlobReference.dll')]
    rows = []

    def add(
        path,
        volume=58,
        directory=92,
        alternate=47,
        point=0,
        nth=1,
        scenario=1,
        mode='G',
        second=None,
    ):
        row = f'{mode} {volume} {directory} {alternate} {point} {nth} {scenario} {enc(path)}'
        if mode == 'B':
            row += ' ' + enc(second)
        rows.append(row)

    paths = [
        None,
        '',
        '/',
        '\\',
        '///\\',
        'a',
        'a/b',
        'a\\b',
        'a/b/c',
        'a//b///c',
        'a/b/c/d',
        ':',
        '::',
        ':a',
        'a:',
        ':a:',
        ':a::b:c',
        'a:::b:c',
        'a:b/c',
        'a/b:c/d',
        'C:\\x\\*.ini',
        'C:relative',
        '\\\\server\\share\\file',
        'a\0b',
        '\ud800',
        'a\ud800b/\udfff',
        '😀/配置',
    ]
    for path, scenario, point, nth in itertools.product(paths, range(4), range(4), [1, 2, 3, 7]):
        add(path, scenario=scenario, point=point, nth=nth)
    for volume, directory, alternate in itertools.product([0, 47, 58, 92, 97, 0xD800], repeat=3):
        for path, scenario in itertools.product(paths[:20], [0, 1]):
            add(path, volume, directory, alternate, scenario=scenario)
    for unit in range(65536):
        char = chr(unit)
        add(char)
        add('prefix', mode='B', second=char)
    combinations = [
        '',
        'a',
        'a:',
        'C:',
        'C:/',
        'C:\\',
        'C:relative',
        '\\root',
        '/root',
        '\\\\server\\share',
        '1:invalid',
        '\0',
        '\ud800',
        '😀',
    ]
    for first, second in itertools.product(combinations, repeat=2):
        add(first, mode='B', second=second)
    rng = random.Random(66571)
    alphabet = 'ab/:\\*.? \x00\ud800\udfff'
    for _ in range(3000):
        add(
            ''.join(rng.choices(alphabet, k=rng.randrange(35))),
            scenario=rng.randrange(4),
            point=rng.randrange(4),
            nth=rng.choice([1, 2, 3, 7]),
        )
    container = Path(tempfile.mkdtemp(prefix='glob-files-', dir=w))
    root = container / 'root'
    root.mkdir()
    for name in ['a', 'b', 'empty', 'folder.ini']:
        (root / name).mkdir()
    for name in ['first.ini', 'FIRST.INI', 'noext', '.hidden', '配置😀.ini']:
        (root / name).write_bytes(b'x')
    for folder in ['a', 'b']:
        for name in ['one.ini', 'two.txt']:
            (root / folder / name).write_bytes(b'x')
        (root / folder / 'nested').mkdir()
        (root / folder / 'nested' / 'deep.ini').write_bytes(b'x')
    long_root = (
        root
        / ('long-' + 'x' * 60)
        / ('more-' + 'x' * 60)
        / ('deep-' + 'x' * 60)
        / ('last-' + 'x' * 60)
    )
    long_root.mkdir(parents=True)
    (long_root / 'long.ini').write_bytes(b'x')
    fixture_patterns = [
        '*',
        '*.*',
        '*.ini',
        '*.INI',
        '?.ini',
        'noext',
        'missing',
        'a/*',
        'a/*.ini',
        '*/*.ini',
        '*/nested/*.ini',
        'a//nested///deep.ini',
        'empty/*',
        'folder.ini/*',
        'a/../*.ini',
        '配置😀.*',
    ]
    actual_paths = [str(root / name) for name in fixture_patterns] + [
        str(root),
        str(root) + os.sep,
        str(long_root / '*.ini'),
        None,
        '',
        '///',
    ]
    actual_paths += [
        str(root) + os.sep + '.',
        str(root) + os.sep + '.' + os.sep + '*.ini',
        str(root) + os.sep + '..' + os.sep + '*.ini',
        str(root) + os.sep + 'bad\0name',
    ]
    if os.name == 'nt':
        actual_paths += [str(root / name)[2:] for name in fixture_patterns]
        actual_paths += ['Q:\\', 'C:', '\\\\server\\share\\*.ini']
    # Bare relative spelling intentionally starts at the volume/filesystem root in
    # Util.Glob; a drive-stripped absolute fixture tests this without broad scans.
    for path in actual_paths:
        add(path, mode='F')
    outputs = [call(source, rows, root), call([a.native, '--probe'], rows, root)]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    differences = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]
    counts = {kind: sum(row.startswith(kind + ' ') for row in rows) for kind in ['G', 'B', 'F']}
    runtime = call(source + ['--runtime'])
    report = dict(
        passed=not differences,
        executedPlatform=sys.platform,
        cases=len(rows),
        counts=counts,
        bclVersion=runtime[0],
        bclSha256=hashlib.sha256(Path(runtime[1]).read_bytes()).hexdigest(),
        originalMethodSha256=hashlib.sha256(method.encode()).hexdigest(),
        fixtureRoot=str(root),
        scope='Verbatim original Util.Glob body, Path/Directory aliases expose controlled ordered faults and rawUTF16 separator/split/count2 behavior. Nonnull native Path.Combine closure compared to actual BCL, including all BMP units. Actual native filesystem Globs preserve provider order, duplicate results and root-based relative spelling; only reported host executed.',
        differences=differences[:4],
    )
    (w / 'glob-results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert not differences, json.dumps(differences[:1])
    print('PASS:', len(rows), 'Glob cases', counts)


if __name__ == '__main__':
    main()
