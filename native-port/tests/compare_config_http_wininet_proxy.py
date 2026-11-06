from pathlib import Path
import argparse, subprocess, itertools, json, xml.etree.ElementTree as ET
import http.server, threading

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--stub', required=True)
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
    Include=str(d / 'config_http_wininet_proxy_reference.cs'),
)
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)


def enc(s):
    return 'null' if s is None else s.encode('utf-16-be').hex() or '-'


values = itertools.product(
    [0, 1],
    [0, 1],
    [None, '', 'http://pac.example/proxy.pac'],
    [None, '', 'http=one:1;https=two:2'],
    [None, '', '*.example'],
    [
        'http://destination/path?query#fragment',
        'https://destination/path',
        'ws://user:pass@destination:81/path?query#fragment',
        'wss://destination/path',
    ],
    ['0', '12015,0', '12015,12015,0', '12180,0', '12166,0'],
    [(None, None), ('', None), ('one:1;two:2', '')],
)
rows = [
    ' '.join(
        [
            str(detect),
            str(use),
            enc(url),
            enc(proxy),
            enc(bypass),
            enc(uri),
            errors,
            enc(pac),
            enc(pac_bypass),
        ]
    )
    for detect, use, url, proxy, bypass, uri, errors, (pac, pac_bypass) in values
]
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
out = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), str(Path(a.stub).resolve())]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=90)
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    out[name] = r.stdout.splitlines()
    assert len(out[name]) == len(rows), (name, len(out[name]), len(rows))
diff = [
    dict(row=row, expected=e, actual=n)
    for row, e, n in zip(rows, out['reference'], out['native'])
    if e != n
]
settings = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll'), '--native']),
    ('native', [str(Path(a.native).resolve()), '--native']),
]:
    r = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
    (w / (name + '-settings.txt')).write_text(r.stdout)
    (w / (name + '-settings-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    settings[name] = r.stdout
assert settings['reference'] == settings['native'], settings


class PAC(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        scripts = {
            '/proxy.pac': b'function FindProxyForURL(url, host) { return "PROXY 127.0.0.1:17777"; }',
            '/direct.pac': b'function FindProxyForURL(url, host) { return "DIRECT"; }',
            '/invalid.pac': b'not valid javascript {{{',
        }
        path = self.path.split('?')[0]
        body = scripts.get(path, b'not found')
        self.send_response(200 if path in scripts else 404)
        self.send_header('Content-Type', 'application/x-ns-proxy-autoconfig')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), PAC)
threading.Thread(target=server.serve_forever, daemon=True).start()
live = []
try:
    for path, fallback in itertools.product(
        ['/proxy.pac', '/direct.pac', '/invalid.pac', '/missing.pac'], [None, 'manual:8080']
    ):
        name = path[1:].replace('.', '-') + ('-manual' if fallback else '-none')
        result = {}
        url = 'http://127.0.0.1:' + str(server.server_port) + path
        args = ['--live', url, enc(fallback), enc('*.example'), 'http://destination.example/path']
        for label, cmd in [
            ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
            ('native', [str(Path(a.native).resolve())]),
        ]:
            r = subprocess.run(
                cmd + [args[0], args[1] + '?fixture=' + name + '-' + label] + args[2:],
                capture_output=True,
                text=True,
                timeout=45,
            )
            (w / (name + '-' + label + '.txt')).write_text(r.stdout)
            (w / (name + '-' + label + '-error.txt')).write_text(r.stderr)
            assert r.returncode == 0, (name, label, r.returncode, r.stderr)
            result[label] = r.stdout.strip()
        live.append(dict(case=name, **result))
        assert result['reference'] == result['native'], live[-1]
finally:
    server.shutdown()
    server.server_close()
proxy_live = next(row for row in live if row['case'] == 'proxy-pac-none')['reference']
live_verified = proxy_live.startswith('1:3:') and '127.0.0.1:17777' in bytes.fromhex(
    proxy_live.split(':')[2]
).decode('utf-16-be')
assert live_verified, proxy_live
(w / 'live-pac-results.json').write_text(
    json.dumps(dict(cases=live, livePACExecutionVerified=live_verified), indent=2)
)
report = dict(
    cases=len(rows),
    differences=len(diff),
    examples=diff[:3],
    nativeSettingsAndSessionMatch=True,
    livePACCases=len(live),
    livePACExecutionVerified=live_verified,
    applicationParityVerified=False,
    scope='Actual Windows WinInetProxyHelper against test-process WinHTTP stub: flags, nullable strings, ws/wss URL rewrite, one authentication retry, auto-detection failure, manual fallback. Real read-only current-user settings and session creation match source. Local PAC valid/direct/invalid/missing script calls match native WinHTTP output; live success recorded separately. No changes to system proxy settings or full application claim.',
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
print(json.dumps(dict(cases=len(rows), differences=len(diff), examples=diff[:1])))
raise SystemExit(bool(diff))
