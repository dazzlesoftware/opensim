import argparse, base64, hashlib, http.server, json, os, re, socket, subprocess, threading, time, uuid, urllib.parse, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(exist_ok=True, parents=True)
tests = Path(__file__).resolve().parent


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, **kwargs)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def build(name, source):
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        RuntimeFrameworkVersion='8.0.31',
        RollForward='Disable',
        EnableDefaultCompileItems='false',
        BaseIntermediateOutputPath=str(w / (name + '-obj')) + '/',
    ).items():
        ET.SubElement(props, key).text = value
    ET.SubElement(ET.SubElement(project, 'ItemGroup'), 'Compile', Include=str(tests / source))
    ET.ElementTree(project).write(w / (name + '.csproj'))
    run([a.dotnet, 'build', str(w / (name + '.csproj')), '-o', str(w / name), '-v:q'])
    return [a.dotnet, str(w / name / (name + '.dll'))]


prepare = build('Prepare', 'config_http_linux_aia_chain_reference.cs')
reference = build('Reference', 'config_http_linux_chain_download_reference.cs')
routes = {}
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def log_message(self, *args):
        pass

    def do_GET(self):
        requests.append(self.path)
        reply = routes[self.path]
        time.sleep(reply['delay'] / 1000)
        body = reply.get('body', bytes.fromhex(reply['der']) if reply['der'] else b'')
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
run([*prepare, '--prepare', str(w / 'fixtures.json'), str(server.server_port)])
rows = json.loads((w / 'fixtures.json').read_text())
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
cases = [
    dict(label=row['label'], index=i, mode=0, ticks=row['timeout'] * 10000, aux=False)
    for i, row in enumerate(rows)
]
single = next(i for i, row in enumerate(rows) if row['label'] == 'single')
two = next(i for i, row in enumerate(rows) if row['label'] == 'two-level')
for label, mode, ticks, aux, index in [
    ('policy-failure-still-cache', 1, 10000000, False, single),
    ('expired-no-cache', 2, 10000000, False, single),
    ('downloads-disabled', 3, 10000000, False, single),
    ('default-timeout', 0, 0, False, single),
    ('negative-timeout-clamp', 0, -1, False, single),
    ('large-timeout-clamp', 0, 9223372036854775807, False, single),
    ('aux-single', 0, 10000000, True, single),
    ('aux-two-level', 0, 10000000, True, two),
    ('aux-policy-failure-still-cache', 1, 10000000, True, single),
]:
    cases.append(dict(label=label, index=index, mode=mode, ticks=ticks, aux=aux))


def snapshot(home):
    ca = home / '.dotnet/corefx/cryptography/x509stores/ca'
    files = []
    if ca.exists():
        for file in sorted(ca.iterdir()):
            text = run(['openssl', 'pkcs12', '-in', str(file), '-passin', 'pass:', '-nokeys'])
            certs = re.findall(
                r'-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----', text, re.S
            )
            assert len(certs) == 1
            cert = base64.b64decode(certs[0])
            files.append(dict(name=file.name, mode=file.stat().st_mode & 0o7777, der=cert.hex()))
    return dict(directoryMode=ca.stat().st_mode & 0o7777 if ca.exists() else None, files=files)


observations = []
differences = []
scenario_root = w / 'scenarios' / uuid.uuid4().hex
try:
    for case in cases:
        row = rows[case['index']]
        routes.clear()
        for fixture in rows:
            for reply in fixture['replies']:
                prepared = dict(reply)
                if case['aux'] and reply['der']:
                    (w / 'issuer.der').write_bytes(bytes.fromhex(reply['der']))
                    prepared['body'] = run(
                        [
                            'openssl',
                            'x509',
                            '-inform',
                            'DER',
                            '-in',
                            str(w / 'issuer.der'),
                            '-trustout',
                            '-addtrust',
                            'clientAuth',
                            '-addreject',
                            'serverAuth',
                            '-setalias',
                            'Persisted AIA issuer',
                        ]
                    ).encode()
                routes[urllib.parse.urlsplit(reply['uri']).path] = prepared
        protocol = (
            ' '.join(
                [
                    row['leaf'],
                    str(len(row['roots'])),
                    *row['roots'],
                    str(len(row['extra']) + len(row['manual'])),
                    *row['extra'],
                    *row['manual'],
                ]
            )
            + '\n'
        )
        values = {}
        for kind, command in [
            (
                'source',
                [
                    *reference,
                    str(w / 'fixtures.json'),
                    str(case['index']),
                    str(case['mode']),
                    str(case['ticks']),
                ],
            ),
            ('native', [a.native, str(case['mode']), str(case['ticks'])]),
        ]:
            home = scenario_root / case['label'] / kind
            home.mkdir(parents=True)
            env = os.environ.copy()
            env.update(
                HOME=str(home), SSL_CERT_FILE='', SSL_CERT_DIR='', NO_PROXY='*', no_proxy='*'
            )
            for key in [
                'HTTP_PROXY',
                'HTTPS_PROXY',
                'ALL_PROXY',
                'http_proxy',
                'https_proxy',
                'all_proxy',
            ]:
                env.pop(key, None)
            start = len(requests)
            output = run(command, input=protocol, env=env).splitlines()
            assert len(output) == 2
            values[kind] = dict(outputs=output, requests=requests[start:], store=snapshot(home))
        item = dict(label=case['label'], **values)
        observations.append(item)
        if values['source'] != values['native']:
            differences.append(item)
        if case['label'] in [
            'single',
            'two-level',
            'policy-failure-still-cache',
            'aux-single',
            'aux-two-level',
            'aux-policy-failure-still-cache',
        ]:
            assert values['source']['store']['files'] and len(values['source']['requests']) == (
                2 if 'two-level' in case['label'] else 1
            ), item
        if case['label'] == 'policy-failure-still-cache':
            assert not values['source']['outputs'][0].startswith('- '), item
        if case['label'] in ['expired-no-cache', 'downloads-disabled']:
            assert not values['source']['store']['files'], item
finally:
    server.shutdown()
    server.server_close()
    thread.join()
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=not differences,
            cases=len(cases),
            buildsPerCase=2,
            differences=differences,
            observations=observations,
            scope='Actual public pinned Linux X509Chain.Build NoCheck versus complete native cached-store/AIA/import/save/Finish composition. Two immediate builds per case verify successful CA cache invalidation avoids repeat HTTP. Exact chain DER/status order, complete HTTP request order, saved PFX cert DER/filenames/modes. Includes save before final certificate-policy failure, no save for native expired failure, downloads disabled, timeout normalization and trusted PEM auxiliary metadata. Isolated HOME; live TLS and revocation not claimed.',
        ),
        indent=2,
    )
    + '\n'
)
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(cases)} complete chain-download/cache scenarios, two builds each')
