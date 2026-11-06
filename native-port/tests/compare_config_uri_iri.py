import argparse, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    return s.encode('utf-16-be', 'surrogatepass').hex() or '-'


def call(cmd, rows=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        text=True,
        capture_output=True,
        encoding='utf-8',
        timeout=240,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-1000:], r.stderr[-1000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--full-scalars', action='store_true')
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        AllowUnsafeBlocks='true',
    ).items():
        ET.SubElement(props, k).text = v
    g = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(g, 'Compile', Include=str(ROOT / 'native-port/tests/config_uri_iri_reference.cs'))
    cs = w / 'IriReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    call(['dotnet', 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'])
    rows = []
    for c in map(chr, range(65536)):
        for component in [0, 2, 16, 32, 64, 127]:
            rows.append(str(component) + ' ' + enc('a' + c + 'b'))
    for first, last in itertools.product(range(256), repeat=2):
        for component in [0, 32]:
            rows.append(str(component) + ' ' + enc('a%' + f'{first:02x}%{last:02X}' + 'b'))
    for c in [
        '%',
        '%G',
        '%GG',
        '%4G',
        '%41%',
        '%C3%A9%2F%20%GG',
        '\ud800\udc00',
        '\udbff\udfff',
        '\ud800a',
        '\udc00\ud800',
        '\ue000',
        '\uffff',
    ]:
        for component in [0, 1, 2, 4, 8, 16, 32, 64, 127, 128, 255, 0x80000000, 0xFFFFFFFF]:
            rows.append(str(component) + ' ' + enc(c))
    rng = random.Random(990170)
    parts = [
        'a',
        '%',
        '%00',
        '%41',
        '%GG',
        '%80',
        '%C3',
        '%A9',
        '%F0',
        '%9F',
        '%99',
        '%82',
        '\ud800',
        '\udc00',
        '\ue000',
        'é',
        '😀',
        ' ',
        '\r',
        '\0',
        '\\',
        '?',
        '#',
        ';',
    ]
    for _ in range(20000):
        rows.append(
            str(rng.choice([0, 2, 16, 32, 64, 127]))
            + ' '
            + enc(''.join(rng.choice(parts) for _ in range(rng.randrange(20))))
        )
    if a.full_scalars:
        for c in range(0x110000):
            if 0xD800 <= c <= 0xDFFF:
                continue
            for component in [0, 32]:
                rows.append(str(component) + ' ' + enc(chr(c)))
    source = call(['dotnet', str(w / 'app/IriReference.dll')], rows)
    native = call([a.native, '--probe'], rows)
    assert len(source) == len(native) == len(rows)
    diff = [dict(row=r, source=s, native=n) for r, s, n in zip(rows, source, native) if s != n]
    (w / 'iri-results.json').write_text(
        json.dumps(
            dict(
                cases=len(rows), fullScalars=a.full_scalars, passed=not diff, differences=diff[:10]
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    print(len(rows), len(diff), diff[:3])
    assert not diff


if __name__ == '__main__':
    main()
