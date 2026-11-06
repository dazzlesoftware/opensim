import argparse, json, subprocess, threading, time, http.server, urllib.parse, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
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
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, k).text = v
ET.SubElement(
    ET.SubElement(project, 'ItemGroup'),
    'Compile',
    Include=str(ROOT / 'native-port/tests/config_http_pool_reference.cs'),
)
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, state=None):
        self.state = state or dict(lock=threading.Lock(), next_id=0, records=[])
        self.lock = self.state['lock']
        self.records = self.state['records']
        super().__init__(('127.0.0.1', 0), Handler)

    @property
    def next_id(self):
        return self.state['next_id']


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        with self.server.lock:
            self.server.state['next_id'] += 1
            self.cid = self.server.next_id

    def log_message(self, *args):
        pass

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError):
            self.close_connection = True

    def do_GET(self):
        self.route()

    def do_HEAD(self):
        self.route()

    def route(self):
        with self.server.lock:
            self.server.records.append(
                dict(
                    connection=self.cid,
                    method=self.command,
                    target=self.path,
                    headers={
                        k.lower(): (v.rsplit(':', 1)[0] + ':<port>' if k.lower() == 'host' else v)
                        for k, v in self.headers.items()
                    },
                )
            )
        body = b'abcdef'
        status = 200
        headers = {}
        path = self.path
        if path.startswith('/keepalive/'):
            headers['Keep-Alive'] = urllib.parse.unquote(path[len('/keepalive/') :])
        if path.endswith('large-ok'):
            body = b'a' * (1048576 - 8192)
        if path.endswith('large-limit'):
            body = b'a' * 1048576
        if path.endswith('large-over'):
            body = b'a' * (1048576 + 8192)
        if path == '/zero':
            body = b''
        if path.startswith('/redirect') or path == '/drain-slow-redirect':
            status = 302
            headers['Location'] = '/ok'
            body = b'' if path == '/redirect' else b'ignored'
        if path == '/slow-header':
            time.sleep(0.3)
        if path == '/close':
            headers['Connection'] = 'close'
            self.close_connection = True
        if path == '/silent-close':
            self.close_connection = True
        if path in ['/eof', '/truncated', '/http10']:
            headers['Connection'] = 'close'
            self.close_connection = True
        if path in ['/http10', '/keepalive10']:
            self.protocol_version = 'HTTP/1.0'
            if path == '/keepalive10':
                headers['Connection'] = 'keep-alive'
        if path.startswith('/chunked'):
            headers['Transfer-Encoding'] = 'chunked'
        elif path != '/eof':
            headers['Content-Length'] = str(
                8 if path == '/truncated' else 3 if path == '/extra' else len(body)
            )
        try:
            self.send_response(status)
            self.send_header('X-Connection-Id', str(self.cid))
            for k, v in headers.items():
                self.send_header(k, v)
            self.end_headers()
            if self.command == 'HEAD':
                return
            if path.startswith('/chunked'):
                for i in range(0, len(body), 32768):
                    chunk = body[i : i + 32768]
                    self.wfile.write(('%x\r\n' % len(chunk)).encode() + chunk + b'\r\n')
                self.wfile.write(b'0\r\nTrailer: value\r\n\r\n')
            elif path == '/truncated':
                self.wfile.write(b'ab')
            elif path == '/extra':
                self.wfile.write(b'abcUNEXPECTED')
            elif path in ['/slow-body', '/drain-timeout', '/drain-slow-redirect']:
                self.wfile.write(body[:2])
                self.wfile.flush()
                time.sleep(3 if path == '/drain-timeout' else 0.3)
                self.wfile.write(body[2:])
            else:
                self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass


def op(id, path='/ok', timeout=2000, method='GET', connection='k'):
    return f'O {id} {path} {method} {timeout} {connection}'


def read(id, count=-1):
    return f'R {id} {count}'


cases = []


def add(name, commands, limit=2, idle=31000, lifetime=180000):
    cases.append(dict(name=name, commands=commands, limit=limit, idle=idle, lifetime=lifetime))


add('completed-before-dispose', [op('a'), read('a'), op('b'), read('b'), 'C a', op('c'), read('c')])
add('disposed-complete-stream', [op('a'), read('a'), 'D a', op('b'), read('a'), read('b')])
add(
    'disposed-drained-stream',
    [op('a'), read('a', 1), 'D a', 'S 500', read('a'), op('b'), read('b')],
)
add(
    'empty-and-head',
    [op('a', '/zero'), op('b', method='HEAD'), op('c'), read('c'), read('a'), read('b')],
    limit=1,
)
add('chunked-trailers', [op('a', '/chunked'), read('a'), op('b'), read('b')])
add('request-close', [op('a', connection='c'), read('a'), op('b'), read('b')])
for path in ['/close', '/silent-close', '/eof', '/http10', '/keepalive10', '/truncated', '/extra']:
    add(path, [op('a', path), read('a'), 'S 100', op('b'), read('b')])
add('lifo', [op('a'), op('b'), read('a'), read('b'), op('c'), read('c'), op('d'), read('d')])
add('queue-timeout', [op('a'), op('b', timeout=100), read('a'), op('c'), read('c')], limit=1)
add('queue-release', [op('a'), 'A b /ok GET 2000 k', 'S 100', read('a'), 'J b', read('b')], limit=1)
add(
    'queue-canceled-head',
    [
        op('a'),
        'A b /ok GET 80 k',
        'S 30',
        'A c /ok GET 2000 k',
        'S 200',
        'J b',
        read('a'),
        'J c',
        read('c'),
    ],
    limit=1,
)
add('idle-scavenge', [op('a'), read('a'), 'S 1250', op('b'), read('b')], idle=50)
add('lifetime-get', [op('a'), read('a'), 'S 200', op('b'), read('b')], lifetime=100)
add('lifetime-return', [op('a', '/slow-header'), read('a'), op('b'), read('b')], lifetime=100)
add('infinite', [op('a'), read('a'), 'S 200', op('b'), read('b')], idle=-1, lifetime=-1)
add('zero-idle', [op('a'), read('a'), op('b'), read('b')], idle=0)
add('zero-idle-unlimited', [op('a'), read('a'), op('b'), read('b')], idle=0, limit=2147483647)
add('zero-lifetime', [op('a'), read('a'), op('b'), read('b')], lifetime=0)
add('redirect-empty', [op('a', '/redirect'), read('a'), op('b'), read('b')], limit=1)
add('redirect-body', [op('a', '/redirect-body'), read('a'), 'S 100', op('b'), read('b')], limit=1)
add('partial-dispose', [op('a'), read('a', 2), 'C a', 'S 100', op('b'), read('b')], limit=1)
add(
    'partial-dispose-slow',
    [op('a', '/slow-body'), read('a', 1), 'C a', 'S 500', op('b'), read('b')],
    limit=1,
)
add(
    'async-drain-lifo',
    [op('a', '/slow-body'), read('a', 1), 'C a', op('b'), read('b'), 'S 500', op('c'), read('c')],
)
add(
    'redirect-slow-drain', [op('a', '/drain-slow-redirect'), read('a'), op('b'), read('b')], limit=1
)
add(
    'drain-deadline',
    [op('a', '/drain-timeout'), read('a', 1), 'C a', op('b', timeout=4000), read('b')],
    limit=1,
)
for path in [
    '/large-ok',
    '/large-limit',
    '/large-over',
    '/chunked-large-ok',
    '/chunked-large-over',
]:
    add('drain-size' + path, [op('a', path), 'C a', op('b'), read('b')], limit=1)
for value in [
    'timeout=0',
    'timeout=1',
    'timeout=2',
    'timeout=2147485',
    'timeout=2147483647',
    'max=0',
    'max=00',
    'timeout="0"',
    'timeout=1,',
    'timeout=20, max=0',
]:
    add(
        'keepalive-' + value,
        [
            op('a', '/keepalive/' + urllib.parse.quote(value, safe='')),
            read('a'),
            op('b'),
            read('b'),
        ],
    )
add(
    'keepalive-expiry-persists',
    [op('a', '/keepalive/timeout=2'), read('a'), op('b'), read('b'), 'S 1100', op('c'), read('c')],
)
add(
    'keepalive-overwrite',
    [
        op('a', '/keepalive/timeout=2'),
        read('a'),
        op('b', '/keepalive/timeout=20'),
        read('b'),
        'S 1100',
        op('c'),
        read('c'),
    ],
)
add(
    'repeated-disposal',
    [
        command
        for i in range(40)
        for command in [op('r' + str(i)), read('r' + str(i)), 'C r' + str(i)]
    ],
    limit=1,
)
add(
    'server-limit32',
    [op('r' + str(i)) for i in range(32)]
    + [op('overflow', timeout=100)]
    + [read('r' + str(i)) for i in range(32)]
    + [op('last'), read('last')],
    limit=32,
)
# Windows may spend more than two seconds rejecting localhost's IPv6 address
# before falling back to this IPv4-only peer. This case checks pool keys, not
# DNS/connect timeout behavior (the queue timeout cases above remain strict).
add(
    'hostname-isolation',
    [op('a'), read('a'), op('b', '{localhost}/ok', timeout=10000), read('b'), op('c'), read('c')],
)
add('port-isolation', [op('a'), read('a'), op('b', '{other}/ok'), read('b'), op('c'), read('c')])
outputs = []
for case in cases:
    results = {}
    for label, cmd in [
        ('source', [a.dotnet, str(w / 'app/Reference.dll')]),
        ('native', [a.native]),
    ]:
        server = Server()
        servers = [server]
        workers = []
        if any('{other}' in c for c in case['commands']):
            servers.append(Server(server.state))
        for peer in servers:
            worker = threading.Thread(target=peer.serve_forever, daemon=True)
            worker.start()
            workers.append(worker)
        try:
            commands = [
                c.replace('{localhost}', f'http://localhost:{server.server_port}').replace(
                    '{other}', f'http://127.0.0.1:{servers[-1].server_port}'
                )
                for c in case['commands']
            ]
            r = subprocess.run(
                cmd
                + [
                    f'http://127.0.0.1:{server.server_port}',
                    str(case['limit']),
                    str(case['idle']),
                    str(case['lifetime']),
                ],
                input='\n'.join(commands) + '\n',
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=40,
            )
            assert r.returncode == 0, (case['name'], label, r.returncode, r.stdout, r.stderr)
            with server.lock:
                results[label] = dict(
                    output=r.stdout.splitlines(),
                    requests=list(server.records),
                    accepted=server.next_id,
                )
        finally:
            for peer in servers:
                peer.shutdown()
                peer.server_close()
            for worker in workers:
                worker.join()
    outputs.append(dict(**case, **results))
differences = [x for x in outputs if x['source'] != x['native']]
report = dict(
    passed=not differences,
    scenarios=len(cases),
    sourceRequests=sum(len(x['source']['requests']) for x in outputs),
    nativeRequests=sum(len(x['native']['requests']) for x in outputs),
    differences=differences,
    cases=outputs,
    scope='Actual .NET8.0.31 SocketsHttpHandler direct HTTP/1 pooling and native handler socket ownership, body disposal, request queue and expiration; controlled local HTTP peer with physical connection identities and parsed HTTP requests preserving all header values except ephemeral Host ports. TLS/proxy/authentication/full handler configuration/macOS not claimed.',
    applicationParityVerified=False,
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['passed'], f'{len(differences)} pool differences; see {w}/results.json'
print(f"PASS: {len(cases)} direct HTTP pool scenarios, {report['nativeRequests']} exact requests")
