"""Compare native timezone lookup and daylight status with the installed .NET runtime."""

import argparse, datetime, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path


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
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(Path(__file__).with_name('time_zone_reference.cs').resolve()),
    )
    path = w / 'TimeZone.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    managed = [a.dotnet, str(w / 'app/TimeZone.dll')]
    zones = subprocess.check_output(managed + ['list'], text=True).splitlines()
    zones += [
        None,
        '',
        ' ',
        'UTC',
        'utc',
        'America/Los_Angeles',
        'Europe/London',
        'Australia/Lord_Howe',
        'Africa/Casablanca',
        'Asia/Kathmandu',
        'Pacific Standard Time',
        'pacific standard time',
        'Etc/UTC',
        'Etc/GMT',
        'US/Pacific',
        'Bad/Zone',
        'UTC\0',
        'America/Los_Angeles;Pacific Standard Time',
        'x' * 256,
    ]
    stamps = [
        int(datetime.datetime(y, m, 15, tzinfo=datetime.timezone.utc).timestamp())
        for y in [1900, 1945, 1970, 2000, 2010, 2024, 2026, 2038, 2050]
        for m in [1, 3, 4, 7, 10, 11, 12]
    ]
    stamps += [-62135596800, -62135596801, 253402300799, 253402300800]
    stamps += [1793523600 + d for d in [-3601, -3600, -1, 0, 1, 3599, 3600, 3601]]

    def token(s):
        return '-' if s is None else s.encode().hex() or '.'

    commands = [token(z) + ' ' + str(t) for z in zones for t in stamps]
    commands += ['local ' + str(t) for t in stamps]
    commands += subprocess.check_output(
        managed + ['transitions'], text=True, timeout=45
    ).splitlines()
    rows = []
    for cmd in [managed, [a.native]]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=120
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (
        len(rows[0]),
        len(rows[1]),
        len(commands),
    )
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'report.json').write_text(
        json.dumps({'observations': len(commands), 'differences': diffs}, indent=2),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(str(diffs[:15]) + ' total=' + str(len(diffs)))
    print('PASS: ' + str(len(commands)) + ' timezone runtime observations')


if __name__ == '__main__':
    main()
