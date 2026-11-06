"""Entire original asset provider initialization with real Migration and embedded resource."""

import argparse, hashlib, itertools, json, platform, shutil, subprocess, uuid, xml.etree.ElementTree as ET
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
    text = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        text[: text.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'AllowUnsafeBlocks': 'true',
        'StartupObject': 'AssetInitializerReference',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    sources = [
        ROOT / f
        for f in [
            'OpenSim/Data/SQLite/SQLiteAssetData.cs',
            'OpenSim/Data/Migration.cs',
            'OpenSim/Data/AssetDataBase.cs',
            'OpenSim/Data/IAssetData.cs',
            'OpenSim/Framework/IPlugin.cs',
            'OpenSim/Framework/AssetBase.cs',
            'OpenSim/Framework/NrbfReader.cs',
        ]
    ]
    for f in sources + [
        w / 'SLUtil.cs',
        ROOT / 'native-port/tests/migration_parser_reference.cs',
        ROOT / 'native-port/tests/asset_initializer_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    resource = ROOT / 'OpenSim/Data/SQLite/Resources/AssetStore.migrations'
    r = ET.SubElement(group, 'EmbeddedResource', Include=str(resource))
    ET.SubElement(r, 'LogicalName').text = 'OpenSim.Data.SQLite.Resources.AssetStore.migrations'
    for name in ['OpenMetaverseTypes', 'System.Data.SQLite']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AssetInitializer.csproj'
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
    schema = '\n'.join(
        s
        for s in resource.read_text(encoding='utf-8-sig').splitlines()
        if not s.startswith(':VERSION')
    )
    setups = [
        '',
        'CREATE TABLE migrations(name,version int);',
        "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',0);",
        "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',6);",
        "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',7);",
        'CREATE TABLE migrations(wrong);',
        schema + 'CREATE TABLE migrations(wrong);',
        schema
        + "CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AssetStore',6);",
    ]
    setups += [
        'CREATE TABLE assets(UUID NOT NULL PRIMARY KEY,Data);',
        "CREATE TABLE assets(UUID NOT NULL PRIMARY KEY,Data);INSERT INTO assets VALUES('00000000-0000-0000-0000-000000000001',x'0102');",
    ]
    op = lambda name, **kw: dict(op=name, **kw)
    cases = []
    connections = [
        'Data Source=$DB;Pooling=False',
        'URI=file:$DB,version=3',
        'Data Source="$DB";Version=3;Pooling=False',
        'data source=$DB;version=3;pooling=false',
        'Data Source=:memory:;Pooling=False',
    ]
    for setup, connection in itertools.product(setups, connections):
        cases.append(
            dict(
                setup=setup,
                fail='',
                ops=[
                    op('empty'),
                    op('invalid'),
                    op('get'),
                    op('init', connection=connection),
                    op('get'),
                    op('store'),
                    op('exists'),
                    op('get'),
                    op('init', connection=connection),
                    op('get'),
                    op('dispose'),
                    op('empty'),
                    op('invalid'),
                    op('get'),
                ],
            )
        )
    for fail in [
        'info:[MIGRATIONS]: Creating migrations at version 1',
        'info:[MIGRATIONS]: Upgrading AssetStore to latest revision 6.',
        'info:[MIGRATIONS]: Creating AssetStore at version 6',
        'debug:[MIGRATIONS]: AssetStore data tables already up to date at revision 6',
    ]:
        cases.append(
            dict(
                setup='',
                fail=fail,
                ops=[
                    op('init', connection=connections[0]),
                    op('get'),
                    op('init', connection=connections[0]),
                    op('store'),
                    op('get'),
                ],
            )
        )
    for connection in [
        'URI=FILE:$DB;Pooling=off',
        'Data Source=$DB;URI=invalid;Version=+3;Pooling=no',
        'URI=invalid;Data Source=$DB;Version=03;Pooling=0',
        '"Data Source"="$DB";Pooling=f',
        'Data Source=;URI=file:$DB;Pooling=N',
        'DataSource=$DB',
        'Data_Source=$DB',
        'Data_Source=wrong.db;DataSource=wrong.db;Data Source=$DB',
        'Data Source=$DB;DATA SOURCE=$DB',
        'URI=file:$DB;Version=2',
        'URI=invalid',
        'Data Source=$DB;Pooling=bogus',
    ]:
        cases.append(
            dict(
                setup='',
                fail='',
                ops=[
                    op('init', connection=connections[0]),
                    op('store'),
                    op('init', connection=connection),
                    op('get'),
                    op('empty'),
                    op('invalid'),
                    op('init', connection=connections[0]),
                    op('get'),
                ],
            )
        )
    rows = []
    run = w / str(uuid.uuid4())
    run.mkdir()
    for name, command in [
        ('source', [a.dotnet, str(w / 'app/AssetInitializer.dll')]),
        ('native', [str(Path(a.native).resolve())]),
    ]:
        folder = run / name
        folder.mkdir()
        r = subprocess.run(
            command + [str(folder)],
            input='\n'.join(json.dumps(c, separators=(',', ':')).encode().hex() for c in cases)
            + '\n',
            capture_output=True,
            text=True,
            timeout=180,
        )
        assert r.returncode == 0, r.stderr[-2000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    assert not any('FIXTURE_EX' in v for row in rows for v in row)
    differences = [
        dict(case=c, source=s[:4000], native=n[:4000]) for c, s, n in zip(cases, *rows) if s != n
    ]
    (w / 'asset-initializer-results.json').write_text(
        json.dumps(
            dict(
                passed=not differences,
                observations=len(cases),
                migrationExecuted=True,
                sourceHashes={
                    str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in sources
                },
                schemaSha256=hashlib.sha256(resource.read_bytes()).hexdigest(),
                scope='Original entire SQLiteAssetData and Migration with actual bundled SQLite and exact embedded v6 resource; selected original connection strings, fresh/existing/in-memory databases, missing schema at current version, initialization failure retention, retry/disposal and logger errors. General connection options, arbitrary ADO/assembly/IO/error-message/GC/concurrent/platform semantics pending.',
                differences=differences[:4],
            ),
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} asset initialization and migration source observations')


if __name__ == '__main__':
    main()
