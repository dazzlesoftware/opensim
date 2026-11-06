from pathlib import Path
import argparse, subprocess, itertools, json, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
d = Path(__file__).resolve().parent
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    Nullable='enable',
).items():
    ET.SubElement(props, k).text = v
ET.SubElement(
    ET.SubElement(project, 'ItemGroup'),
    'Compile',
    Include=str(d / 'config_http_windows_proxy_bypass_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)


def enc(s):
    return 'null' if s is None else s.encode('utf-16-be', 'surrogatepass').hex() or '-'


hosts = [
    'localhost',
    'LOCALHOST',
    'loopback',
    'Loopback',
    'localhost.',
    '127.0.0.1',
    '127.2.3.4',
    '0x7f000001',
    '2130706433',
    '192.0.2.10',
    '192.0.2.11',
    'flat',
    'flat.example',
    'höst.example',
    'xn--hst-sna.example',
    '_flat',
    'a_b.example',
    '[::1]',
    '[::127.0.0.1]',
    '[::ffff:127.0.0.1]',
    '[::ffff:127.0.0.2]',
    '[2001:db8::10]',
    '[2001:db8::11]',
    '[fe80::1%7]',
    '[fe80::1%8]',
]
uris = [
    scheme + '://' + host + '/path'
    for scheme, host in itertools.product(['http', 'https', 'ws', 'wss', 'ftp', 'custom'], hosts)
] + ['file:///C:/path', 'file://server/share', 'mailto:user@flat', 'relative', '', None]
bypasses = [
    None,
    '',
    ' ',
    '\t',
    '\u00a0',
    '*',
    '?',
    '*.example',
    'FLAT',
    'flat.example',
    'http://flat.example',
    'HTTPS://flat.example:80',
    '[2001:db8::10]:80',
    '[::1];flat',
    '<local>',
    '<LOCAL>',
    '<local>suffix',
    '[<local>]',
    'http://<local>',
    'foo; <local>;*.example',
    'foo bar;flat.example',
    '\tflat.example',
    'flat.example\tignored',
    'foo;; ;flat.example',
    ';[;];',
    'http://',
    'https://',
    '<loca',
    '<local> extra; flat.example',
    '*EXAMPLE',
    'höst.example',
    'xn--hst-sna.example',
]
strings = [
    '',
    'a',
    'A',
    'aa',
    'a.b',
    'ababa',
    '*',
    '?',
    'a?b',
    'a*b',
    'i',
    'I',
    'ı',
    'ſ',
    'K',
    'k',
    'σ',
    'ς',
    'Σ',
    'ß',
    'é',
    'É',
    '\x00',
    '\ud800',
    '\udc00',
    'a\ud800b',
]
patterns = strings + [
    '*',
    '**',
    '***',
    '*a',
    'a*',
    '*a*',
    'a*b*a',
    '*?*',
    '?*',
    '*.example',
    '*\ud800*',
    '*\x00*',
]
rows = (
    ['I ' + enc(uri) + ' -' for uri in uris]
    + ['B ' + enc(uri) + ' ' + enc(bypass) for uri, bypass in itertools.product(uris, bypasses)]
    + [
        'W ' + enc(value) + ' ' + enc(pattern)
        for value, pattern in itertools.product(strings, patterns)
    ]
)
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
out = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=90)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    out[name] = r.stdout.splitlines()
    assert len(out[name]) == len(rows), (name, len(out[name]), len(rows))
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
addresses = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), '--addresses']),
    ('native', [str(Path(a.native).resolve()), '--addresses']),
]:
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    (w / (name + '-addresses.txt')).write_text(r.stdout)
    (w / (name + '-addresses-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    addresses[name] = r.stdout.splitlines()
assert addresses['reference'] == addresses['native'], addresses
report = dict(
    cases=len(rows),
    differences=len(diff),
    examples=diff[:5],
    nativeInterfaceAddressesMatch=True,
    applicationParityVerified=False,
    scope='Actual Windows HttpWindowsProxy constructor/GetProxy with deterministic local address list; exact bypass patterns, local/loopback/IP/flat-name decisions. Actual SimpleRegex span method via dynamic bridge tests star-only Unicode/raw UTF16 matching. Uri.HostNameType/IsLoopback direct comparison and real Windows unicast interface enumeration match. Provider composition and full application remain incomplete.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(json.dumps(dict(cases=len(rows), differences=len(diff), examples=diff[:2])))
raise SystemExit(bool(diff))
