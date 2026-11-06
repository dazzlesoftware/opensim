"""Actual bundled Nini reader over raw UTF16, all public read/state options."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def call(command, rows=None):
    r = subprocess.run(
        command,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr[-2000:])
    return r.stdout.splitlines()


def enc(text):
    return '-' if text is None else text.encode('utf-16-le', 'surrogatepass').hex() or '.'


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
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        group, 'Compile', Include=str(ROOT / 'native-port/tests/nini_reader_reference.cs')
    )
    r = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'NiniReaderReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    rows = []
    basic = 'r,r,r,r,r,r,q,c,r,q,d,r,q'

    def add(text, flags=4, point=0, nth=1, ops=basic):
        rows.append(f'{enc(text)} {flags} {point} {nth} {ops}')

    texts = [
        None,
        '',
        ' ',
        '\t\r',
        '\n',
        ';',
        ';x',
        ';x\n',
        '[a]',
        '[ a ] ; ignored\nkey = value ; comment\n',
        'x=',
        'x=\"\"',
        'x=\"a;b\";c',
        'x=a\"b\"c',
        'x=\"missing\nnext=y',
        '[missing\n',
        'noassign',
        'x=\\\n  next',
        'x=\\  \r\n  next',
        'x=\\',
        'x=\\ ;tail\nnext=y',
        '[a]\r\nx=foo\rbar\n',
        'x=a\u00a0',
        'x=\"a \"\n',
        'x=\"a\" garbage ;c\n',
        'x=one\n[a]\ny=two\n',
    ]
    for text, flags in itertools.product(texts, range(32)):
        add(text, flags)
    for text, flags, point, nth in itertools.product(
        texts, [0, 4, 6, 12, 20, 31], range(1, 4), [1, 2, 3, 7, 15]
    ):
        add(text, flags, point, nth)
    for text, ops in itertools.product(
        texts,
        [
            's,k,k,s,r,q',
            'k,k,k,k,q',
            'sc2300,sa3a00,r,mc,ma,r,gc,ga,r,q',
            'sc.,sa.,r,q,c,q',
            'sc-,sa-,r,q,d,q',
            'i1,l1,t0,a1,v1,r,r,r,q',
        ],
    ):
        add(text, ops=ops)
    for code in range(65536):
        add('x=' + chr(code) + '\n', ops='r,r,q')
    rng = random.Random(557391)
    alphabet = 'ab=;#[]"\\ \t\r\n\x00\uffff\u00a0\ud800\udc00'
    for _ in range(12000):
        add(
            ''.join(rng.choice(alphabet) for _ in range(rng.randrange(50))),
            rng.randrange(32),
            rng.randrange(4),
            rng.choice([1, 2, 3, 7]),
            rng.choice([basic, 's,k,k,s,r,q', 'sa3a00,sc2300,r,ma,mc,r,gc,ga,r,q']),
        )
    outputs = [
        call([a.dotnet, str(w / 'app/NiniReaderReference.dll')], rows),
        call([a.native, '--probe'], rows),
    ]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    differences = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]
    report = dict(
        passed=not differences,
        cases=len(rows),
        niniSha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
        scope='Actual bundled IniReader and IniException with raw UTF16 StringReader/fault-reader input. All read/move/close/dispose state, token fields, flags, borrowed delimiter arrays/getter copies, repeated calls/EOF recursion, quotes/comments/continuation, Unicode trailing trim, dynamic exception line/message and exact reader I/O call traces. Document collection/save/merge and general stream encoding are separate.',
        differences=differences[:5],
    )
    (w / 'nini-reader-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:2])
    print('PASS:', len(rows), 'actual Nini reader state/trace cases')


if __name__ == '__main__':
    main()
