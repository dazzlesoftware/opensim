"""Execute original SQLite friends queries and complete generic Store/DoQuery."""

import argparse, hashlib, json, platform, shutil, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        'OpenSim/Data/SQLite/SQLiteFriendsData.cs',
        'OpenSim/Data/SQLite/SQLiteGenericTableHandler.cs',
        'OpenSim/Data/SQLite/SQLiteFramework.cs',
        'OpenSim/Data/IFriendsData.cs',
        'native-port/tests/friends_sqlite_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(ROOT / f))
    for name in ['OpenMetaverseTypes', 'System.Data.SQLite']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'FriendsSqlite.csproj'
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
    arm = platform.machine().lower() in ['arm64', 'aarch64']
    source, target = {
        'Windows': ('e_sqlite3.dll', 'e_sqlite3.dll'),
        'Linux': ('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so', 'libe_sqlite3.so'),
        'Darwin': (
            'libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib',
            'libe_sqlite3.dylib',
        ),
    }[platform.system()]
    shutil.copy2(ROOT / 'bin/lib64' / source, w / 'app' / target)

    def t(s):
        return '-' if s is None else s.encode().hex() or '.'

    commands = []
    for principal in ['a', 'b', 'A', '', None, "quote'", '試験', 'nul\0id']:
        for friend in ['a', 'b', '', None, 'unicode😀', 'nul\0friend']:
            for flags, offered in [
                ('7', None),
                ('-2147483648', '123'),
                ('bad', ''),
                ('2147483648', None),
            ]:
                commands += [
                    's '
                    + t(principal)
                    + ' '
                    + t(friend)
                    + ' '
                    + t(flags)
                    + ' '
                    + ('!' if offered is None else t(offered)),
                    'g ' + t(principal),
                ]
    commands += [
        'g ' + t('b'),
        'd ' + t('b') + ' ' + t('a'),
        'g ' + t('a'),
        'd - -',
        'g -',
        'd ' + t('missing') + ' ' + t('unknown'),
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/FriendsSqlite.dll')], [a.native, 'sqlite']]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=45
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (
        len(rows[0]),
        len(rows[1]),
        len(commands),
    )
    differences = [
        {'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y
    ]
    (w / 'friends-sqlite-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(commands),
                'migrationExecuted': False,
                'managedProviderHash': hashlib.sha256(
                    (ROOT / 'bin/System.Data.SQLite.dll').read_bytes()
                ).hexdigest(),
                'sourceHashes': {
                    f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest()
                    for f in [
                        'OpenSim/Data/SQLite/SQLiteFriendsData.cs',
                        'OpenSim/Data/SQLite/SQLiteGenericTableHandler.cs',
                        'OpenSim/Data/SQLite/SQLiteFramework.cs',
                    ]
                },
                'differences': differences,
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if differences:
        raise AssertionError(str(differences[:3]))
    print(f'PASS: {len(commands)} friends SQLite source observations')


if __name__ == '__main__':
    main()
