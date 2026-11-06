"""Whole original SQLiteAssetData and actual bundled managed SQLite provider."""

import argparse, hashlib, itertools, json, platform, shutil, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def token(value):
    return (
        'N'
        if value is None
        else (value if isinstance(value, bytes) else value.encode()).hex() if value else '.'
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        source[: source.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    originals = [
        ROOT / f
        for f in [
            'OpenSim/Data/SQLite/SQLiteAssetData.cs',
            'OpenSim/Data/AssetDataBase.cs',
            'OpenSim/Data/IAssetData.cs',
            'OpenSim/Framework/IPlugin.cs',
            'OpenSim/Framework/AssetBase.cs',
            'OpenSim/Framework/NrbfReader.cs',
        ]
    ]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'AllowUnsafeBlocks': 'true',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in originals + [w / 'SLUtil.cs', ROOT / 'native-port/tests/asset_storage_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'System.Data.SQLite']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AssetStorage.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
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
    migration = ROOT / 'OpenSim/Data/SQLite/Resources/AssetStore.migrations'
    schema = migration.read_text(encoding='utf-8-sig')
    (w / 'schema.sql').write_text(
        '\n'.join(s for s in schema.splitlines() if not s.startswith(':VERSION')), encoding='utf-8'
    )
    uid = '00000000-0000-0000-0000-000000000001'
    zero = '00000000-0000-0000-0000-000000000000'
    commands = ['e', 'm 0 0', 'g ' + token(uid), 'd ' + token('bad')]

    def asset(
        id=uid,
        name='Name',
        desc='Description',
        type=0,
        flags=0,
        local=False,
        temp=False,
        creator='',
        data='data',
    ):
        return 'n ' + ' '.join(
            [
                token(id),
                token(name),
                token(desc),
                str(type),
                str(flags),
                str(int(local)),
                str(int(temp)),
                token(creator),
                token(data),
            ]
        )

    for name, desc in itertools.product(
        ['', 'a' * 64, 'a' * 65, '試験😀' * 25, 'a' * 63 + '😀', 'nul\0tail', None],
        ['', 'd' * 65, '試験😀' * 25, None],
    ):
        commands += [asset(name=name, desc=desc), 's', 'asset', 'g ' + token(uid), 'm 0 10']
    for type, flags, local, temp, creator, data in itertools.product(
        [-128, -1, 0, 49, 127],
        [-2147483648, 0, 2147483647],
        [False, True],
        [False, True],
        ['', 'creator', None],
        ['', 'abc\0tail', None],
    ):
        commands += [
            asset(type=type, flags=flags, local=local, temp=temp, creator=creator, data=data),
            's',
            'g ' + token(uid),
            'm 0 1',
        ]
    for id in ['bad', zero, uid, uid.upper(), '{' + uid + '}', uid.replace('-', '')]:
        commands += [
            asset(id=id),
            's',
            'e ' + token(uid) + ' ' + token(zero) + ' ' + token(uid),
            'd ' + token(id),
            'g ' + token(uid),
        ]
    for start, count in itertools.product(
        [-2147483648, -2, -1, 0, 1, 2, 2147483647], [-1, 0, 1, 2, 10]
    ):
        commands += ['m ' + str(start) + ' ' + str(count)]
    commands += [
        asset(),
        's',
        'c',
        'g ' + token(uid),
        'e',
        'm 0 0',
        'd ' + token('bad'),
        'd ' + token(uid),
        'o',
        'g ' + token(uid),
        'f',
        asset(name='x' * 65),
        's',
        'f',
        'asset',
        'a ' + token(uid),
        'g ' + token(uid),
    ]
    for field, values in [
        ('Type', ['-129', '128', "'7'", "'bad'", '7.5', 'NULL']),
        ('Local', ["'true'", "'False'", "'1'", '0', '-1', 'NULL']),
        (
            'asset_flags',
            [
                "'2147483648'",
                '2147483648',
                '1.5',
                'NULL',
                "'bogus'",
                "'1abc'",
                "'1.5abc'",
                "'-2.5'",
                "x'0102'",
            ],
        ),
        ('CreatorID', ['NULL', "'creator'"]),
        ('Data', ['NULL', "'not blob'", "x''"]),
    ]:
        for value in values:
            commands += [
                asset(),
                's',
                'raw ' + token('UPDATE assets SET ' + field + '=' + value),
                'g ' + token(uid),
                'm 0 1',
            ]
    commands += [asset(data=bytes(range(256))), 's', 'g ' + token(uid), 'c', 'o', 'g ' + token(uid)]
    rows = []
    for command, db in [
        ([a.dotnet, str(w / 'app/AssetStorage.dll')], w / 'source.db'),
        ([str(Path(a.native).resolve())], w / 'native.db'),
    ]:
        if db.exists():
            db.unlink()
        r = subprocess.run(
            command + [str(db)],
            input='\n'.join(commands) + '\n',
            cwd=w,
            capture_output=True,
            text=True,
            timeout=180,
        )
        assert r.returncode == 0, r.stderr[-2000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    cross_commands = [
        'e ' + token(uid) + ' ' + token(zero),
        'g ' + token(uid),
        'm 0 10',
        'd ' + token('bad'),
    ]
    cross = []
    for command, db in [
        ([a.dotnet, str(w / 'app/AssetStorage.dll')], w / 'native.db'),
        ([str(Path(a.native).resolve())], w / 'source.db'),
    ]:
        r = subprocess.run(
            command + [str(db)],
            input='\n'.join(cross_commands) + '\n',
            cwd=w,
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert r.returncode == 0, r.stderr
        cross.append(r.stdout.splitlines())
    assert cross[0] == cross[1] and len(cross[0]) == len(cross_commands), cross
    differences = [
        {'index': i, 'command': c, 'source': s, 'native': n}
        for i, (c, s, n) in enumerate(zip(commands, *rows))
        if s != n
    ]
    (w / 'asset-storage-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(commands),
                'crossDatabaseReadObservations': len(cross_commands) * 2,
                'migrationExecuted': False,
                'schemaSha256': hashlib.sha256(migration.read_bytes()).hexdigest(),
                'managedProviderSha256': hashlib.sha256(
                    (ROOT / 'bin/System.Data.SQLite.dll').read_bytes()
                ).hexdigest(),
                'sourceHashes': {
                    str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in originals
                },
                'scope': 'Whole original SQLiteAssetData with actual bundled System.Data.SQLite, SDK and AssetBase. Migration boundary executes original current v6 schema; general migration/connection-string/plugin/version semantics, arbitrary concurrent/error schedules and platforms remain pending.',
                'differences': differences[:8],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:2]
    print(f'PASS: {len(commands)} asset SQLite source observations')


if __name__ == '__main__':
    main()
