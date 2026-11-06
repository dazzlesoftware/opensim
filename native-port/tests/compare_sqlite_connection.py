"""Run the exact bundled SQLite default parser, URI mapper and Boolean converter."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
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
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        group, 'Compile', Include=str(ROOT / 'native-port/tests/sqlite_connection_reference.cs')
    )
    ref = ET.SubElement(group, 'Reference', Include='System.Data.SQLite')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/System.Data.SQLite.dll')
    cs = w / 'Reference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    keys = [
        'Data Source',
        'URI',
        'data source',
        'DataSource',
        '',
        '"Data Source"',
        "'URI'",
        'Σ',
        'σ',
        'ς',
        'İ',
        'i',
        'ı',
        'ſ',
        'S',
        '𐐀',
        '𐐨',
        'é',
        'É',
        'e\u0301',
    ]
    values = [
        '',
        'foo.db',
        'file:foo.db,version=3',
        'file://x.db',
        'file:foo.db;Version=3',
        '"a;b"',
        "'a;b'",
        '"a""b"',
        "'a''b'",
        'a=b',
        'a\\;b',
        'a\\"b',
        'a\\\\b',
        'a\\z',
        'a\\',
        '"',
        '\u2003value\u2003',
        '\0',
    ]
    cases = [
        (mode, key + '=' + value)
        for mode, key, value in itertools.product(['P', 'A'], keys, values)
    ]
    cases += [
        (mode, text)
        for mode, text in itertools.product(
            ['P', 'A'],
            [
                '',
                ';',
                ' ; ; ',
                'name',
                'name;foo',
                'Data Source=a;DATA SOURCE=b',
                '"URI"=a;uri=b',
                'Data Source=a;URI=file:b',
                'URI=file:b;Data Source=a',
                "'=x",
                '"a=b"=c',
                'a=1;=2',
                'a=1;;b=2',
                'a=1; b = 3; A=2',
            ],
        )
    ]
    rng = random.Random(8931)
    alphabet = 'ab;=\\"\' \t\n'
    cases += [
        (mode, ''.join(rng.choice(alphabet) for _ in range(rng.randrange(25))))
        for mode in ['P', 'A']
        for _ in range(1200)
    ]
    cases += [('P', a + '=1;' + b + '=2') for a, b in itertools.product(keys, keys)]
    cases += [
        ('M', s)
        for s in [
            '',
            ':memory:',
            'file:abc',
            'file://abc',
            'FILE:abc',
            'FiLe://abc',
            'file:///tmp/a',
            '/tmp/a',
            'foo',
            ' file:a',
            'file:',
            'file://',
            'file:/a?mode=ro',
            'file:\\a',
            '/',
            '//server/share',
        ]
    ]
    cases += [
        ('B', s)
        for s in [
            '',
            't',
            'tr',
            'tru',
            'true',
            'truex',
            'f',
            'fa',
            'fal',
            'fals',
            'false',
            'falsex',
            'T',
            'TR',
            'FaLs',
            'TRUE',
            'FALSE',
            'yes',
            'no',
            'on',
            'off',
            'Y',
            'N',
            '1',
            '0',
            '2',
            ' true',
            'false ',
            '\0',
            'İ',
            'ſ',
            'ｔｒｕｅ',
        ]
    ]
    cases += [
        ('C', key + '=' + value + ';URI=file:fallback.db')
        for key, value in itertools.product(
            ['Data Source', 'data source', 'DataSource', 'Data_Source', '"Data Source"'],
            ['', ':memory:', ':MEMORY:', 'test.db', '"a;b.db"', "'quoted.db'"],
        )
    ]
    cases += [
        ('C', s)
        for s in [
            'URI=file:a.db,version=3',
            'URI=FILE://a.db',
            'URI=file:;Data Source=selected.db',
            'Data Source=;URI=/tmp/a',
            'URI=file:a;Data Source=b',
            'Data Source=b;URI=file:a',
            'DataSource=a',
            'URI=relative.db',
            'Data_Source=a;DataSource=b;Data Source=c',
            'Data Source=;DataSource=b;URI=file:c',
        ]
    ]
    commands = '\n'.join(mode + value.encode().hex() for mode, value in cases) + '\n'
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/Reference.dll')], [a.native]]:
        r = subprocess.run(cmd, input=commands, capture_output=True, text=True, timeout=90)
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    differences = [
        dict(mode=c[0], value=c[1], source=s, native=n) for c, s, n in zip(cases, *rows) if s != n
    ]
    (w / 'sqlite-connection-results.json').write_text(
        json.dumps(
            dict(
                passed=not differences,
                observations=len(cases),
                sourceSha256=hashlib.sha256(
                    (ROOT / 'bin/System.Data.SQLite.dll').read_bytes()
                ).hexdigest(),
                scope='Actual bundled default parser with selected Unicode ordinal key ordering/duplicates, quoting, escapes, semicolons/comma filenames, URI mapping and Boolean aliases/prefixes. Framework/legacy parsers, environment settings, FullUri/open flags, pooling, general connection options, invalid UTF16 and other runtime/platform profiles pending.',
                differences=differences[:12],
            ),
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:3]
    print(f'PASS: {len(cases)} bundled SQLite connection observations')


if __name__ == '__main__':
    main()
