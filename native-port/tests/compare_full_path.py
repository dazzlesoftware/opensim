"""Actual .NET Path.GetFullPath against native OS-backed path normalization."""

import argparse, collections, ctypes, hashlib, itertools, json, os, re, subprocess, tempfile, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, rows=None, cwd=None):
    result = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        cwd=cwd,
        timeout=110,
    )
    assert result.returncode == 0, (result.returncode, result.stdout[-2000:], result.stderr[-2000:])
    return result.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    native = str(Path(a.native).resolve())
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        AllowUnsafeBlocks='true',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    source = ROOT / 'native-port/tests/full_path_reference.cs'
    ET.SubElement(group, 'Compile', Include=str(source))
    cs = w / 'FullPathReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/FullPathReference.dll')]
    root = Path(tempfile.mkdtemp(prefix='full-path-', dir=w))
    existing = root / 'Native Full Path Existing ~ Directory é漢😀'
    existing.mkdir()
    file = existing / 'Native Full Path Existing ~ File.txt'
    file.write_bytes(b'path normalization fixture')
    paths = [
        None,
        '',
        ' ',
        '  ',
        '\t',
        ' \t ',
        '\u3000',
        '\u200b',
        '\ufeff',
        'é漢😀',
        '.',
        '..',
        '...',
        '....',
        'a/..',
        'a/.',
        'a/../',
        'a/./',
        './',
        '../',
        'a\\..',
        'a\\.',
        'a\\..\\',
        'a\\.\\',
        '~',
        '~/cache',
        '%TEMP%/cache',
        '$HOME/cache',
        str(root),
        str(existing),
        str(file),
    ]
    paths += [chr(n) for n in range(32)] + ['a' + chr(n) + 'b' for n in range(32)]
    parts = ['a', 'b.', 'c ', 'd. ', 'e...', ' f ', '..', '.', '~missing']
    for left, right, separator, tail in itertools.product(
        parts, parts, ['/', '\\', '//', '\\\\'], ['', '/', '/.', '/..']
    ):
        paths.append(left + separator + right + tail)
    if os.name == 'nt':
        paths += [
            drive + suffix
            for drive, suffix in itertools.product(
                ['C:', 'G:', 'Z:', 'a:', '1:'],
                ['', 'folder', 'folder/../other', '\\', '/cache', '\\a. \\b. ', 'a:b'],
            )
        ]
        paths += [
            '\\server',
            '\\\\server',
            '\\\\server\\share',
            '\\\\server\\share\\a\\..\\b',
            '\\\\server\\share\\..\\..\\b',
            '\\\\?\\C:\\a\\..\\b',
            '\\\\?\\relative',
            '\\\\?\\C:/a/../b',
            '\\\\?\\UNC\\server\\share\\a\\..\\b',
            '\\??\\a/../b',
            '//?/C:/a/../b',
            '\\\\.\\C:\\a\\..\\b',
            '\\\\.\\relative',
            'CON',
            'NUL',
            'COM1',
            'a:b',
            'a::b',
            ':',
            '::',
            'a|b',
            'a<b',
            'a>b',
            'a"b',
            'a?b',
            'a*b',
            '\\\\?\\C:\\a\x00b',
            '\\??\\a\x00b',
        ]
    else:
        paths += ['/', '//', '///', '/a/../..', '/a/.', '/a/./', '/../a', '\\\\?\\a/../b']
    for size in [259, 260, 261, 32740, 32750, 32766, 32767, 32768, 65536]:
        path = 'a' * size
        paths += [path, str(root / path)]
        if os.name == 'nt':
            paths += ['\\\\?\\C:\\' + path]
    if os.name == 'nt':
        for size in [32750, 32767, 32768, 65536]:
            leaf = 'a' * size
            paths += [
                str(root).replace('\\', '/') + '/' + leaf + '/../tail',
                str(root) + '\\' + leaf + '\\..\\tail',
                "quote'" + leaf,
            ]
    aliases = []
    if os.name == 'nt':
        call = ctypes.windll.kernel32.GetShortPathNameW
        call.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
        call.restype = ctypes.c_uint32
        for long in [str(existing), str(file), os.environ.get('ProgramFiles', 'C:\\Program Files')]:
            buf = ctypes.create_unicode_buffer(32768)
            count = call(long, buf, len(buf))
            if count and count < len(buf):
                short = buf.value
                aliases.append(dict(long=long, short=short))
                paths += [
                    short,
                    short.lower(),
                    short + '\\missing\\..\\tail',
                    short + '\\missing\\child',
                    short + '\\.',
                ]
        paths += [
            str(existing).lower() + '\\~missing\\child',
            str(root).lower() + '\\~missing\\child',
        ]
    paths = list(dict.fromkeys(paths))
    rows = ['-' if x is None else '.' if x == '' else x.encode().hex() for x in paths]
    differences = []
    observations = []
    exception_types = collections.Counter()
    metadata_cases = 0
    metadata_outcomes = []
    for cwd in [ROOT, existing]:
        outputs = [invoke(managed, rows, str(cwd)), invoke([native, '--probe'], rows, str(cwd))]
        assert len(outputs[0]) == len(outputs[1]) == len(rows)
        differences += [
            dict(cwd=str(cwd), path=p, source=s, native=n)
            for p, s, n in zip(paths, *outputs)
            if s != n
        ]
        exception_types.update(
            line.split(':', 2)[1] for line in outputs[0] if line.startswith('EX:')
        )
        if os.name == 'nt':
            meta = [
                invoke(managed + ['--metadata-probe'], rows, str(cwd)),
                invoke([native, '--metadata-probe'], rows, str(cwd)),
            ]
            assert len(meta[0]) == len(meta[1]) == len(rows)
            meta_diffs = [
                dict(
                    pathLength=len(path) if path is not None else None,
                    sourcePrefix=source[:120],
                    sourceMetadata=source[-40:],
                    nativePrefix=result[:120],
                    nativeMetadata=result[-40:],
                )
                for path, source, result in zip(paths, *meta)
                if source != result
            ]
            assert not meta_diffs, json.dumps(meta_diffs[:3])
            metadata_cases += len(rows)
            for path, line in zip(paths, meta[0]):
                if line.startswith('EX:'):
                    fields = line.split(':')
                    metadata_outcomes.append(
                        dict(
                            type=fields[1],
                            parameter=fields[2],
                            hresult=fields[-2],
                            win32=fields[-1],
                            pathLength=len(path) if path is not None else None,
                        )
                    )
        observations.append(
            dict(cwd=str(cwd), cases=len(rows), null=outputs[0][0], empty=outputs[0][1])
        )
    unix_paths = paths + [
        prefix + left + '/' + right + tail
        for prefix, left, right, tail in itertools.product(
            ['', '/', '//', '/../../'],
            ['a', '..', '.', '', 'é', '😀', '..x'],
            ['a', '..', '.', '', 'é', '😀', '..x'],
            ['', '/', '/.', '/..'],
        )
    ]
    unix_paths = list(dict.fromkeys(unix_paths))
    cwds = [
        b'/',
        b'/tmp/base',
        '/é/漢😀'.encode(),
        b'//tmp///base/',
        b'/tmp/bad\xff',
        b'/tmp/c0\xc0\xaf',
        b'/tmp/incomplete\xf0\x9f',
        b'/tmp/\xed\xa0\x80',
        b'/tmp/suffix\x00ignored',
        b'/tmp/\x80\xbf/',
        b'/tmp/\xf4\x90\x80\x80/',
        b'/tmp/\xe0\x80\x80/',
    ]

    def encode(value):
        return '-' if value is None else '.' if value == b'' else value.hex()

    unix_rows = [
        f'{encode(None if path is None else path.encode())} {encode(cwd)} 0 0'
        for path, cwd in itertools.product(unix_paths, cwds)
    ]
    unix_rows += [
        f'{encode(b"cache")} {encode(b"/tmp/"+bytes([n])+b"/base")} 0 0' for n in range(256)
    ]
    error_rows = [
        f'{encode(None if path is None else path.encode())} {encode(b"/tmp/base")} {error} {fault}'
        for path, error, fault in itertools.product(
            [None, '', 'a\x00b', 'cache', '.', '..', '/abs', '/../', ' ', '\\'], range(12), range(2)
        )
    ]
    unix_rows += error_rows
    unix_outputs = [
        invoke(managed + ['--unix-probe'], unix_rows),
        invoke([native, '--unix-probe'], unix_rows),
    ]
    assert len(unix_outputs[0]) == len(unix_outputs[1]) == len(unix_rows)
    unix_differences = [
        dict(command=c, source=s, native=n) for c, s, n in zip(unix_rows, *unix_outputs) if s != n
    ]
    sources = {
        name: dict(sha256=sha, url=url)
        for name, sha, url in re.findall(
            r'^// (\S+) ([0-9a-f]{64}) (https://\S+)$', source.read_text(encoding='utf-8'), re.M
        )
    }
    (w / 'full-path-unix-simulation-results.json').write_text(
        json.dumps(
            dict(
                passed=not unix_differences,
                cases=len(unix_rows),
                errorAndFaultCases=len(error_rows),
                runtimePlatform=os.name,
                unixRuntimeExecuted=os.name != 'nt',
                sources=sources,
                scope='Pinned dotnet/runtime v8.0.31 Path.Unix GetFullPath/GetFullPathInternal/IsPathRooted, PathInternal RemoveRelativeSegments and Unix errno mapping methods retained unchanged. Controlled cwd bytes are decoded by actual Marshal.PtrToStringUTF8; English error resources are pinned. Native portable Unix core compares exact paths, exception classification/parameter/message and cwd call count, including malformed bytes, errno categories, provider faults and absolute/validation bypass. These portable Unix provider simulations record the host separately and do not themselves execute POSIX cwd syscalls; actual Linux/macOS cwd/syscall execution, arbitrary Unix HResult/inner exceptions, allocation failures and all localized resources remain unverified.',
                differences=unix_differences[:6],
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    assert not unix_differences, json.dumps(unix_differences[:3])
    runtime = invoke(managed + ['--runtime'])
    core = Path(runtime[0])
    report = dict(
        passed=not differences,
        cases=len(rows) * 2,
        platform=os.name,
        exceptionMessageCases=sum(exception_types.values()),
        exceptionTypeCounts=dict(exception_types),
        win32MetadataCases=metadata_cases,
        win32MetadataErrorOutcomes=metadata_outcomes,
        simulatedUnixCases=len(unix_rows),
        simulatedUnixErrorAndFaultCases=len(error_rows),
        simulatedUnixSourceHashes=sources,
        shortAliases=aliases,
        shortExpansionExercised=any('~' in p['short'] and p['short'] != p['long'] for p in aliases),
        observations=observations,
        referenceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        runtime=dict(
            version=runtime[1],
            assembly=str(core),
            sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
        ),
        scope='Actual .NET Path.GetFullPath and native OS providers compare exact returned UTF8 paths and exception type/parameter/messages plus Windows HRESULT/native-code metadata for covered deterministic validation and length failures, with two actual cwd roots, null/empty/control/Unicode whitespace and Unicode paths, relative/root/drive/UNC/device forms, separators/dot/trailing-space rules, limits and actual generated short-name aliases/existing-prefix expansion. Only generated fixture directories/files are created. Native unit separately tests provider validation/order/fault propagation. Portable Unix algorithm/provider comparisons are recorded separately and are not Linux/macOS syscall execution. Windows execution does not verify Unix branches; arbitrary concurrent filesystem/cwd races, malformed UTF16 and complete localized OS exception messages remain unverified.',
        differences=differences[:6],
    )
    (w / 'full-path-results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert not differences, json.dumps(
        [
            dict(
                cwd=d['cwd'],
                pathLength=len(d['path']) if d['path'] is not None else None,
                pathPrefix=d['path'][:80] if d['path'] is not None else None,
                source=d['source'][:160],
                native=d['native'][:160],
            )
            for d in differences[:3]
        ]
    )
    print(
        f'PASS: {len(rows)*2} actual .NET/native full path cases + {metadata_cases} Windows metadata cases + {len(unix_rows)} portable Unix provider cases; short expansion exercised={report["shortExpansionExercised"]}'
    )


if __name__ == '__main__':
    main()
