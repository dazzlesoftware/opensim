"""Compare the complete IniWriter state/control-flow surface with bundled Nini.dll."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def call(cmd, rows=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=120,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-3000:], r.stderr[-3000:])
    return r.stdout.splitlines()


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


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
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        group, 'Compile', Include=str(ROOT / 'native-port/tests/nini_writer_reference.cs')
    )
    r = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'NiniWriterReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    rows = []

    def add(ops, kind=0, point=0, nth=1, mode=0):
        rows.append(f'{kind} {point} {nth} {mode} {ops}')

    texts = [
        None,
        '',
        'x',
        'a\nb\rc',
        ' \t\n',
        ';#=[]"',
        '\x00',
        '\ud800',
        '\udc00',
        '\uffff',
        '\u0085\u2028\u2029',
    ]
    for kind, start, v, s, k, c in itertools.product(
        range(5), ['', 'e,', 'c,', 'd,'], range(2), texts, texts, [None, '', 'comment\n']
    ):
        add(start + f'v:{v},sc:{enc(s)}:{enc(c)},kc:{enc(k)}:{enc(s)}:{enc(c)},t,q', kind)
    scripts = [
        'e,e,ec:-,ec:.,i:2,e,i:0,e',
        's:7800,k:-:-,v:1,k:-:-,kc:.:.:.,t',
        'nl:7800,ng,s:.,nl:-,ng,k:.:.,t',
        'c,c,f,t,d,d,z,s:.,e,k:.:.,i:-1,i:3,v:1,m:0,a:10,q',
        'd,s:.,f,t,d,z,c,q',
        's:.,k:.:.,sc:-:-,ec:-,ec:.,d,d,c,f,t',
        'h:1,d,z,s:.,h:0,d,d,z',
        'h:2,d,d,h:0,d,s:.,t',
        'ret:-,t,ret:.,t,ret:7800,t',
        'b:7800,s:.,bs:7900,b:7a00,t,bx,t,bc,b:6100,t,bw:-,q',
        'sf,s:.,d,d,z,c',
    ]
    for kind, script, point, nth, mode in itertools.product(
        range(5), scripts, range(9), [1, 2, 3], [0, 1, 2]
    ):
        add(script, kind, point, nth, mode)
    for code in range(65536):
        add(f's:.,k:.:{enc(chr(code))},v:1,k:.:{enc(chr(code))}')
    for delimiter, indent in itertools.product(
        [0, 10, 13, 59, 61, 0xD800, 0xFFFF], [-1, 0, 1, 4, 32]
    ):
        add(
            f'i:{indent},a:{delimiter},m:{delimiter},sc:7800:7900,kc:7800:0a000d00:7900,e,ec:7900,t'
        )
    rng = random.Random(95412)
    tokens = [
        'e',
        'ec:-',
        'ec:.',
        's:-',
        's:.',
        'sc:.:.',
        'k:.:.',
        'k:-:-',
        'kc:-:-:-',
        'v:0',
        'v:1',
        'i:-1',
        'i:0',
        'i:2',
        'c',
        'd',
        'z',
        'f',
        't',
        'nl:-',
        'nl:.',
        'ng',
        'b:7800',
        'bx',
        'bs:.',
        'bc',
        'bw:-',
        'q',
    ]
    for _ in range(3000):
        add(
            ','.join(rng.choices(tokens, k=15)),
            rng.randrange(5),
            rng.randrange(9),
            rng.randrange(1, 4),
            rng.randrange(3),
        )
    outputs = [
        call([a.dotnet, str(w / 'app/NiniWriterReference.dll')], rows),
        call([a.native, '--probe'], rows),
    ]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    diff = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]
    report = dict(
        passed=not diff,
        cases=len(rows),
        niniSha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
        scope='Actual IniWriter with real StringWriter and controlled TextWriter/StreamWriter subclasses. Raw UTF16 output, state/property/error/dependency traces, borrowed StringBuilder, payload and return identity, close/dispose retries/protected hooks. File/byte-stream encoding is a required separate constructor provider; arbitrary CLR finalization timing is not claimed. Windows executed; Unix platform newline branches not executed.',
        differences=diff[:3],
    )
    (w / 'nini-writer-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:1])
    print('PASS:', len(rows), 'actual Nini writer state/trace cases')


if __name__ == '__main__':
    main()
