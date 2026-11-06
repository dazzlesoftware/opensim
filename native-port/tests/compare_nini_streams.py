"""Compare consumed default BCL text streams, native file sharing and buffering."""

import argparse, itertools, json, random, subprocess, shutil, tempfile, os, hashlib, sys, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def call(cmd, rows=None, cwd=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=180,
        cwd=cwd,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr[-2000:])
    return r.stdout.splitlines()


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


def differences(rows, outputs):
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    return [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
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
        group, 'Compile', Include=str(ROOT / 'native-port/tests/nini_streams_reference.cs')
    )
    reference = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'NiniStreamsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    source = [a.dotnet, str(w / 'app/NiniStreamsReference.dll')]
    rows = []

    def add(kind, data=b'', chunk=1024, point=0, nth=1, flags=7, pos=0, ops=None):
        rows.append(
            f'{kind} {data.hex() or "."} {chunk} {point} {nth} {flags} {pos} '
            + (
                ops
                or (
                    'p,p,r,p,all,r,p,append:6100,r,all,c,c,r,p'
                    if kind == 0
                    else 'w:7800,f,c,c,w:-,w:.,w:7800,f,t'
                )
            )
        )

    inputs = [
        b'',
        b'abc',
        b'\xef',
        b'\xef\xbb',
        b'\xef\xbb\xbf',
        b'\xff\xfe',
        b'\xff\xfe\x00',
        b'\xff\xfe\x00\x00',
        b'\x00\x00\xfe\xff',
        b'\xfe\xff',
        b'\xe0\xa0',
        b'\xed\xa0\x80',
        b'\xf4\x90\x80\x80',
    ]
    for encoding, bom in [
        ('utf-8', b'\xef\xbb\xbf'),
        ('utf-16-le', b'\xff\xfe'),
        ('utf-16-be', b'\xfe\xff'),
        ('utf-32-le', b'\xff\xfe\x00\x00'),
        ('utf-32-be', b'\x00\x00\xfe\xff'),
    ]:
        for text in ['a\n\r\ufeff😀\x00', '\ud800x\udc00', 'a' * 1022 + '😀x', '\ufeff\ufeff']:
            b = text.encode(encoding, 'surrogatepass')
            inputs.extend([bom + b, bom + b + b'\xff', bom + b[:-1]])
    for data, chunk, point, nth in itertools.product(
        inputs, [1, 2, 3, 4, 7, 1024], range(5), [1, 2, 3]
    ):
        add(0, data, chunk, point, nth)
    for i in range(65536):
        add(0, i.to_bytes(2, 'little'), [1, 2, 3][i % 3], ops='p,all,p,c')
    values = [
        None,
        '',
        'a',
        'ab',
        'abc',
        'abcd',
        'abcde',
        '\ud800',
        '\udc00',
        '😀',
        '\ud800x',
        'x\udc00',
        'a' * 1023 + '\ud800',
        'a' * 1024,
        'a' * 1025,
        'a' * 2049,
        'a' * 1023 + '😀',
        '\ud800' * 2,
    ]
    for text, point, nth, pos, flags in itertools.product(
        values, range(10), [1, 2, 3], [0, 3], [7, 3, 6]
    ):
        add(
            1,
            b'ABC',
            point=point,
            nth=nth,
            pos=pos,
            flags=flags,
            ops=f'w:{enc(text)},w:00dc,f,w:7800,f,c,c,w:-,w:.,f,t',
        )
    for first, second in itertools.product(values[:12], repeat=2):
        add(1, ops=f'w:{enc(first)},f,w:{enc(second)},f,c')
    for flags, point, kind in itertools.product(range(8), range(9), range(2)):
        add(kind, flags=flags, point=point)
    rng = random.Random(9811)
    for _ in range(3000):
        if rng.randrange(2):
            add(
                0,
                bytes(rng.randrange(256) for _ in range(rng.randrange(30))),
                rng.choice([1, 2, 3, 4, 7, 1024]),
                rng.randrange(5),
                rng.randrange(1, 4),
            )
        else:
            add(
                1,
                ops=','.join(
                    rng.choice(['f', 'c', 't'] + [f'w:{enc(v)}' for v in values[:12]])
                    for _ in range(10)
                ),
                point=rng.randrange(10),
                nth=rng.randrange(1, 4),
            )
    files = Path(tempfile.mkdtemp(prefix='nini-streams-files-', dir=w))
    file_rows = []

    def fileadd(kind, path, ops):
        file_rows.append(f'{kind} {enc(str(path) if isinstance(path,Path) else path)} {ops}')

    for kind, ops in itertools.product(
        [1, 2],
        [
            'inspect,w:7800,inspect,f,inspect,c,inspect,c',
            'w:' + enc('a' * 1025) + ',inspect,w:' + enc('a' * 4000) + ',inspect,f,inspect,c',
            'openr,openw,inspect,c,inspect',
            'w:00d8,f,w:7800,c,inspect',
            'w:-,w:.,f,c,w:-,w:.,w:7800,t',
            'raw:616263,inspect,sf,inspect,sc,w:7800,f,c',
        ],
    ):
        fileadd(kind, files / 'native.ini', ops)
    for text in [
        'ascii',
        '配置😀',
        'trailing.',
        'trailing ',
        'CON',
        'nul',
        'missing/child',
        'bad*name',
        'bad?name',
        'x' * 256,
    ]:
        for kind in range(3):
            fileadd(kind, files / text, 'p,all,c' if kind == 0 else 'w:7800,f,c')
    for path in [None, '', ' ', '\0', str(files), str(files / 'missing.ini')]:
        for kind in range(3):
            fileadd(kind, path, 'c')
    long_directory = (
        files
        / ('long-segment-' + 'x' * 60)
        / ('more-' + 'x' * 60)
        / ('deep-' + 'x' * 60)
        / ('final-' + 'x' * 60)
    )
    long_directory.mkdir(parents=True)
    long_file = long_directory / 'long-配置😀.ini'
    fileadd(2, long_file, 'w:7800,inspect,f,inspect,c')
    if os.name == 'nt':
        for path in [
            '\\\\?\\' + str(long_file),
            '\\\\.\\' + str(long_file),
            os.path.relpath(long_file, files),
        ]:
            fileadd(2, path, 'w:7800,f,c')
    for kind in range(3):
        fileadd(kind, str(files / 'raw-\ud800-é😀'), 'p,all,c' if kind == 0 else 'w:7800,f,c')
    initial = [
        ('utf8', b'\xef\xbb\xbf' + ('a' * 1023 + '😀end').encode()),
        ('utf16', b'\xff\xfe' + ('a' * 2047 + '😀end').encode('utf-16-le')),
    ]
    for name, data in initial:
        fileadd(0, files / (name + '.ini'), 'p,r,inspect,all,c,r')
        fileadd(0, files / (name + '.ini'), 'p,sc,r,all,p,c,p')

    def file_call(cmd):
        assert files.parent == w and files.name.startswith('nini-streams-files-')
        shutil.rmtree(files)
        files.mkdir()
        long_directory.mkdir(parents=True)
        for name, data in initial:
            (files / (name + '.ini')).write_bytes(data)
        # Relative paths (including a valid Unix whitespace-only filename) belong to
        # the same reset fixture as absolute paths. Neither run may inherit files
        # created by the other run or touch CTest's working directory.
        return call(cmd + ['--file-probe'], file_rows, cwd=files)

    ini_rows = []
    ini_paths = []
    for kind, ft, value in itertools.product(
        [0, 1],
        [-1, 0, 1, 2, 3, 4, 99],
        [None, '', 'ASCII', '配置😀', '\ud800', 'a\nb\rc', 'a' * 2049],
    ):
        path = files / f'ini-{len(ini_rows)}.ini'
        ini_paths.append(path)
        ini_rows.append(f'{kind} {enc(str(path))} {ft} {enc(value)}')
    for ft in [-1, 0, 1, 2, 3, 4, 99]:
        ini_rows.append(f'2 {enc(str(files/"utf8.ini"))} {ft} .')
    for name, data in initial:
        (files / (name + '.ini')).write_bytes(data)
    ini_source = call(source + ['--ini-file-probe'], ini_rows)
    source_bytes = [path.read_bytes().hex() for path in ini_paths]
    ini_native = call([a.native, '--ini-file-probe'], ini_rows)
    native_bytes = [path.read_bytes().hex() for path in ini_paths]
    ini_diff = differences(ini_rows, [ini_source, ini_native])
    ini_bytes_diff = [
        dict(index=i, source=s, native=n)
        for i, (s, n) in enumerate(zip(source_bytes, native_bytes))
        if s != n
    ]
    file_diff = differences(file_rows, [file_call(source), file_call([a.native])])
    diff = differences(rows, [call(source, rows), call([a.native, '--probe'], rows)])
    runtime = call(source + ['--runtime'])
    core = Path(runtime[1])
    report = dict(
        executedPlatform=sys.platform,
        bclVersion=runtime[0],
        bclSha256=hashlib.sha256(core.read_bytes()).hexdigest(),
        niniSha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
        passed=not diff and not file_diff and not ini_diff and not ini_bytes_diff,
        cases=len(rows),
        fileCases=len(file_rows),
        iniFileCases=len(ini_rows),
        iniDifferences=ini_diff[:3],
        iniBytesDifferences=ini_bytes_diff[:3],
        scope='Actual .NET8 default StreamReader consumed Peek/ReadChar/Close and StreamWriter Write(string)/Flush/Close/ToString with arbitrary chunk/fault byte providers; actual native consumed FileStream buffer/share/path effects. This run verifies only the reported host platform; unrelated FileStream modes/options require external provider. Nini file output bytes compared after controlled source GC; arbitrary finalization timing remains separate.',
        differences=diff[:3],
        fileDifferences=file_diff[:3],
    )
    (w / 'nini-streams-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff and not file_diff and not ini_diff and not ini_bytes_diff, json.dumps(
        (diff or file_diff or ini_diff or ini_bytes_diff)[:1]
    )
    print(
        'PASS:',
        len(rows),
        'BCL text streams and',
        len(file_rows),
        'file cases and',
        len(ini_rows),
        'actual Nini file cases',
    )


if __name__ == '__main__':
    main()
