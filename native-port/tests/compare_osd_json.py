"""Compare JSON type conversion and wire bytes against the bundled structured-data SDK."""

import argparse, hashlib, json, random, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_osd_numeric import ROOT, cases


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
    for f in ['osd_json_reference.cs', 'osd_fixture_value.cs']:
        ET.SubElement(group, 'Compile', Include=str(ROOT / 'native-port/tests' / f))
    for n in ['OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        node = ET.SubElement(group, 'Reference', Include=n)
        ET.SubElement(node, 'HintPath').text = str(ROOT / 'bin' / (n + '.dll'))
    path = w / 'OsdJson.csproj'
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
    commands = [prefix + ' ' + value for value in cases() for prefix in ['s', 'm']]
    strings = [
        '',
        '{}',
        '[]',
        'null',
        'true',
        'false',
        '1',
        '"text"',
        '{"a":1,"b":null,"c":""}',
        '{"z":1,"a":2,"z":3}',
        '[] trailing',
        '{}{}',
        '{"a":01}',
        '{"a":-01}',
        '{"a":1.}',
        '{"a":.5}',
        '{"a":+1}',
        '{"a":1e}',
        '{"a":1e999}',
        '{"a":1e-999}',
        '{"a":9223372036854775808}',
        '{"a":18446744073709551615}',
        '{"a":18446744073709551616}',
        "{'x':'single'}",
        '/*comment*/{"x":1}',
        '{"x":true,//comment\n"y":false}',
        '[1,]',
        '{"a":1,}',
        '{"a":"\\v"}',
        '[NaN]',
        '[Infinity]',
        '[true false]',
        '["line\nfeed"]',
        '["\0"]',
        '["\\uD83E\\uDD8A"]',
        '{} /*unfinished',
        '[1',
        '{"a":',
        '{"a":1',
        '//hello',
        '  ',
        '\ufeff{}',
    ]
    rng = random.Random(7251)

    def sample(depth=0):
        choices = [
            None,
            False,
            True,
            rng.randrange(-(2**63), 2**63),
            rng.uniform(-1e30, 1e30),
            '試験 🦊 \0\n\t"/\\',
            '',
        ]
        if depth < 3:
            choices += [
                [sample(depth + 1) for _ in range(rng.randrange(4))],
                {
                    str(i) + rng.choice(['', '試験']): sample(depth + 1)
                    for i in range(rng.randrange(4))
                },
            ]
        return rng.choice(choices)

    for _ in range(250):
        strings.append(json.dumps({'field': sample()}, ensure_ascii=rng.choice([True, False])))
    for n in range(65536):
        if not 0xD800 <= n <= 0xDFFF:
            commands.append('s string ' + chr(n).encode().hex())
    for n in [0x10000, 0x1F98A, 0xEFFFF, 0x10FFFF] + [
        rng.randrange(0x10000, 0x110000) for _ in range(500)
    ]:
        commands.append('s string ' + chr(n).encode().hex())
    for value in [
        '0',
        '-0',
        '0.0',
        '-0.0',
        '-1',
        '1 ',
        '1e1',
        '1e1 ',
        '00',
        '-00',
        '01',
        '-01',
        '1.0',
        '1e999',
        '-9223372036854775809',
        '18446744073709551616',
    ]:
        strings += [value, '[' + value + ']', '{"x":' + value + '}']
    commands += ['p ' + s.encode().hex() for s in strings]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/OsdJson.dll')], [a.native]]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=45,
        )
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'osd-json-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'libraryHash': hashlib.sha256(
                    (ROOT / 'bin/OpenMetaverse.StructuredData.dll').read_bytes()
                ).hexdigest(),
                'differences': diffs,
            },
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:3], indent=2))
    print(f'PASS: {len(commands)} SDK JSON observations')


if __name__ == '__main__':
    main()
