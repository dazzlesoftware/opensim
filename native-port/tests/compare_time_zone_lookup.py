"""Consumed named TimeZoneInfo lookup: actual pinned BCL metadata/ID identity, not full TimeZoneInfo UI or serialization."""

import argparse, datetime, hashlib, itertools, json, os, shutil, struct, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent


def enc(value):
    if value is None:
        return '-'
    data = value.encode('utf-16-le', 'surrogatepass')
    return (
        ''.join(
            format(int.from_bytes(data[i : i + 2], 'little'), '04x') for i in range(0, len(data), 2)
        )
        or '.'
    )


def execute(command, rows, env):
    result = subprocess.run(
        command,
        input='\n'.join(rows) + '\n',
        env=env,
        text=True,
        encoding='utf-8',
        capture_output=True,
        timeout=300,
    )
    assert result.returncode == 0, (command, result.returncode, result.stderr[-2000:])
    return result.stdout.splitlines()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', type=Path, required=True)
    a = parser.parse_args()
    a.native = str(Path(a.native).resolve())
    a.work = a.work.resolve()
    a.work.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in [
        ('OutputType', 'Exe'),
        ('TargetFramework', 'net8.0'),
        ('EnableDefaultCompileItems', 'false'),
    ]:
        ET.SubElement(props, key).text = value
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(HERE / 'time_zone_lookup_reference.cs'),
    )
    projectfile = a.work / 'Reference.csproj'
    ET.ElementTree(project).write(projectfile, encoding='unicode')
    subprocess.run(
        [a.dotnet, 'build', str(projectfile), '-o', str(a.work / 'reference')],
        check=True,
        capture_output=True,
    )
    reference = [a.dotnet, str(a.work / 'reference/Reference.dll')]
    base = os.environ.copy()
    for name in [
        'TZDIR',
        'DOTNET_SYSTEM_GLOBALIZATION_APPLOCALICU',
        'DOTNET_ICU_VERSION_OVERRIDE',
        'CLR_ICU_VERSION_OVERRIDE',
    ]:
        base.pop(name, None)
    base.update(
        DOTNET_SYSTEM_GLOBALIZATION_INVARIANT='0',
        DOTNET_SYSTEM_GLOBALIZATION_USENLS='0',
        DOTNET_SYSTEM_IO_DISABLEFILELOCKING='0',
    )
    groups = []

    def batch(label, rows, extra=None):
        env = base.copy()
        env.update(extra or {})
        metadata = json.loads(
            subprocess.run(
                reference + ['--runtime'], env=env, text=True, capture_output=True, check=True
            ).stdout
        )
        assert metadata['runtime'] == '8.0.31', metadata
        flags = ['--invariant'] if metadata['invariant'] else ['--nls'] if metadata['nls'] else []
        source = execute(reference, rows, env)
        native = execute([a.native, '--probe'] + flags, rows, env)
        assert len(source) == len(native) == len(rows), (label, len(source), len(native), len(rows))
        diff = [
            dict(index=i, row=r, source=s, native=n)
            for i, (r, s, n) in enumerate(zip(rows, source, native))
            if s != n
        ]
        for kind, data in [('input', rows), ('source', source), ('native', native)]:
            (a.work / (label + '-' + kind + '.txt')).write_text(
                '\n'.join(data) + '\n', encoding='utf-8'
            )
        group = dict(
            label=label,
            cases=len(rows),
            metadata=metadata,
            failures=len(diff),
            differences=diff[:20],
        )
        groups.append(group)
        assert not diff, json.dumps(group, indent=2)

    ids = [
        None,
        '',
        'UTC',
        'utc',
        'America/New_York',
        'Eastern Standard Time',
        'bad',
        'a\0b',
        'x' * 256,
        'x' * 255,
        '\ud800',
        '\ud801',
        '\udc00',
        '\ud800\udc00',
        '\ud800\ud800',
        '\ud800\udc01',
        '\ufffd',
        '/bad',
        '..',
        'x.y',
        'x y',
        '\\',
        '\\' * 255,
        'UTC\\',
        'Eastern Standard Time\\',
        'Valid/',
        'Valid//',
        'Missing/Child',
    ] + [chr(i) for i in range(65536)]
    batch('default-raw', [enc(i) for i in ids])
    subprocess.run([a.native], check=True, capture_output=True)
    if sys.platform == 'win32':
        subprocess.run(
            [
                sys.executable,
                str(HERE / 'time_zone_lookup_registry_fixture.py'),
                '--native',
                a.native,
                '--dotnet',
                a.dotnet,
                '--reference',
                reference[1],
                '--work',
                str(a.work / 'registry'),
            ],
            check=True,
        )
        registry = json.loads(
            (a.work / 'registry/time-zone-lookup-registry-results.json').read_text(encoding='utf-8')
        )
        groups.append(
            dict(
                label='owned-registry',
                cases=sum(len(x['observations']) for x in registry['profiles']),
                failures=0,
            )
        )
    else:
        import fcntl

        owned = a.work / 'owned'
        marker = owned / '.time-zone-lookup-owned'
        assert not owned.exists() or marker.is_file(), 'Refuse to overwrite a non-fixture directory'
        owned.mkdir(exist_ok=True)
        marker.write_text('opensim time zone lookup owned fixture', encoding='utf-8')
        for name, data in [
            ('Empty', b''),
            ('Malformed', b'xxx'),
            ('Header', b'TZif' + bytes(40)),
            ('Negative', b'garbage' * 20),
        ]:
            (owned / name).write_bytes(data)
        (owned / 'Directory').mkdir(exist_ok=True)
        shutil.copyfile('/usr/share/zoneinfo/America/New_York', owned / 'Valid')
        shutil.copyfile('/usr/share/zoneinfo/Etc/UTC', owned / 'UtcFixture')
        ownenv = {'TZDIR': str(owned)}
        batch(
            'owned-raw',
            [
                enc(i)
                for i in ids
                + [
                    'Empty',
                    'Malformed',
                    'Header',
                    'Negative',
                    'Directory',
                    'Valid',
                    'valid',
                    'Missing',
                    'Missing/Child',
                ]
            ],
            ownenv,
        )
        generated = []
        for i, (version, types, offset, dst, abbr, chars) in enumerate(
            itertools.product(
                [0, ord('1'), ord('2'), ord('3'), ord('4'), ord('9')],
                [0, 1, 2],
                [-2147483648, -86400, -50401, -50400, -1, 0, 1, 50400, 50401, 86400, 2147483647],
                [0, 1, 2],
                [0, 1],
                [0, 1, 2],
            )
        ):
            name = 'F' + str(i)
            data = (
                b'TZif'
                + bytes([version])
                + bytes(15)
                + struct.pack('>6I', 0, 0, 0, 0, types, chars)
                + b''.join(struct.pack('>iBB', offset, dst, abbr) for _ in range(types))
                + b'X' * chars
            )
            (owned / name).write_bytes(data)
            generated.append(name)
        for i, (times, types, index, dst, offset) in enumerate(
            itertools.product(
                [[], [0], [100, 0], [0, 100]],
                [0, 1, 2],
                [0, 1, 255],
                [0, 1],
                [-50401, 0, 3600, 50401],
            )
        ):
            name = 'T' + str(i)
            data = (
                b'TZif'
                + bytes(16)
                + struct.pack('>6I', 0, 0, 0, len(times), types, 2)
                + b''.join(struct.pack('>i', v) for v in times)
                + bytes([index] * len(times))
                + b''.join(struct.pack('>iBB', offset, dst, 0) for _ in range(types))
                + b'A\0'
            )
            (owned / name).write_bytes(data)
            generated.append(name)

        def block(version, width):
            return (
                b'TZif'
                + bytes([version])
                + bytes(15)
                + struct.pack('>6I', 0, 0, 0, 2, 2, 8)
                + b''.join(struct.pack('>i' if width == 4 else '>q', v) for v in [0, 946684800])
                + bytes([0, 1])
                + struct.pack('>iBB', 0, 0, 0)
                + struct.pack('>iBB', 3600, 1, 4)
                + b'STD\0DST\0'
            )

        footers = [
            '',
            'EST5EDT,M3.2.0/2,M11.1.0/2',
            'IST-1GMT0,M10.5.0,M3.5.0/1',
            '<+1030>-10:30<+11>-11,M10.1.0,M4.1.0',
            'AAA0BBB,M3.2.0/-167:59:59,M11.1.0/167:59:59',
            'garbage',
            'AAA0BBB,J0,J365',
            'AAA0BBB,J1,J366',
            'AAA0BBB,59,300',
            'AAA0BBB,M0.1.0,M12.1.0',
            'AAA0BBB,M3.2.0,M11.1.0',
        ]
        for i, (version, footer) in enumerate(itertools.product([ord('2'), ord('3')], footers)):
            name = 'P' + str(i)
            (owned / name).write_bytes(
                block(version, 4) + block(version, 8) + b'\n' + footer.encode() + b'\n'
            )
            generated.append(name)
        for name, field in [('OverflowTime', 3), ('OverflowType', 4)]:
            counts = [0] * 6
            counts[field] = 0xFFFFFFFF
            (owned / name).write_bytes(b'TZif' + bytes(16) + struct.pack('>6I', *counts))
            generated.append(name)
        for name, real in [
            ('NY', 'America/New_York'),
            ('Dublin', 'Europe/Dublin'),
            ('LordHowe', 'Australia/Lord_Howe'),
            ('Casablanca', 'Africa/Casablanca'),
        ]:
            shutil.copyfile('/usr/share/zoneinfo/' + real, owned / name)
            generated.append(name)
        batch('tzif', [enc(i) for i in generated], ownenv)
        for label, inv, nls in [('invariant', '1', '0'), ('nls-request', '0', '1')]:
            batch(
                label,
                [enc(i) for i in ids[:28] + ['Empty', 'Malformed', 'Header', 'Directory', 'Valid']],
                dict(
                    ownenv,
                    DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=inv,
                    DOTNET_SYSTEM_GLOBALIZATION_USENLS=nls,
                ),
            )
        # Owned OS faults: no global permissions, locks, environment stores or shared fixtures.
        shutil.copyfile(owned / 'Valid', owned / 'Locked')
        shutil.copyfile(owned / 'Valid', owned / 'Denied')
        os.chmod(owned / 'Denied', 0)
        with open(owned / 'Huge', 'wb') as file:
            file.truncate(21 * 1024 * 1024)
        pipe = owned / 'Pipe'
        if pipe.exists():
            pipe.unlink()
        os.mkfifo(pipe)
        fd = os.open(pipe, os.O_RDWR | os.O_NONBLOCK)
        held = open(owned / 'Locked', 'rb')
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            batch('os-errors', [enc(i) for i in ['Locked', 'Denied', 'Huge', 'Pipe']], ownenv)
        finally:
            held.close()
            os.close(fd)
            os.chmod(owned / 'Denied', 0o600)
        # Alias loading must also cache the underlying IANA ID, including Id string identity.
        alias = owned / 'alias'
        (alias / 'America').mkdir(parents=True, exist_ok=True)
        target = alias / 'America/New_York'
        shutil.copyfile('/usr/share/zoneinfo/America/New_York', target)
        env = base.copy()
        env['TZDIR'] = str(alias)
        processes = [
            subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding='utf-8',
                env=env,
            )
            for command in [reference, [a.native, '--probe']]
        ]
        observations = []

        def step(row):
            result = []
            for process in processes:
                process.stdin.write(row + '\n')
                process.stdin.flush()
                result.append(process.stdout.readline().rstrip('\n'))
            observations.append(dict(row=row, source=result[0], native=result[1]))
            assert result[0] == result[1] and result[0], observations[-1]

        try:
            step(enc('Eastern Standard Time'))
            target.unlink()
            step(enc('America/New_York'))
            step(enc('eastern standard time'))
            step('!')
            step(enc('America/New_York'))
            shutil.copyfile('/usr/share/zoneinfo/America/New_York', target)
            step(enc('America/New_York'))
        finally:
            for process in processes:
                process.stdin.close()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            shutil.copyfile('/usr/share/zoneinfo/America/New_York', target)
        (a.work / 'alias-mutation.json').write_text(
            json.dumps(observations, indent=2), encoding='utf-8'
        )
        groups.append(dict(label='alias-mutation', cases=len(observations), failures=0))
        env = base.copy()
        env.update(ownenv)
        subprocess.run([a.native, '--provider-units'], env=env, check=True, capture_output=True)
    report = dict(
        scope='Actual .NET8.0.31 named lookup metadata/UTF16/ID identity and owned fixtures. Does not claim full TimeZoneInfo UI/serialization, macOS execution, Windows Local-to-named cache interaction, or unconstrained VM allocation/race equivalence.',
        groups=groups,
        cases=sum(x['cases'] for x in groups),
        nativeSha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
    )
    (a.work / 'time-zone-lookup-results.json').write_text(
        json.dumps(report, indent=2), encoding='utf-8'
    )
    print(json.dumps(dict(cases=report['cases'], failures=0)))


if __name__ == '__main__':
    main()
