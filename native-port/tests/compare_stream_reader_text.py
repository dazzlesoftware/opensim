"""Actual BCL StreamReader decoding/lines and bundled Nini configuration files."""

import argparse, ctypes, hashlib, itertools, json, os, random, sqlite3, subprocess, tempfile, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, rows=None, timeout=110):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=timeout,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-1500:], r.stderr[-1500:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--native-app')
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    native = str(Path(a.native).resolve())
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    reference = ROOT / 'native-port/tests/stream_reader_text_reference.cs'
    ET.SubElement(group, 'Compile', Include=str(reference))
    ref = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'StreamReaderTextReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/StreamReaderTextReference.dll')]
    prefixes = [
        b'',
        b'\xef\xbb\xbf',
        b'\xff\xfe',
        b'\xfe\xff',
        b'\xff\xfe\x00\x00',
        b'\x00\x00\xfe\xff',
    ]
    encodings = ['utf-8', 'utf-8', 'utf-16-le', 'utf-16-be', 'utf-32-le', 'utf-32-be']
    cases = [bytes([n]) for n in range(256)] + [
        bytes(pair) for pair in itertools.product(range(256), repeat=2)
    ]
    for prefix in prefixes:
        cases += [prefix[:n] for n in range(len(prefix) + 1)]
    for first, second, tail in itertools.product(
        prefixes, prefixes, [b'', b'a', b'\x00', b'\x00\x00\x00']
    ):
        cases.append(first + second + tail)
    units = [
        0,
        10,
        13,
        0x85,
        0x2028,
        0x2029,
        0xD7FF,
        0xD800,
        0xDBFF,
        0xDC00,
        0xDFFF,
        0xE000,
        0xFEFF,
        0xFFFF,
    ]
    for little, prefix in [(True, prefixes[2]), (False, prefixes[3])]:
        for first, second, tail in itertools.product(units, units, [b'', b'\x00', b'\xff']):
            cases.append(
                prefix
                + first.to_bytes(2, 'little' if little else 'big')
                + second.to_bytes(2, 'little' if little else 'big')
                + tail
            )
    points = [
        0,
        10,
        13,
        0x85,
        0x2028,
        0xD7FF,
        0xD800,
        0xDBFF,
        0xDC00,
        0xDFFF,
        0xE000,
        0xFEFF,
        0xFFFF,
        0x10000,
        0x10FFFF,
        0x110000,
        0x7FFFFFFF,
        0xFFFFFFFF,
    ]
    for little, prefix in [(True, prefixes[4]), (False, prefixes[5])]:
        for value, tail in itertools.product(
            points, [b'', b'\x00', b'\x00\x00', b'\x00\x00\x00', b'\xff', b'\xff\xff\xff']
        ):
            cases.append(prefix + value.to_bytes(4, 'little' if little else 'big') + tail)
    breaks = [
        '',
        'a',
        '\r',
        '\n',
        '\r\n',
        '\n\r',
        '\r\r\n',
        '\x85',
        '\u2028',
        '\u2029',
        '\x1a',
        '\x00',
        '😀',
    ]
    for index, left, right in itertools.product(range(6), breaks, breaks):
        cases.append(prefixes[index] + (left + right).encode(encodings[index]))
    for index, size, ending in itertools.product(
        range(6),
        [255, 256, 511, 512, 1023, 1024, 1025, 4095, 4096, 4097],
        ['é漢😀\r\n', '\r\na', '\r\r\n', '\x1aTAIL'],
    ):
        cases.append(prefixes[index] + ('a' * size + ending).encode(encodings[index]))
    rng = random.Random(791104)
    for _ in range(3500):
        cases.append(rng.choice(prefixes) + rng.randbytes(rng.randrange(80)))
    cases = list(dict.fromkeys(cases))
    rows = [value.hex() if value else '.' for value in cases]
    outputs = [invoke(managed + ['--probe'], rows), invoke([native, '--probe'], rows)]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    differences = [dict(input=c, source=s, native=n) for c, s, n in zip(rows, *outputs) if s != n]
    fixture = Path(tempfile.mkdtemp(prefix='stream-text-', dir=w)) / 'Unicode 配置 é漢😀'
    fixture.mkdir()
    files = []
    config_cases = []
    for index, value, newline in itertools.product(
        range(6), ['数据库-é漢😀', 'before\x1aafter'], ['\n', '\r\n']
    ):
        data = prefixes[index] + ('[Probe]' + newline + 'x = ' + value + newline).encode(
            encodings[index]
        )
        file = fixture / f'配置 {index}-{len(files)} é😀.ini'
        file.write_bytes(data)
        files.append(file)
        config_cases.append(
            dict(file=str(file), encoding=encodings[index], bom=prefixes[index].hex(), value=value)
        )
    file_rows = [str(path).encode().hex() for path in files]
    file_out = [
        invoke(managed + ['--file-probe'], file_rows),
        invoke([native, '--file-probe'], file_rows),
    ]
    assert file_out[0] == file_out[1], [
        (str(p), s, n) for p, s, n in zip(files, *file_out) if s != n
    ][:2]
    config_out = [
        invoke(managed + ['--config-probe'], file_rows),
        invoke([native, '--config-probe'], file_rows),
    ]
    assert config_out[0] == config_out[1], [
        (str(p), s, n) for p, s, n in zip(files, *config_out) if s != n
    ][:2]
    for case, output in zip(config_cases, config_out[0]):
        assert output == 'OK:' + case['value'].encode('utf-16-le').hex()
    # A second BOM is decoded text, not another preamble to strip in IniConfig.
    double_files = []
    for index in range(1, 6):
        path = fixture / f'double-bom-{index}.ini'
        path.write_bytes(
            prefixes[index] + prefixes[index] + '[Probe]\nx=a\n'.encode(encodings[index])
        )
        double_files.append(path)
    double_rows = [str(path).encode().hex() for path in double_files]
    doubled = [
        invoke(managed + ['--config-probe'], double_rows),
        invoke([native, '--config-probe'], double_rows),
    ]
    assert all(s.startswith('ERR:') and n.startswith('ERR:') for s, n in zip(*doubled)), doubled
    shape_texts = [
        ('duplicate-key-first', '[Probe]\nx=first\nx=second\ny=last\n'),
        (
            'duplicate-section-replace',
            '[Probe]\nx=old\ny=discard\n[Other]\nx=other\n[Probe]\nx=new\n',
        ),
        ('empty-section', '[]\nx=empty\n[Probe]\nx=normal\n'),
        ('leading-section-space', '[ Probe]\nx=leading\n[Probe ]\nx=trailing\n'),
        ('quote-garbage', '[Probe]\nx="quoted" garbage\n'),
        ('quote-unclosed', '[Probe]\nx="unterminated'),
        ('quote-doubled', '[Probe]\nx="a""b"\n'),
        ('cr-only', '[Probe]\rx=one\ry=two\r'),
        ('cr-in-value', '[Probe]\nx=one\ry=two\n'),
        ('hash-not-comment', '[Probe]\n#notacomment\nx=ok\n'),
        ('hash-in-value', '[Probe]\nx=before #literal\n'),
        ('missing-assignment', '[Probe]\nx\n'),
        ('key-before-section', 'x=before\n[Probe]\nx=after\n'),
        ('before-section-priority', 'x=before\n[unterminated'),
        ('continuation-off', '[Probe]\nx=one\\\ntail=two\n'),
        ('unicode-whitespace', '[Probe]\n\u2003x\u2003 = \u3000spaced\u2003\n'),
        ('semicolon-comments', '[Probe] ; section\nx=before ; inline\n'),
        ('empty-key', '[Probe]\n=empty key\n'),
        ('empty-section-late-error', '[]\nx=empty\n[unterminated'),
        ('include-overwritten', '[Probe]\nInclude-X=a\n[Probe]\nx=ok\n'),
        ('include-before-syntax-error', '[Probe]\nInclude-X=a\n[Bad'),
        ('comment-after-reserved-key', '[Probe]\n#comment0=value\n; comment\n'),
        ('blank-after-reserved-key', '[Probe]\n#comment0=value\n\n'),
        ('comment-before-reserved-key', '[Probe]\n; comment\n#comment0=value\n'),
        ('blank-before-reserved-key', '[Probe]\n\n#comment0=value\n'),
        ('next-comment-number', '[Probe]\n; one\n#comment1=value\n; two\n'),
        ('inline-comment-not-reserved', '[Probe]\nx=value ; inline\n#comment0=literal\n'),
        ('whitespace-eof-comment-collision', '[Probe]\n#comment0=value\n   '),
    ]
    shape_paths = []
    shape_profiles = []
    for index, (label, text) in itertools.product(range(6), shape_texts):
        path = fixture / f'shape-{index}-{len(shape_paths)}.ini'
        path.write_bytes(prefixes[index] + text.encode(encodings[index]))
        shape_paths.append(path)
        shape_profiles.append(dict(label=label, encoding=encodings[index], text=text))
    shape_rows = [str(path).encode().hex() for path in shape_paths]
    shape_outputs = [
        invoke(managed + ['--config-shape-probe'], shape_rows),
        invoke([native, '--config-shape-probe'], shape_rows),
    ]
    assert len(shape_outputs[0]) == len(shape_outputs[1]) == len(shape_rows)
    shape_differences = [
        dict(profile=p, source=s, native=n)
        for p, s, n in zip(shape_profiles, *shape_outputs)
        if s != n
    ]
    (w / 'nini-config-integration-results.json').write_text(
        json.dumps(
            dict(
                passed=not shape_differences,
                cases=len(shape_rows),
                observations=[
                    dict(profile=p, result=s) for p, s in zip(shape_profiles, shape_outputs[0])
                ],
                differences=shape_differences,
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    assert not shape_differences, json.dumps(shape_differences[:3])
    missing = fixture / '不存在-é漢😀.ini'
    missing_row = [str(missing).encode().hex()]
    missing_file = [
        invoke(managed + ['--file-probe'], missing_row),
        invoke([native, '--file-probe'], missing_row),
    ]
    if os.name == 'nt':
        assert missing_file == [['ERR:2'], ['ERR:2']], missing_file
    missing_config = invoke([native, '--config-probe'], missing_row)[0]
    assert missing_config.startswith('ERR:') and str(missing) in bytes.fromhex(
        missing_config[4:]
    ).decode('utf-8'), missing_config
    locked = False
    if os.name == 'nt':
        kernel = ctypes.windll.kernel32
        kernel.CreateFileW.argtypes = [
            ctypes.c_wchar_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_void_p,
        ]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(files[0]), 0x80000000, 0, None, 3, 0, None)
        assert handle not in (None, ctypes.c_void_p(-1).value)
        try:
            observed = [
                invoke(managed + ['--file-probe'], file_rows[:1]),
                invoke([native, '--file-probe'], file_rows[:1]),
            ]
            assert observed == [['ERR:32'], ['ERR:32']], observed
            locked = True
        finally:
            kernel.CloseHandle(handle)
    long_file_cases = []
    long_source = []
    long_nini_cases = 0
    long_startup_directory = None
    if os.name == 'nt':

        def extended(path):
            text = str(path)
            return '\\\\?\\UNC\\' + text[2:] if text.startswith('\\\\') else '\\\\?\\' + text

        long_directory = fixture / 'long-root' / ('a' * 90) / ('b' * 90) / ('c' * 90)
        os.makedirs(extended(long_directory), exist_ok=True)
        long_config_rows = []
        long_rows = []
        for index in range(6):
            path = long_directory / f'配置 {index} é😀.ini'
            body = prefixes[index] + '[Probe]\nx=long-é漢😀\n'.encode(encodings[index])
            with open(extended(path), 'wb') as file:
                file.write(body)
            forms = [
                str(path),
                str(path).replace('\\', '/'),
                extended(path),
                '\\??\\' + str(path),
                '\\\\.\\' + str(path),
                str(path.parent / 'unused' / '..' / path.name),
            ]
            if (
                os.path.splitdrive(str(path))[0].lower()
                == os.path.splitdrive(str(Path.cwd()))[0].lower()
            ):
                forms.append(os.path.relpath(path, Path.cwd()))
            long_file_cases += [
                dict(path=name, kind='long-existing', encoding=encodings[index]) for name in forms
            ]
            long_config_rows += [name.encode().hex() for name in forms]
        for root in [fixture, long_directory]:
            for name, value in [
                ('literal', 'normalized'),
                ('literal. ', 'literal-trailing'),
                ('dot', 'normalized-dot'),
                ('dot.', 'literal-dot'),
            ]:
                path = root / name
                with open(extended(path), 'wb') as file:
                    file.write(('[Probe]\nx=' + value + '\n').encode())
            for name in ['literal. ', 'dot.']:
                path = root / name
                forms = [str(path), extended(path), '\\??\\' + str(path), '\\\\.\\' + str(path)]
                long_file_cases += [
                    dict(path=value, kind='trailing-dot-space', encoding='utf-8') for value in forms
                ]
                long_config_rows += [value.encode().hex() for value in forms]
        long_file_cases += [
            dict(path=str(long_directory / 'missing-é😀.ini'), kind='missing-long'),
            dict(path=str(long_directory / 'none' / 'missing.ini'), kind='missing-long-parent'),
            dict(path='x' * 32750, kind='normalize-io'),
            dict(path='C:\\' + 'x' * 40000, kind='normalize-length'),
            dict(path='', kind='empty'),
            dict(path=str(long_directory) + '\x00tail', kind='nul'),
            dict(path=str(Path.cwd().anchor), kind='drive-root'),
            dict(path='\\??\\' + str(Path.cwd().anchor), kind='nt-drive-root'),
            dict(
                path='\\\\?\\Volume{00000000-0000-0000-0000-000000000000}',
                kind='device-volume-root',
            ),
            dict(path='NUL', kind='nul-device'),
        ]
        long_rows = [
            case['path'].encode().hex() if case['path'] else '.' for case in long_file_cases
        ]
        long_out = [
            invoke(managed + ['--file-probe'], long_rows),
            invoke([native, '--file-probe'], long_rows),
        ]
        assert len(long_out[0]) == len(long_out[1]) == len(long_rows)
        long_differences = [
            dict(case=case, source=source, native=result)
            for case, source, result in zip(long_file_cases, *long_out)
            if source != result
        ]
        long_source = long_out[0]
        assert all(
            not observed.startswith('ERR:')
            for case, observed in zip(long_file_cases, long_source)
            if case['kind'] in ('long-existing', 'trailing-dot-space')
        ), (
            "existing long path unexpectedly failed",
            [
                (case['kind'], case['path'])
                for case, observed in zip(long_file_cases, long_source)
                if case['kind'] in ('long-existing', 'trailing-dot-space')
                and observed.startswith('ERR:')
            ],
        )
        (w / 'stream-reader-long-path-results.json').write_text(
            json.dumps(
                dict(
                    passed=not long_differences,
                    cases=len(long_rows),
                    observations=[
                        dict(case=case, result=source)
                        for case, source in zip(long_file_cases, long_source)
                    ],
                    differences=long_differences,
                ),
                indent=2,
            )
            + '\n',
            encoding='utf-8',
        )
        assert not long_differences, json.dumps(
            [
                dict(
                    kind=d['case']['kind'],
                    pathLength=len(d['case']['path']),
                    source=d['source'][:120],
                    native=d['native'][:120],
                )
                for d in long_differences[:3]
            ]
        )
        config_long = [
            invoke(managed + ['--config-probe'], long_config_rows),
            invoke([native, '--config-probe'], long_config_rows),
        ]
        assert config_long[0] == config_long[1], [
            (source, result) for source, result in zip(*config_long) if source != result
        ][:2]
        long_nini_cases = len(long_config_rows)
        long_startup_directory = long_directory
    applications = []
    if a.native_app:
        app = str(Path(a.native_app).resolve())
        for index in range(6):
            directory = fixture / f'startup-{index}'
            directory.mkdir()
            file = directory / '配置 é😀.ini'
            body = '[NativeServer]\nUserAccountDatabase = users-é漢😀.db\nAuthenticationDatabase = auth-é漢😀.db\nAuthenticationProvider = SQLite\n'
            file.write_bytes(prefixes[index] + body.encode(encodings[index]))
            process = subprocess.run(
                [app, '--config', str(file), '--init-db'], capture_output=True, timeout=30
            )
            assert process.returncode == 0, (index, process.stdout, process.stderr)
            for name, table in [('users-é漢😀.db', 'UserAccounts'), ('auth-é漢😀.db', 'auth')]:
                with sqlite3.connect(directory / name) as db:
                    assert (
                        db.execute(
                            'SELECT count(*) FROM sqlite_master WHERE type=? AND name=?',
                            ('table', table),
                        ).fetchone()[0]
                        == 1
                    )
            applications.append(
                dict(
                    encoding=encodings[index],
                    bom=prefixes[index].hex(),
                    returncode=process.returncode,
                    config=str(file),
                )
            )
        if long_startup_directory:
            for index in range(6):
                directory = fixture / f'long-startup-db-{index}'
                directory.mkdir()
                file = long_startup_directory / f'startup-配置 {index} é😀.ini'
                body = (
                    '[NativeServer]\nUserAccountDatabase = '
                    + str(directory / 'users-é漢😀.db')
                    + '\nAuthenticationDatabase = '
                    + str(directory / 'auth-é漢😀.db')
                    + '\nAuthenticationProvider = SQLite\n'
                )
                with open(extended(file), 'wb') as handle:
                    handle.write(prefixes[index] + body.encode(encodings[index]))
                process = subprocess.run(
                    [app, '--config', str(file), '--init-db'], capture_output=True, timeout=30
                )
                assert process.returncode == 0, (index, process.stdout, process.stderr)
                for name, table in [('users-é漢😀.db', 'UserAccounts'), ('auth-é漢😀.db', 'auth')]:
                    with sqlite3.connect(directory / name) as db:
                        assert (
                            db.execute(
                                'SELECT count(*) FROM sqlite_master WHERE type=? AND name=?',
                                ('table', table),
                            ).fetchone()[0]
                            == 1
                        )
                applications.append(
                    dict(
                        kind='long-config-short-db-targets',
                        encoding=encodings[index],
                        bom=prefixes[index].hex(),
                        returncode=process.returncode,
                        config=str(file),
                    )
                )
        process = subprocess.run(
            [app, '--config', str(missing), '--init-db'], capture_output=True, timeout=30
        )
        assert (
            process.returncode == 1
            and b'[CONFIG]: Could not load any configuration' in process.stderr
            and b'LISTENING ' not in process.stdout
        ), (process.returncode, process.stderr)
        applications.append(
            dict(
                mode='missing-main-config-original-fallback',
                returncode=process.returncode,
                stderrHex=process.stderr.hex(),
            )
        )
    report = dict(
        passed=not differences,
        cases=len(rows),
        actualFileCases=len(files),
        actualNiniConfigurations=config_cases,
        doubleBomRejected=len(double_files),
        niniIntegrationCases=len(shape_rows),
        longFileCases=len(long_file_cases),
        longNiniCases=long_nini_cases,
        missingUnicodeConfigPreserved=True,
        lockedFileSharingCompared=locked,
        actualNativeStartup=applications,
        platform=os.name,
        sourceHashes={
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [reference, ROOT / 'bin/Nini.dll']
        },
        scope='Actual default BCL StreamReader ReadToEnd/CurrentEncoding/ReadLine with complete MemoryStream input, malformed bytes and UTF8/16/32 preambles, surrogate/scalar validation, line delimiters, read-buffer boundaries and literal CtrlZ/NUL. Actual generated Unicode files and bundled Nini getters compare supported ordinary INI values across all encodings, double-BOM rejection, missing Unicode filename and Windows file sharing. Actual Windows long-file/config cases cover normalized absolute/relative/forward/device forms, literal trailing dot/space names, namespace roots and error codes. Optional native executable startup verifies all six encoding profiles in ordinary and long config paths and the original missing-main-file/OpenSim.xml fallback failure; raw loading separately preserves missing Unicode filename diagnostics; long-path startup uses explicit short database targets to isolate the reader contract. Additional actual Nini/native INI integration covers duplicate-key first-wins, duplicate-section replacement, leading/empty sections, quote failures, CR-only text, hash semantics, bare keys, before-section NRE priority, disabled continuations and Unicode whitespace, including exact parser/NRE messages. Full Nini syntax, arbitrary stream chunking/concurrent I/O/error ordering, all CLR/encoding APIs and Unix execution remain unverified.',
        differences=differences[:6],
    )
    (w / 'stream-reader-text-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:3])
    print(
        f'PASS: {len(rows)} actual StreamReader cases, {len(files)} Nini encoding configurations + {len(shape_rows)} Nini integration cases, {len(long_file_cases)} long file cases, {len(applications)} native startup cases'
    )


if __name__ == '__main__':
    main()
