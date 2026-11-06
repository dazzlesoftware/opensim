"""Actual Linux owned-fixture EINTR injection; child-only LD_PRELOAD."""

import argparse, fcntl, hashlib, json, os, shutil, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--injector', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    assert sys.platform.startswith('linux')
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
        Include=str(HERE / 'posix_io_retry_reference.cs'),
    )
    projectfile = a.work / 'Reference.csproj'
    ET.ElementTree(project).write(projectfile, encoding='unicode')
    subprocess.run(
        [a.dotnet, 'build', str(projectfile), '-o', str(a.work / 'reference')],
        check=True,
        capture_output=True,
    )
    version = subprocess.run(
        [a.dotnet, str(a.work / 'reference/Reference.dll'), '--runtime'],
        text=True,
        capture_output=True,
        check=True,
    ).stdout.strip()
    assert version == '8.0.31', version
    root = a.work / 'owned'
    directory = root / 'directory'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'file').write_bytes(b'ABC')
    (directory / 'childdir').mkdir(exist_ok=True)
    for name, target in [('link', directory / 'file'), ('linkdir', directory / 'childdir')]:
        link = directory / name
        if link.is_symlink():
            link.unlink()
        link.symlink_to(target)
    rows = []
    for op, action in [
        ('fstat', 'read'),
        ('flock', 'read'),
        ('flock_locked', 'read'),
        ('ftruncate', 'write'),
        ('opendir', 'list'),
        ('stat', 'list'),
    ]:
        path = directory if action == 'list' else root / 'data'
        row = dict(operation=op, action=action, runs={})
        lockfile = None
        if op == 'flock_locked':
            path.write_bytes(b'ABC')
            lockfile = open(path, 'rb')
            fcntl.flock(lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            for label, cmd in [
                ('source', [a.dotnet, str(a.work / 'reference/Reference.dll')]),
                ('native', [a.native]),
            ]:
                if action != 'list':
                    path.write_bytes(b'ABC')
                env = os.environ.copy()
                env.update(
                    LD_PRELOAD=str(Path(a.injector).resolve()),
                    OWN_ROOT=str(root),
                    INJECT_OP=op.split('_')[0],
                )
                result = subprocess.run(
                    cmd + [action, str(path)],
                    env=env,
                    text=True,
                    capture_output=True,
                    timeout=30,
                    check=True,
                )
                row['runs'][label] = dict(
                    stdout=result.stdout.strip(),
                    stderr=result.stderr.strip(),
                    injections=result.stderr.count('INJECTED'),
                    calls=result.stderr.count('CALL'),
                )
        finally:
            if lockfile:
                lockfile.close()
        rows.append(row)
    zones = root / 'zones'
    zones.mkdir(exist_ok=True)
    shutil.copyfile('/usr/share/zoneinfo/America/New_York', zones / 'Real')
    if (zones / 'Owned').exists() or (zones / 'Owned').is_symlink():
        (zones / 'Owned').unlink()
    (zones / 'Owned').symlink_to(zones / 'Real')
    for action, op, locked in [
        ('zone-named', 'flock', False),
        ('zone-named', 'flock', True),
        ('zone-local', 'stat', False),
        ('zone-local', 'fstat', False),
        ('zone-local', 'flock', False),
        ('zone-local', 'flock', True),
        ('fifo', 'lseek', False),
    ]:
        row = dict(action=action, operation=op, locked=locked, runs={})
        holder = None
        if locked:
            holder = open(zones / 'Owned', 'rb')
            fcntl.flock(holder, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            for label, cmd in [
                ('source', [a.dotnet, str(a.work / 'reference/Reference.dll')]),
                ('native', [a.native]),
            ]:
                path = zones / 'Owned'
                fd = None
                if action == 'fifo':
                    path = root / 'pipe'
                    if path.exists():
                        path.unlink()
                    os.mkfifo(path)
                    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
                    os.write(fd, b'ABC')
                env = os.environ.copy()
                env.update(
                    LD_PRELOAD=str(Path(a.injector).resolve()),
                    OWN_ROOT=str(root),
                    INJECT_OP=op,
                    TZDIR=str(zones),
                    TZ='Owned',
                )
                try:
                    result = subprocess.run(
                        cmd + [('read' if action == 'fifo' else action), str(path)],
                        env=env,
                        text=True,
                        capture_output=True,
                        timeout=30,
                        check=True,
                    )
                finally:
                    if fd is not None:
                        os.close(fd)
                row['runs'][label] = dict(
                    stdout=result.stdout.strip(),
                    stderr=result.stderr.strip(),
                    injections=result.stderr.count('INJECTED'),
                    calls=result.stderr.count('CALL'),
                )
        finally:
            if holder:
                holder.close()
        rows.append(row)

    # Existing legacy named-zone exception hierarchy is outside this IO patch;
    # compare failure outcome there, preserving complete source/native text in report.
    def outcome(row, run):
        value = run['stdout']
        return 'ERR' if row['action'] == 'zone-named' and value.startswith('ERR ') else value

    failures = [
        r
        for r in rows
        if r['runs']['source']['injections'] != 1
        or r['runs']['native']['injections'] != 1
        or outcome(r, r['runs']['source']) != outcome(r, r['runs']['native'])
        or r['runs']['source']['calls'] != r['runs']['native']['calls']
    ]
    report = dict(
        scope='Actual Linux .NET8 and native public FileStream/Directory, child-only owned paths/FDs; includes actual FIFO lseek, named/local timezone stat/fstat/locks; excludes macOS execution; named-zone errors compare outcome only',
        cases=len(rows),
        failures=failures,
        observations=rows,
        nativeSha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
        injectorSha256=hashlib.sha256(Path(a.injector).read_bytes()).hexdigest(),
    )
    (a.work / 'posix-io-retry-results.json').write_text(
        json.dumps(report, indent=2), encoding='utf-8'
    )
    assert not failures, json.dumps(failures, indent=2)
    print('13 actual Linux EINTR cases passed')


if __name__ == '__main__':
    main()
