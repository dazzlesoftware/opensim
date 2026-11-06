"""Real XElement formatting of actual SDK diagnostic XML; original detail helpers."""

from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET, random, hashlib

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)


def extract(source, signature):
    s = (root / source).read_bytes().decode('utf-8-sig')
    start = s.index(signature)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


methods = [
    extract('OpenSim/Framework/WebUtil.cs', sig)
    for sig in [
        'public static void LogOutgoingDetail(string type, int reqnum, string output)',
        'public static void LogOutgoingDetail(string context, string output)',
        'public static void LogResponseDetail(int reqnum, string input)',
    ]
]
util = [
    extract('OpenSim/Framework/Util.cs', sig)
    for sig in [
        'public static void BinaryToASCII(char[] chars)',
        'public static string BinaryToASCII(string src)',
    ]
]
(w / 'Extracted.cs').write_text(
    'using System;static partial class WebUtil{'
    + ''.join(methods)
    + '}static partial class Util{'
    + ''.join(util)
    + '}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [root / 'native-port/tests/xml_rpc_diagnostics_reference.cs', w / 'Extracted.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
ref = ET.SubElement(items, 'Reference', Include='XMLRPC')
ET.SubElement(ref, 'HintPath').text = str(root / 'bin/XMLRPC.dll')
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
enc = lambda s: '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'
rows = []


def add(mode, text, level=5, id=0):
    rows.append(f'{mode} {level} {id} {enc(text)}')


texts = [
    None,
    '',
    ' ',
    '\t',
    '\n',
    '\r',
    '\r\n',
    '<&>"\'',
    '😀',
    'a' * 199 + '😀' + 'z' * 80,
    ''.join(map(chr, range(256))),
] + [
    chr(i)
    for i in [
        0,
        1,
        8,
        9,
        10,
        11,
        12,
        13,
        31,
        32,
        127,
        128,
        0xD7FF,
        0xD800,
        0xDBFF,
        0xDC00,
        0xDFFF,
        0xE000,
        0xFFFD,
        0xFFFE,
        0xFFFF,
    ]
]
for n in [1, 2, 198, 199, 200, 201, 511, 1023, 1024, 1025, 2047, 2048, 4095, 4096, 4097]:
    texts.extend(['a' * n, 'a' * (n - 1) + '\r\n😀<&>'])
rng = random.Random(2907)
alphabet = list('abc<&>\r\n\t ') + ['é', '😀', '\ud800', '\udfff', '\x00', '\x7f', '\uffff']
texts += [''.join(rng.choices(alphabet, k=rng.randrange(0, 140))) for _ in range(500)]
for t in texts:
    for mode in [1, 3, 4, 5]:
        add(mode, t)
    for level in [0, 4, 5, 6]:
        for id in [0, -1, 2147483647]:
            add(2, t, level, id)
for x in [
    '',
    ' ',
    '<a/>',
    '<?xml version="1.0"?><a></a>',
    '<a> </a>',
    '<a>\n\t</a>',
    '<a><b/>\n<c></c></a>',
    '<a> hello <b/> world </a>',
    '<a>&#13;&#10;&#13;x\n</a>',
    '<a>&amp;&lt;&gt;&quot;&apos;</a>',
    '<a>&#0;</a>',
    '<a>&#xFFFF;</a>',
    '<a>\x00</a>',
    '<a>bad</b>',
    '<a/>extra',
    '<a/><b/>',
]:
    add(0, x)


def run(cmd):
    r = subprocess.run(
        cmd, input='\n'.join(rows) + '\n', capture_output=True, text=True, encoding='utf-8'
    )
    assert r.returncode == 0, r.stdout + r.stderr
    out = r.stdout.splitlines()
    assert len(out) == len(rows), (len(out), len(rows))
    return out


expected = run([a.dotnet, str(w / 'app/Reference.dll')])
actual = run([a.native])
for i, (e, n) in enumerate(zip(expected, actual)):
    if e != n:
        (w / 'failure.json').write_text(
            json.dumps(dict(index=i, input=rows[i], expected=e, actual=n), indent=2)
        )
        raise AssertionError(f'mismatch {i}: {rows[i][:200]}\nexpected {e}\nactual {n}')
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=True,
            cases=len(rows),
            scope='Actual SDK diagnostic string serialization through real XElement Parse/DisableFormatting; exact original WebUtil detail/BinaryToASCII helpers, controlled logger/debug property',
        ),
        indent=2,
    )
)
print(f'XML-RPC diagnostics {len(rows)} cases passed')
