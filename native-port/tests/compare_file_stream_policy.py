import argparse, hashlib, itertools, json, os, shutil, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


def call(command, rows=None, env=None):
    r = subprocess.run(
        command,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=240,
        env=env,
    )
    assert r.returncode == 0, (command, r.returncode, r.stdout[-2000:], r.stderr[-2000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
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
        group, 'Compile', Include=str(ROOT / 'native-port/tests/file_stream_policy_reference.cs')
    )
    cs = w / 'FileStreamPolicyReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    reference = [a.dotnet, str(w / 'app/FileStreamPolicyReference.dll')]
    native = [a.native]
    runtime = call(reference + ['--runtime'])[0].split('|')
    assert runtime[0] == '8.0.31', runtime
    values = [
        None,
        '',
        '1',
        '0',
        'true',
        'TRUE',
        'tRuE',
        'false',
        ' true',
        'true ',
        '\ttrue',
        'true\n',
        'true\0tail',
        '\0true',
        '1\0tail',
        '\01',
    ]
    values += [chr(c) + 'True' for c in range(65536)] + ['True' + chr(c) for c in range(65536)]
    values += ['True'[:i] + chr(c) + 'True'[i + 1 :] for i in range(4) for c in range(65536)]
    values += [''.join(chars) for chars in itertools.product('tT', 'rR', 'uU', 'eE')]
    rows = [enc(v) for v in values]
    expected = call(reference + ['--bool'], rows)
    actual = call(native + ['--bool'], rows)
    assert len(expected) == len(actual) == len(rows)
    failures = [
        dict(index=i, input=rows[i], source=s, native=n)
        for i, (s, n) in enumerate(zip(expected, actual))
        if s != n
    ]
    report = dict(
        runtime=runtime,
        corelib_sha256=hashlib.sha256(Path(runtime[1]).read_bytes()).hexdigest(),
        native_sha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
        boolean_cases=len(rows),
        boolean_failures=failures[:10],
    )
    (w / 'file-stream-policy-results.json').write_text(
        json.dumps(report, indent=2), encoding='utf-8'
    )
    assert not failures, failures[:2]
    # Each scenario gets a new process. The reference reads argv, and performs
    # no Console/file I/O before its explicitly requested first handle operation.
    existing = w / 'existing.bin'
    existing.write_bytes(b'owned fixture')
    missing = w / 'missing-parent' / 'missing.bin'
    directory = w / 'directory'
    directory.mkdir(exist_ok=True)
    flag = 'DOTNET_SYSTEM_IO_DISABLEFILELOCKING'
    op = lambda name, *args: ':'.join([name] + [enc(x) for x in args])
    setflag = lambda value: op('env', flag, value)
    scenarios = []
    for initial, later in itertools.product(
        ['false', 'true', ' true', 'true\0tail'], ['false', 'true']
    ):
        for before in [
            '',
            'factory',
            'context',
            'open:' + enc(None) + ':3:1',
            'open:' + enc('') + ':3:1',
            'open:' + enc('x\0y') + ':3:1',
            'open:' + enc(str(missing)) + ':3:1',
            'open:' + enc(str(missing)) + ':2:0',
            'open:' + enc(str(directory)) + ':3:1',
            'open:' + enc(str(existing)) + ':3:1',
            'open:' + enc(str(existing)) + ':2:0',
        ]:
            scenarios.append(
                ','.join(x for x in [setflag(initial), before, setflag(later), 'p'] if x)
            )
    for before, after, value in itertools.product(['0', '1'], ['0', '1'], ['false', 'true']):
        scenarios.append(
            ','.join(['sw:' + before, setflag(value), 'p', 'sw:' + after, setflag('true'), 'p'])
        )
    for value in ['true', 'false', ' true', 'true\0tail']:
        scenarios.append(','.join([setflag(value), 'factory', 'p', setflag('false'), 'p']))
    if os.name != 'nt':
        zone = w / 'owned-zone'
        shutil.copyfile('/usr/share/zoneinfo/America/New_York', zone)
        utc = w / 'owned-utc'
        shutil.copyfile('/usr/share/zoneinfo/Etc/UTC', utc)
        jan = 638409600000000000
        jul = 638566848000000000
        seconds = 1719792000
        for first, second, kind in itertools.product(
            [str(zone), str(utc), '', str(missing)],
            [str(zone), str(utc), '', str(missing)],
            ['env', 'raw'],
        ):
            scenarios.append(
                ','.join(
                    [
                        op('env', 'TZ', first),
                        'clear',
                        'offset:' + str(jan),
                        op(kind, 'TZ', second),
                        'offset:' + str(jul),
                        'clear',
                        'offset:' + str(jul),
                        'dst:' + str(seconds),
                    ]
                )
            )
        for first, second in itertools.product(['false', 'true'], ['false', 'true']):
            for consumer in [
                'offset:' + str(jan),
                'dst:' + str(seconds),
                'find:' + enc('America/New_York') + ':' + str(seconds),
            ]:
                scenarios.append(
                    ','.join(
                        [
                            setflag(first),
                            op('env', 'TZ', str(zone)),
                            'clear',
                            consumer,
                            setflag(second),
                            'factory',
                            'p',
                        ]
                    )
                )
                scenarios.append(
                    ','.join(
                        [
                            setflag(first),
                            'open:' + enc(str(existing)) + ':3:1',
                            setflag(second),
                            op('env', 'TZ', str(zone)),
                            'clear',
                            consumer,
                            'p',
                        ]
                    )
                )
        for value in ['', str(missing)]:
            scenarios.append(
                ','.join(
                    [
                        setflag('false'),
                        op('env', 'TZ', value),
                        'clear',
                        'offset:' + str(jan),
                        setflag('true'),
                        'p',
                    ]
                )
            )
    override_cases = 0
    for value in ['0', '1']:
        call(native + ['--override', enc(str(existing)), 'nini', value])
        override_cases += 1
        if os.name != 'nt':
            call(native + ['--override', enc(str(zone)), 'zone', value])
            override_cases += 1
    failures = []
    observed = []
    for i, row in enumerate(scenarios):
        s = call(reference + [row])[0]
        n = call(native + [row])[0]
        observed.append(dict(row=row, source=s, native=n))
        if s != n:
            failures.append(dict(index=i, row=row, source=s, native=n))
    lock_observations = []
    if os.name != 'nt':
        import fcntl

        with zone.open('rb') as held:
            fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            for initial, later, first in itertools.product(
                ['false', 'true'],
                ['false', 'true'],
                ['offset:' + str(jul), 'dst:' + str(seconds), 'open:' + enc(str(zone)) + ':3:1'],
            ):
                row = ','.join(
                    [
                        setflag(initial),
                        op('env', 'TZ', str(zone)),
                        'clear',
                        first,
                        setflag(later),
                        'clear',
                        'offset:' + str(jul),
                        'dst:' + str(seconds),
                        'open:' + enc(str(zone)) + ':3:1',
                        'p',
                    ]
                )
                source = call(reference + [row])[0]
                actual = call(native + [row])[0]
                lock_observations.append(dict(row=row, source=source, native=actual))
                if source != actual:
                    failures.append(
                        dict(kind='owned external file lock', row=row, source=source, native=actual)
                    )
            fcntl.flock(held, fcntl.LOCK_UN)
    special = []
    if os.name != 'nt':
        lexical = w / 'lexical'
        physical = w / 'physical'
        lexical.mkdir(exist_ok=True)
        (physical / 'child').mkdir(parents=True, exist_ok=True)
        shutil.copyfile(zone, lexical / 'zone')
        shutil.copyfile(utc, physical / 'zone')
        link = lexical / 'link'
        if not link.exists():
            link.symlink_to(physical / 'child', target_is_directory=True)
        row = ','.join(
            [
                op('env', 'TZ', str(link / '..' / 'zone')),
                'clear',
                'offset:' + str(jan),
                'dst:' + str(seconds),
            ]
        )
        source = call(reference + [row])[0]
        actual = call(native + [row])[0]
        special.append(
            dict(
                kind='lexical full path before symlink filesystem lookup',
                source=source,
                native=actual,
            )
        )
        if source != actual:
            failures.append(
                dict(
                    kind='lexical full path before symlink filesystem lookup',
                    row=row,
                    source=source,
                    native=actual,
                )
            )
    if os.name != 'nt':
        env = dict(os.environ)
        env['TZDIR'] = ''
        env['TZ'] = str(zone).lstrip('/')
        row = 'clear,offset:' + str(jan) + ',dst:' + str(seconds)
        s = call(reference + [row], env=env)[0]
        n = call(native + [row], env=env)[0]
        special.append(dict(kind='empty TZDIR means root', source=s, native=n))
        assert s == n, (s, n)
        for consumer in ['offset:' + str(jan), 'dst:' + str(seconds)]:
            env = dict(os.environ)
            env['TZ'] = ''
            env[flag] = 'false'
            row = ','.join(['context', consumer, setflag('true'), 'p'])
            s = call(reference + [row], env=env)[0]
            n = call(native + [row], env=env)[0]
            special.append(
                dict(kind='startup empty TZ does not initialize file policy', source=s, native=n)
            )
            assert s == n and s.endswith('P:1'), (s, n)
    report.update(
        native_override_cases=override_cases,
        lock_cases=len(lock_observations),
        lock_observations=lock_observations,
        scenario_cases=len(scenarios),
        scenario_failures=failures[:20],
        observations=observed,
        special=special,
    )
    (w / 'file-stream-policy-results.json').write_text(
        json.dumps(report, indent=2), encoding='utf-8'
    )
    assert not failures, failures[:2]
    print(
        json.dumps(
            dict(
                boolean_cases=len(rows),
                scenario_cases=len(scenarios),
                lock_cases=len(lock_observations),
                special_cases=len(special),
                failures=0,
            )
        )
    )


if __name__ == '__main__':
    main()
