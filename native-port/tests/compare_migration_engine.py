"""Unchanged original migration execution with the actual managed SQLite provider."""

import argparse, hashlib, itertools, json, platform, shutil, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'StartupObject': 'MigrationEngineReference',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    source = ROOT / 'OpenSim/Data/Migration.cs'
    for f in [
        source,
        ROOT / 'native-port/tests/migration_parser_reference.cs',
        ROOT / 'native-port/tests/migration_engine_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='System.Data.SQLite')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/System.Data.SQLite.dll')
    cs = w / 'MigrationEngine.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    arm = platform.machine().lower() in ['arm64', 'aarch64']
    src, dst = {
        'Windows': ('e_sqlite3.dll', 'e_sqlite3.dll'),
        'Linux': ('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so', 'libe_sqlite3.so'),
        'Darwin': (
            'libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib',
            'libe_sqlite3.dylib',
        ),
    }[platform.system()]
    shutil.copy2(ROOT / 'bin/lib64' / src, w / 'app' / dst)
    cases = []

    def op(name, **kw):
        return dict(op=name, **kw)

    def add(resources=[], setup='', type='AssetStore', sub='', ops=None, fail='', no_db=False):
        cases.append(
            dict(
                resources=[
                    dict(name=n, data=None if s is None else (s.encode().hex() or '.'))
                    for n, s in resources
                ],
                setup=setup,
                type=type,
                sub=sub,
                ops=ops
                or [
                    op('version'),
                    op('table'),
                    op('update'),
                    op('version'),
                    op('update'),
                    op('version'),
                ],
                fail=fail,
                no_db=no_db,
            )
        )

    scripts = [
        '',
        ':VERSION 1\nCREATE TABLE payload(value);\nINSERT INTO payload VALUES(1);',
        ':VERSION 1\nBEGIN;\nCREATE TABLE payload(value);\nINSERT INTO payload VALUES(1);\nMISSING SQL;\nCOMMIT;',
        ':VERSION 1\nCREATE TABLE payload(value);\nMISSING SQL;',
        ':VERSION 1\nBEGIN;\n:GO\nCREATE TABLE payload(value);\nINSERT INTO payload VALUES(1);\n:GO\nCOMMIT;\n:VERSION 2\nINSERT INTO payload VALUES(2);',
        ':VERSION 1\nCREATE TABLE payload(value);\n:VERSION 2\nINSERT INTO payload VALUES(2);\n:VERSION bad\nIGNORE;',
        ':VERSION 0\nCREATE TABLE ignored(value);\n:VERSION 6\nCREATE TABLE payload(value);',
    ]
    setups = [
        '',
        'CREATE TABLE migrations(name,version int);',
        'CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES(\'migrations\',1);',
        "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',1);",
        "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',-1),('AssetStore',0);",
        'CREATE TABLE migrations(wrong);',
    ]
    for script, setup in itertools.product(scripts, setups):
        add([('R.AssetStore.migrations', script)], setup)
    for type, sub, setup in itertools.product(
        ['AssetStore', 'migrations', '', None, "a'b"], ['', 'SQLite'], setups[:3]
    ):
        add(
            [
                ('R.AssetStore.migrations', scripts[1]),
                ('R.AssetStore.SQLite.migrations', scripts[1]),
            ],
            setup,
            type,
            sub,
        )
    for no_db in [False, True]:
        for resources in [
            [],
            [('R.001_AssetStore.sql', '')],
            [('R.001_AssetStore.sql', ' ')],
            [('R.AssetStore.migrations', None)],
        ]:
            add(resources, no_db=no_db)
    add(
        [('R.AssetStore.migrations', scripts[1])],
        "CREATE TABLE migrations(NAME,VERSION int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',0);",
    )
    for kind, value in itertools.product(
        ['int', 'INTEGER', 'TEXT', ''],
        [
            '0',
            '-1',
            '2147483647',
            '2147483648',
            '9223372036854775807',
            "'bad'",
            "'1.5x'",
            '1.5',
            'NULL',
            "x'0102'",
        ],
    ):
        add(
            [],
            f"CREATE TABLE migrations(name,version {kind});INSERT INTO migrations VALUES('AssetStore',{value});",
            ops=[op('version'), op('find', type='other'), op('set', value=7), op('version')],
        )
    for value in [-2147483648, -1, 0, 1, 2147483647]:
        add(
            [],
            ops=[
                op('set', value=value),
                op('table'),
                op('set', value=value),
                op('version'),
                op('set', value=2),
                op('version'),
            ],
        )
    for batches in [
        [],
        [''],
        [' '],
        ['CREATE TABLE payload(value);', 'INSERT INTO payload VALUES(1);'],
        ['BEGIN;', 'CREATE TABLE payload(value);', 'MISSING SQL;'],
        ['SELECT 1;'],
    ]:
        add([], ops=[op('execute', batches=batches), op('raw', sql='ROLLBACK;'), op('version')])
    for fail in [
        'names',
        'read:R.AssetStore.migrations',
        'info:[MIGRATIONS]: Creating migrations at version 1',
        'info:[MIGRATIONS]: Upgrading AssetStore to latest revision 1.',
        'info:[MIGRATIONS]: Creating AssetStore at version 1',
        'info:[MIGRATIONS]: Updating AssetStore to version 1',
        'debug:[MIGRATIONS]: An error has occurred in the migration.  If you\'re running OpenSim for the first time then you can probably safely ignore this, since certain migration commands attempt to fetch data out of old tables.  However, if you\'re using an existing database and you see database related errors while running OpenSim then you will need to fix these problems manually. Continuing.',
    ]:
        for script in scripts[1:4]:
            add([('R.AssetStore.migrations', script)], fail=fail)
    commands = '\n'.join(json.dumps(c, separators=(',', ':')).encode().hex() for c in cases) + '\n'
    rows = []
    for command in [[a.dotnet, str(w / 'app/MigrationEngine.dll')], [a.native]]:
        r = subprocess.run(command, input=commands, capture_output=True, text=True, timeout=180)
        assert r.returncode == 0, r.stderr[-2000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    assert not any('FIXTURE_EX' in v for row in rows for v in row)
    differences = [
        dict(case=c, source=s[:4500], native=n[:4500]) for c, s, n in zip(cases, *rows) if s != n
    ]
    (w / 'migration-engine-results.json').write_text(
        json.dumps(
            dict(
                passed=not differences,
                observations=len(cases),
                sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                managedProviderSha256=hashlib.sha256(
                    (ROOT / 'bin/System.Data.SQLite.dll').read_bytes()
                ).hexdigest(),
                scope='Entire unchanged original Migration, actual SQLite provider, resource regex/reader and logger. Init/update/version setter/query, real SQL batches, partial changes, rollback/failure/version stamping, re-runs, malformed metadata, typed version cells and logger/resource errors. Provider error message text normalized; general custom ADO lifecycle/timeout/cancellation/concurrency/culture/runtime/platform semantics pending.',
                differences=differences[:5],
            ),
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} migration execution source observations')


if __name__ == '__main__':
    main()
