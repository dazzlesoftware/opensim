from pathlib import Path
import subprocess, json, argparse

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
d = Path(__file__).resolve().parent
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
bases = [
    None,
    '',
    'relative',
    'relative/path',
    '../a',
    '?q',
    '#f',
    'http://example/a/b',
    'http://example/a/b?old#f',
    'https://user:pass@EXAMPLE:443/a/../b',
    'http://[::1]:8080/a',
    'file:///C:/a/b',
    'file://server/share/a',
    'file:///tmp/a',
    'C:\\a\\b',
    '/tmp/a',
    '\\\\server\\share\\a',
    'http:relative',
    'file:relative',
]
relatives = [
    None,
    '',
    'a',
    '../a',
    '../../b',
    './x',
    '/root',
    '//host/a',
    '\\root',
    '\\\\server\\share\\a',
    '?q',
    '#f',
    '?q#f',
    'a b',
    'a%20b',
    '%2e%2e/x',
    '%2f',
    '%zz',
    'a#b',
    'a?b',
    'http://OTHER:80/a/../b',
    'https://example:443/é',
    'file:///tmp/x',
    'file:///C:/x',
    'file://server/share/x',
    'C:\\x',
    'C:/x',
    'c|/x',
    'é中Ω',
    '𐀀',
    '\x00',
    '  ',
    '\t',
    'a\x00b',
    'a\r\nb',
    'file:relative',
    'http:relative',
]
bases += [
    'https://EXAMPLE:444/a%2fb/%2e%2e/c?x=%41#f%20g',
    'http://user:@example/a',
    'http://:pass@example/a',
    'http://user%40name:pa%3ass@example/a',
    'http://example./a',
    'http://éxample.org/é?q=中#Ω',
    'http://127.000.000.001/a',
    'http://2130706433/a',
    'http://[2001:db8::1]/a',
    'http://[fe80::1%25zone]/a',
    'http://example/a\\b',
    'http://example/a//b',
    'http://example/a%20b',
    'http://example/a%2fb',
    'http://example/a%5cb',
    'http://example/a%2e%2e/b',
    'http://example/a/./b/../',
    'file:///C:/a%20b/c?query#fragment',
    'file://localhost/C:/a/b',
    'file://LOCALHOST/tmp/a',
    'file://server/share/a?query#fragment',
    'file:/tmp/a',
    'file:////server/share/a',
    'file://///server/share/a',
    'C:/a/b',
    'C|/a/b',
    'c:\\a b\\c',
    '/tmp/a%20b',
    'relative%20path',
    '../é',
    '#',
    '?',
    'a:b',
    'a:/b',
    'file:',
    'https:',
    'https:/a',
    'https://',
    'file://',
    'file:///',
    'http://example:bad/a',
    'http://[invalid]/a',
    'http://example/a\x00b',
]
relatives += [
    '.',
    '..',
    './',
    '../',
    '../../../',
    'a//b',
    'a\\b',
    './a/../b',
    'a/./b/../../c',
    'a.../b',
    'a./b',
    'a../b',
    '%2e',
    '.%2e',
    '%2e.',
    '%2e%2e',
    '%2e%2e/',
    '%2e%2e\\x',
    'a/%2f/b',
    'a/%5c/b',
    'a/%2E%2E/b',
    'a/%252e%252e/b',
    '%',
    '%0',
    '%00',
    '%FF',
    '%C0%AF',
    '%ED%A0%80',
    '%F0%90%80%80',
    'é%20中',
    'a?x=é#Ω',
    'a#é?中',
    '?',
    '??',
    '#',
    '##',
    '?#',
    '#?',
    'a?b?c#d#e',
    'http://example',
    'https://example',
    'HTTP://EXAMPLE:80',
    'http://user:pass@other:81/x',
    'https://[::1]/x',
    'http://',
    'https:/x',
    'http:/x',
    'http:\\x',
    'http:',
    'file:',
    'file:/',
    'file:/x',
    'file://',
    'file://x',
    'file:///',
    'file:///x',
    'file:////x/a',
    'file://///x/a',
    'file://localhost/x',
    'file://host:80/a',
    'C:relative',
    'C:',
    'C|',
    'C|/x',
    'D:\\x',
    '/C:/x',
    '//host',
    '///host',
    '////host/a',
    '\\',
    '\\\\',
    'x:y',
    'x:/y',
    'a+b:c',
    'a.b:c',
    'a-b:c',
    '1a:b',
    ':x',
    'a\tb',
    'a\nb',
    'a\rb',
    'a\x01b',
    'a\x7fb',
    '\ud800',
    '\udfff',
    'a\ud800b',
    'a\ufeffb',
    '\u200e/x',
    '\u00a0',
    '\u2028',
    '\u2029',
    'a ',
    ' a',
    ' a ',
    '\r\nhttp://example/a\t',
    '   http://example/a   ',
]

for scheme in [
    'ftp',
    'gopher',
    'nntp',
    'news',
    'mailto',
    'uuid',
    'telnet',
    'ldap',
    'net.tcp',
    'net.pipe',
    'vsmacros',
    'a+b',
    'a.b',
    'a-b',
]:
    bases += [
        scheme + ':' + body
        for body in [
            'body',
            'body?old#f',
            '/a/b',
            '//host/a/b',
            '//user@EXAMPLE:81/a?old#f',
            '//[fe80::1%25zone]:81/a',
            '///é?中#Ω',
        ]
    ]


def code(v):
    return '~' if v is None else '-' if v == '' else v.encode('utf-16-be', 'surrogatepass').hex()


rows = [(b, v) for b in bases for v in relatives]
payload = '\n'.join(code(b) + ' ' + code(v) for b, v in rows) + '\n'
(w / 'inputs.txt').write_text(payload)
subprocess.run(
    [
        a.dotnet,
        'build',
        str(d / 'nini_xml_resolver_reference/Reference.csproj'),
        '-o',
        str(w / 'app'),
        '--nologo',
        '-v:q',
    ],
    check=True,
)
outputs = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve()), '--probe']),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, cwd=w)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    outputs[name] = r.stdout.splitlines()
    assert len(outputs[name]) == len(rows), (name, len(outputs[name]), len(rows))
diff = [
    {'input': row, 'expected': e, 'actual': n}
    for row, e, n in zip(rows, outputs['reference'], outputs['native'])
    if e != n
]
report = {
    'cases': len(rows),
    'differences': len(diff),
    'examples': diff[:20],
    'applicationParityVerified': False,
    'scope': 'Actual XmlUrlResolver.ResolveUri with composed nativeHTTP/fileURI providers and generic-scheme gap probes; all mismatches remain counted.',
    'missingGenericProviderCases': sum(
        x['actual'].startswith('NATIVE:File URI provider cannot handle generic schemes')
        for x in diff
    ),
}
(w / 'all-differences.json').write_text(json.dumps(diff, indent=2))
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(json.dumps({'cases': len(rows), 'differences': len(diff)}))
raise SystemExit(bool(diff))
