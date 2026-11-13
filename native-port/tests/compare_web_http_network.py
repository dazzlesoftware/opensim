import os, argparse, json, subprocess, threading, time, http.server, ssl, socket, gzip, hashlib, re, sys, select, tempfile, xml.etree.ElementTree as ET
from urllib.parse import urlsplit
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
p.add_argument('--tls', action='store_true')
p.add_argument('--tunnel', action='store_true')
p.add_argument('--sni', action='store_true')
p.add_argument('--platform-trust', action='store_true')
p.add_argument('--linux-aia', action='store_true')
p.add_argument('--linux-platform-trust', action='store_true')
p.add_argument('--filter', default='')
a = p.parse_args()
a.linux_platform_trust = a.linux_platform_trust or a.linux_aia
a.platform_trust = a.platform_trust or a.linux_platform_trust
a.tls = a.tls or a.tunnel or a.sni or a.platform_trust
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'OpenSim/Framework/WebUtil.cs').read_bytes().decode('utf-8-sig')


def extract(signature):
    at = source.index(signature)
    start = source.index('{', at)
    i = start + 1
    depth = 1
    while depth:
        if source[i] == '{':
            depth += 1
        if source[i] == '}':
            depth -= 1
        i += 1
    return source[at:i]


methods = ''.join(
    extract(sig)
    for sig in [
        'public static void SetupHTTPClients(',
        'public static HttpClient GetNewGlobalHttpClient(',
        'public static HttpClient GetGlobalNoRedirHttpClient(',
    ]
)
(w / 'Extracted.cs').write_text(
    'using System;using System.Net;using System.Net.Http;using System.Net.Security;using System.Security.Authentication;using System.Security.Cryptography.X509Certificates;static partial class WebUtil{'
    + methods
    + '}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
    NoWarn='SYSLIB0039',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [ROOT / 'native-port/tests/web_http_network_reference.cs', w / 'Extracted.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
reference = [a.dotnet, str(w / 'app/Reference.dll')]
certs = w / 'certificates'
issuer_requests = []
issuer_server = None
if a.linux_aia:

    class IssuerHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            issuer_requests.append(self.path)
            body = (certs / 'issuer.der').read_bytes()
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    issuer_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), IssuerHandler)
    issuer_worker = threading.Thread(target=issuer_server.serve_forever, daemon=True)
    issuer_worker.start()
if a.tls:
    cert_args = (
        [f'http://127.0.0.1:{issuer_server.server_port}/issuer.der'] if issuer_server else []
    )
    r = subprocess.run(
        reference + ['--make-certs', str(certs), *cert_args], capture_output=True, text=True
    )
    assert r.returncode == 0, r.stdout + r.stderr


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, tls=None, dual=False):
        self.lock = threading.Lock()
        self.next_id = 0
        self.records = []
        self.tls_alerts = []
        self.sni = []
        self.dual = dual
        self.tls_context = None
        super().__init__(('127.0.0.1', 0), Handler)
        if tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = context.maximum_version = tls[1]
            context.load_cert_chain(
                str(certs / (tls[0] + '.pem')), str(certs / (tls[0] + '.key.pem'))
            )

            def observe_hello(sock, direction, version, content_type, message_type, data):
                if direction != 'read' or int(content_type) != 22 or int(message_type) != 1:
                    return
                # The string SNI callback decodes names as ASCII and rejects raw UTF8
                # ClientHellos. Observe the message bytes without affecting negotiation.
                pos = 4 + 2 + 32
                pos += 1 + data[pos]
                length = int.from_bytes(data[pos : pos + 2], 'big')
                pos += 2 + length
                pos += 1 + data[pos]
                names = []
                if pos + 2 <= len(data):
                    stop = pos + 2 + int.from_bytes(data[pos : pos + 2], 'big')
                    pos += 2
                    while pos + 4 <= stop:
                        kind = int.from_bytes(data[pos : pos + 2], 'big')
                        size = int.from_bytes(data[pos + 2 : pos + 4], 'big')
                        payload = data[pos + 4 : pos + 4 + size]
                        pos += 4 + size
                        if kind == 0:
                            index = 2
                            while index + 3 <= len(payload):
                                name_type = payload[index]
                                name_size = int.from_bytes(payload[index + 1 : index + 3], 'big')
                                index += 3
                                if name_type == 0:
                                    names.append(payload[index : index + name_size].hex())
                                index += name_size
                with self.lock:
                    self.sni.append(names[0] if names else None)

            context._msg_callback = observe_hello
            self.tls_context = context
            if not dual:
                self.socket = context.wrap_socket(self.socket, server_side=True)

    def get_request(self):
        sock, address = super().get_request()
        if self.dual and sock.recv(1, socket.MSG_PEEK) == b'\x16':
            sock = self.tls_context.wrap_socket(sock, server_side=True)
        return sock, address


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        with self.server.lock:
            self.server.next_id += 1
            self.cid = self.server.next_id

    def log_message(self, *args):
        pass

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError):
            self.close_connection = True
        except ssl.SSLError as error:
            with self.server.lock:
                self.server.tls_alerts.append(str(error))
            self.close_connection = True

    def do_GET(self):
        self.route()

    def do_HEAD(self):
        self.route()

    def do_POST(self):
        self.route()

    def do_PUT(self):
        self.route()

    def do_DELETE(self):
        self.route()

    def do_OPTIONS(self):
        self.route()

    def do_PATCH(self):
        self.route()

    def do_QUERYACCESS(self):
        self.route()

    def normalize_host(self, value):
        for port in [self.server.server_port, getattr(self.server, 'tunnel_port', -1)]:
            suffix = ':' + str(port)
            if value.endswith(suffix):
                return value[: -len(suffix)] + ':<port>'
        return value

    def do_CONNECT(self):
        with self.server.lock:
            self.server.records.append(
                dict(
                    connection=self.cid,
                    method=self.command,
                    target=self.path.rsplit(':', 1)[0] + ':<port>',
                    headers={
                        k.lower(): (self.normalize_host(v) if k.lower() == 'host' else v)
                        for k, v in self.headers.items()
                    },
                )
            )
        self.close_connection = True
        if self.server.tunnel_status != 200:
            self.wfile.write(
                f'HTTP/1.1 {self.server.tunnel_status} Fixture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'.encode()
            )
            self.wfile.flush()
            return
        with socket.create_connection(('127.0.0.1', self.server.tunnel_port), timeout=5) as peer:
            self.wfile.write(b'HTTP/1.1 200 Connection Established\r\n\r\n')
            self.wfile.flush()
            while True:
                ready = [self.connection] if (
                    isinstance(self.connection, ssl.SSLSocket) and self.connection.pending()
                ) else []
                if not ready:
                    ready, _, _ = select.select([self.connection, peer], [], [], 10)
                if not ready:
                    return
                for current in ready:
                    try:
                        data = current.recv(65536)
                    except (OSError, ConnectionResetError):
                        return
                    if not data:
                        return
                    try:
                        (peer if current is self.connection else self.connection).sendall(data)
                    except OSError:
                        return

    def route(self):
        data = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        with self.server.lock:
            self.server.records.append(
                dict(
                    connection=self.cid,
                    method=self.command,
                    target=self.path,
                    body=data.hex(),
                    tls=(
                        self.connection.version()
                        if isinstance(self.connection, ssl.SSLSocket)
                        else None
                    ),
                    headers={
                        k.lower(): (self.normalize_host(v) if k.lower() == 'host' else v)
                        for k, v in self.headers.items()
                    },
                    hosts=[self.normalize_host(v) for v in self.headers.get_all('Host', [])],
                )
            )
        body = b'abcdef'
        status = 200
        headers = {}
        path = urlsplit(self.path).path
        if path.startswith('/redirect/'):
            status = int(path.rsplit('/', 1)[1])
            headers['Location'] = '/ok'
            body = b'ignored'
        if path == '/bad-location':
            status = 302
            headers['Location'] = 'http://[invalid'
        if path == '/loop':
            status = 302
            headers['Location'] = '/loop'
            body = b''
        if path == '/set-cookie':
            status = 302
            headers['Location'] = '/cookie'
            headers['Set-Cookie'] = 'secret=must-not-send; Path=/'
            body = b''
        if path == '/cookie':
            body = b'cookie:' + self.headers.get('Cookie', '').encode()
        if path == '/gzip':
            body = gzip.compress(body, mtime=0)
            headers['Content-Encoding'] = 'gzip'
        if path == '/extra-encrypted':
            body = b'abcUNEXPECTED'
        if path == '/close':
            headers['Connection'] = 'close'
            self.close_connection = True
        if path == '/silent-close':
            self.close_connection = True
        if path == '/headers-slow':
            time.sleep(0.4)
        if path.startswith('/status/'):
            status = int(path.rsplit('/', 1)[1])
        try:
            self.send_response(status)
            self.send_header('X-Connection-Id', str(self.cid))
            self.send_header('Content-Length', str(3 if path == '/extra-encrypted' else len(body)))
            for k, v in headers.items():
                self.send_header(k, v)
            # Cached-entity cases send headers and their small body in one write. A
            # split write would turn connection identity into an arrival-time race.
            self._headers_buffer.append(b'\r\n')
            head = b''.join(self._headers_buffer)
            self._headers_buffer = []
            if self.command == 'HEAD':
                self.wfile.write(head)
                return
            if path == '/extra-encrypted':
                self.wfile.write(head)
                self.wfile.flush()
                self.wfile.write(body)
            elif path == '/body-slow':
                self.wfile.write(head + body[:2])
                self.wfile.flush()
                time.sleep(0.3)
                self.wfile.write(body[2:])
            else:
                self.wfile.write(head + body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass


def setup(flags=0, limit=2):
    return f'S {flags} {limit}'


def client(id, mode=0, timeout=2000, buffer=262144000):
    return f'G {id} {mode} {timeout} {buffer}'


def op(id, c='c', path='/ok', method='GET'):
    return f'O {id} {c} {path} {method}'


def read(id, n=-1):
    return f'R {id} {n}'


cases = []


def add(name, commands, tls=None, expected_success=None, dual=False):
    cases.append(
        dict(name=name, commands=commands, tls=tls, expected_success=expected_success, dual=dual)
    )


if a.linux_aia:
    add(
        'linux-live-aia',
        [setup(), client('c'), op('a', path='/close'), read('a'), op('b'), read('b')],
        ('aia', ssl.TLSVersion.TLSv1_2),
        True,
    )
    cases[-1]['trusted'] = True
elif a.platform_trust:
    for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
        for tunneled in [False, True]:
            for certificate in ['valid', 'wrong-name', 'expired']:
                for flags in range(4):
                    success = bool(flags & 1) and (certificate != 'wrong-name' or bool(flags & 2))
                    commands = (
                        (['P {proxy}'] if tunneled else [])
                        + [setup(flags), client('c'), op('a')]
                        + ([read('a'), op('b'), read('b')] if success else [])
                    )
                    add(
                        f'platform-{version.name}-{tunneled}-{certificate}-{flags}',
                        commands,
                        (certificate, version),
                        success,
                    )
                    cases[-1]['tunnel'] = tunneled
                    if a.linux_platform_trust:
                        trusted = (certificate != 'expired' or bool(flags & 1)) and (
                            certificate != 'wrong-name' or bool(flags & 2)
                        )
                        commands = (
                            (['P {proxy}'] if tunneled else [])
                            + [setup(flags), client('c'), op('a')]
                            + ([read('a'), op('b'), read('b')] if trusted else [])
                        )
                        add(
                            f'platform-trusted-{version.name}-{tunneled}-{certificate}-{flags}',
                            commands,
                            (certificate, version),
                            trusted,
                        )
                        cases[-1].update(tunnel=tunneled, trusted=True)
elif not a.tls:
    add(
        'shared-wrappers',
        [
            setup(),
            client('a'),
            client('b'),
            op('r1', 'a'),
            read('r1'),
            'D a',
            op('r2', 'b'),
            read('r2'),
            client('c'),
            op('r3'),
            read('r3'),
        ],
    )
    add(
        'independent-redirect-pools',
        [
            setup(),
            client('r'),
            client('n', 1),
            op('a', 'r'),
            read('a'),
            op('b', 'n'),
            read('b'),
            op('c', 'r'),
            read('c'),
            op('d', 'n'),
            read('d'),
        ],
    )
    add(
        'old-handler-survives-setup',
        [
            setup(),
            client('old'),
            op('a', 'old'),
            read('a'),
            setup(3, 1),
            client('new'),
            op('b', 'new'),
            read('b'),
            op('c', 'old'),
            read('c'),
            op('d', 'new'),
            read('d'),
        ],
    )
    for mode in [0, 1]:
        for status in [301, 302, 303, 307, 308]:
            add(
                f'redirect-{mode}-{status}',
                [
                    setup(),
                    client('c', mode),
                    op('a', path=f'/redirect/{status}'),
                    read('a'),
                    op('b'),
                    read('b'),
                ],
            )
        for path in ['/loop', '/bad-location', '/set-cookie', '/gzip']:
            add(f'{mode}-{path}', [setup(), client('c', mode), op('a', path=path), read('a')])
    for method in ['GET', 'HEAD', 'POST', 'PUT', 'DELETE', 'OPTIONS', 'PATCH', 'QUERYACCESS']:
        add('method-' + method, [setup(), client('c'), op('a', method=method), read('a')])
    add(
        'shared-capacity-timeout',
        [
            setup(limit=1),
            client('a'),
            client('b', timeout=100),
            op('r1', 'a'),
            op('r2', 'b'),
            read('r1'),
            client('c'),
            op('r3'),
            read('r3'),
        ],
    )
    add('header-timeout', [setup(), client('c', timeout=100), op('a', path='/headers-slow')])
    add('disposed-wrapper', [setup(), client('c'), 'D c', op('a')])
    add(
        'dispose-after-headers',
        [setup(), client('c'), op('a', path='/body-slow'), 'D c', read('a')],
    )
    add(
        'cancel-pending-headers',
        [setup(), client('c'), 'A a c /headers-slow GET', 'W 100', 'D c', 'J a'],
    )
    add(
        'cancel-pending-queue',
        [
            setup(limit=1),
            client('a'),
            client('b'),
            op('r1', 'a'),
            'A r2 b /ok GET',
            'W 100',
            'D b',
            'J r2',
            read('r1'),
            client('c'),
            op('r3'),
            read('r3'),
        ],
    )
    add('headers-only-buffer-limit', [setup(), client('c', buffer=1), op('a'), read('a')])
    for status in [400, 404, 500]:
        add(
            f'non-success-status-{status}',
            [setup(), client('c'), op('a', path=f'/status/{status}'), read('a')],
        )
    proxied = ['P {plain}', setup(limit=1), client('c')]
    add(
        'proxy-cross-origin-reuse',
        proxied
        + [
            op('a', path='http://first.invalid/ok'),
            read('a'),
            op('b', path='http://second.invalid:81/ok'),
            read('b'),
            op('d', path='http://first.invalid/ok'),
            read('d'),
        ],
    )
    add(
        'proxy-cross-origin-capacity',
        proxied
        + [
            client('b', timeout=100),
            op('a', path='http://first.invalid/ok'),
            op('b', 'b', path='http://second.invalid/ok'),
            read('a'),
            op('d', path='http://second.invalid/ok'),
            read('d'),
        ],
    )
    add(
        'proxy-cross-origin-cancel',
        proxied
        + [
            client('b'),
            op('a', path='http://first.invalid/ok'),
            'A b b http://second.invalid/ok GET',
            'W 100',
            'D b',
            'J b',
            read('a'),
            op('d', path='http://second.invalid/ok'),
            read('d'),
        ],
    )
    add(
        'proxy-partial-drain',
        proxied
        + [
            op('a', path='http://first.invalid/body-slow'),
            read('a', 1),
            'C a',
            op('b', path='http://second.invalid/ok'),
            read('b'),
        ],
    )
    for path in ['/close', '/silent-close']:
        add(
            'proxy-' + path,
            proxied
            + [
                op('a', path='http://first.invalid' + path),
                read('a'),
                'W 100',
                op('b', path='http://second.invalid/ok'),
                read('b'),
            ],
        )
    # Mutable IWebProxy preserves one handler while varying the Uri.Equals key.
    for suffix in ['/#fragment', '/path', '/?query=1', '/p%61th', '/path?x=%41', '/path?x=A']:
        add(
            'proxy-identity-' + suffix,
            proxied
            + [
                op('a', path='http://first.invalid/ok'),
                read('a'),
                'P {plain}' + suffix,
                op('b', path='http://second.invalid/ok'),
                read('b'),
                'P {plain}',
                op('d', path='http://first.invalid/ok'),
                read('d'),
            ],
        )
    add(
        'proxy-equivalent-path-escape',
        [
            'P {plain}/p%61th',
            setup(),
            client('c'),
            op('a', path='http://first.invalid/ok'),
            read('a'),
            'P {plain}/path',
            op('b', path='http://second.invalid/ok'),
            read('b'),
        ],
    )
elif a.sni:

    def host_op(id, host, path='/ok'):
        return op(id, path=path) + ' - ' + (host.encode('utf-16-be').hex() or '_')

    for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
        for tunneled in [False, True]:
            prefix = ['P {proxy}'] if tunneled else []

            def sni_add(name, commands, certificate='dns', success=True):
                add(
                    f'SNI-{version.name}-{tunneled}-{name}',
                    prefix + commands,
                    (certificate, version),
                    success,
                )
                cases[-1]['tunnel'] = tunneled

            for host, certificate, flags, success in [
                ('alt.fixture.test', 'dns', 0, True),
                ('ALT.FIXTURE.TEST', 'dns', 0, True),
                ('alt.fixture.test.', 'dns', 0, True),
                ('alt.fixture.test..', 'dns', 0, False),
                ('alt.fixture.test:443', 'dns', 0, True),
                ('mixed.fixture.test', 'dns', 0, True),
                (' alt.fixture.test ', 'dns', 0, True),
                ('xn--bcher-kva.fixture.test', 'dns', 0, True),
                ('bad.fixture.test', 'dns', 0, False),
                ('bad.fixture.test', 'dns', 1, False),
                ('bad.fixture.test', 'dns', 2, True),
                ('b\u00fccher.fixture.test', 'dns', 0, False),
                ('127.0.0.1', 'valid', 0, True),
                ('127.0.0.1.', 'valid', 0, True),
                ('127.0.0.1:0', 'valid', 0, True),
                ('[::1]', 'valid', 2, True),
                ('[::1]:443', 'valid', 2, True),
                ('[fe80::1%123]:443', 'valid', 2, True),
                ('', 'valid', 0, True),
                ('bad/name', 'valid', 0, True),
                ('.', 'valid', 2, True),
            ]:
                commands = [setup(flags), client('c'), host_op('a', host)] + (
                    [read('a')] if success else []
                )
                sni_add(repr(host) + '-' + str(flags), commands, certificate, success)
            if sys.platform.startswith('linux'):
                for host in ['-a.fixture.test', 'a-.fixture.test']:
                    for flags in range(4):
                        sni_add(
                            'strict-idn-' + host + '-' + str(flags),
                            [setup(flags), client('c'), host_op('a', host)],
                            success=False,
                        )
                for host, certificate, success in [
                    ('127.0.0.1', 'cn-ip-dns', True),
                    ('127.1', 'cn-noncanonical', True),
                    ('abc.fixture.test', 'partial-wild', False),
                ]:
                    sni_add(
                        'native-name-' + certificate,
                        [setup(), client('c'), host_op('a', host)]
                        + ([read('a')] if success else []),
                        certificate,
                        success,
                    )
            sni_add(
                'raw-pool-identity',
                [
                    setup(),
                    client('c'),
                    host_op('a', 'alt.fixture.test'),
                    read('a'),
                    host_op('b', 'ALT.FIXTURE.TEST'),
                    read('b'),
                    host_op('d', 'alt.fixture.test.'),
                    read('d'),
                    host_op('e', 'alt.fixture.test'),
                    read('e'),
                    host_op('f', 'ALT.FIXTURE.TEST'),
                    read('f'),
                ],
            )
            sni_add(
                'port-removed-key',
                [
                    setup(),
                    client('c'),
                    host_op('a', 'alt.fixture.test:443'),
                    read('a'),
                    host_op('b', 'alt.fixture.test:8443'),
                    read('b'),
                    host_op('d', 'alt.fixture.test'),
                    read('d'),
                ],
            )
elif a.tunnel:
    for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
        for certificate, flags in [
            ('valid', 0),
            ('wrong-name', 0),
            ('wrong-name', 2),
            ('untrusted', 0),
            ('untrusted', 1),
        ]:
            success = certificate == 'valid' or flags != 0
            commands = ['P {proxy}', setup(flags), client('c'), op('a')] + (
                [read('a'), op('b'), read('b')] if success else []
            )
            add(
                f'CONNECT-{version.name}-{certificate}-{flags}',
                commands,
                (certificate, version),
                success,
            )
        for status in [201, 301, 400, 403, 407, 500]:
            add(
                f'CONNECT-{version.name}-status-{status}',
                ['P {proxy}', setup(), client('c'), op('a')],
                ('valid', version),
                False,
            )
            cases[-1]['tunnel_status'] = status
        prefix = ['P {proxy}', setup(limit=1), client('c')]
        tls = ('valid', version)
        add(
            f'CONNECT-{version.name}-https-proxy-reuse',
            prefix + [op('a'), read('a'), op('b'), read('b')],
            tls,
            True,
        )
        cases[-1]['proxy_tls'] = tls
        add(
            f'CONNECT-{version.name}-https-proxy-rejection',
            prefix + [op('a')],
            tls,
            False,
        )
        cases[-1].update(proxy_tls=tls, tunnel_status=403)
        for invalid_peer in ('proxy', 'origin'):
            wrong_name = ('wrong-name', version)
            add(
                f'CONNECT-{version.name}-https-proxy-invalid-{invalid_peer}',
                prefix + [op('a')],
                wrong_name if invalid_peer == 'origin' else tls,
                False,
            )
            cases[-1]['proxy_tls'] = wrong_name if invalid_peer == 'proxy' else tls
        add(
            f'CONNECT-{version.name}-user-agent',
            prefix + [op('a') + ' Fixture-Agent/1.0', read('a'), op('b'), read('b')],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-retained-user-agent',
            prefix
            + [
                op('a', path='/close') + ' Fixture-Agent/1.0',
                read('a'),
                op('b'),
                read('b'),
                op('d', path='/close') + ' Replacement/2.0',
                read('d'),
                op('e'),
                read('e'),
            ],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-shared-capacity',
            prefix
            + [client('b', timeout=100), op('a'), op('b', 'b'), read('a'), op('d'), read('d')],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-cancel-queue',
            prefix
            + [
                client('b'),
                op('a'),
                'A b b /ok GET',
                'W 100',
                'D b',
                'J b',
                read('a'),
                op('d'),
                read('d'),
            ],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-partial-drain',
            prefix + [op('a', path='/body-slow'), read('a', 1), 'C a', op('b'), read('b')],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-authority-isolation',
            [
                'P {proxy}',
                setup(2),
                client('c'),
                op('a'),
                read('a'),
                op('b', path='{other}/ok'),
                read('b'),
                op('d'),
                read('d'),
                op('e', path='{other}/ok'),
                read('e'),
            ],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-proxy-identity',
            prefix
            + [
                op('a'),
                read('a'),
                'P {proxy}/path',
                op('b'),
                read('b'),
                'P {proxy}',
                op('d'),
                read('d'),
            ],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-handler-isolation',
            [
                'P {proxy}',
                setup(),
                client('c'),
                client('n', 1),
                op('a'),
                read('a'),
                op('b', 'n'),
                read('b'),
                op('d'),
                read('d'),
                op('e', 'n'),
                read('e'),
            ],
            tls,
            True,
        )
        add(
            f'CONNECT-{version.name}-plain-proxy-isolation',
            prefix
            + [
                op('a', path='http://first.invalid/ok'),
                read('a'),
                op('b'),
                read('b'),
                op('d', path='http://first.invalid/ok'),
                read('d'),
                op('e'),
                read('e'),
            ],
            tls,
            True,
        )
        for path in ['/close', '/silent-close', '/redirect/302']:
            add(
                f'CONNECT-{version.name}-{path}',
                prefix + [op('a', path=path), read('a'), op('b'), read('b')],
                tls,
                True,
            )
else:
    for version in [ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3]:
        for certificate in ['valid', 'wrong-name', 'untrusted', 'untrusted-wrong-name', 'expired']:
            for flags in range(4):
                expected = (
                    flags & 1
                    if certificate in ['untrusted', 'untrusted-wrong-name', 'expired']
                    else True
                )
                if 'wrong-name' in certificate:
                    expected = expected and flags & 2
                commands = [setup(flags), client('c'), op('a')] + (
                    [read('a'), op('b'), read('b')] if expected else []
                )
                add(
                    f'{version.name}-{certificate}-{flags}',
                    commands,
                    (certificate, version),
                    bool(expected),
                )
        tls = ('valid', version)
        add(
            f'{version.name}-https-forward-proxy-reuse',
            [
                'P {endpoint}',
                setup(limit=1),
                client('c'),
                op('a', path='http://first.invalid/ok'),
                read('a'),
                op('b', path='http://second.invalid:81/ok'),
                read('b'),
            ],
            tls,
            True,
        )
        add(
            f'{version.name}-shared-wrapper-pool',
            [
                setup(),
                client('a'),
                client('b'),
                op('r1', 'a'),
                read('r1'),
                'D a',
                op('r2', 'b'),
                read('r2'),
            ],
            tls,
            True,
        )
        add(
            f'{version.name}-shared-capacity',
            [
                setup(limit=1),
                client('a'),
                client('b', timeout=100),
                op('r1', 'a'),
                op('r2', 'b'),
                read('r1'),
                client('c'),
                op('r3'),
                read('r3'),
            ],
            tls,
            True,
        )
        add(
            f'{version.name}-cancel-queue',
            [
                setup(limit=1),
                client('a'),
                client('b'),
                op('r1', 'a'),
                'A r2 b /ok GET',
                'W 100',
                'D b',
                'J r2',
                read('r1'),
                client('c'),
                op('r3'),
                read('r3'),
            ],
            tls,
            True,
        )
        add(
            f'{version.name}-partial-drain',
            [
                setup(limit=1),
                client('c'),
                op('a', path='/body-slow'),
                read('a', 1),
                'C a',
                op('b'),
                read('b'),
            ],
            tls,
            True,
        )
        for path in ['/extra-encrypted', '/close', '/silent-close']:
            add(
                f'{version.name}-{path}',
                [setup(), client('c'), op('a', path=path), read('a'), 'W 100', op('b'), read('b')],
                tls,
                True,
            )
        add(
            f'{version.name}-scheme-isolation',
            [
                setup(),
                client('c'),
                op('a', path='{plain}/ok'),
                read('a'),
                op('b'),
                read('b'),
                op('d', path='{plain}/ok'),
                read('d'),
                op('e'),
                read('e'),
            ],
            tls,
            True,
            dual=True,
        )
outputs = []
cases = [case for case in cases if a.filter in case['name']]
assert cases, 'No scenarios selected'
for case in cases:
    results = {}
    for label, command in [('source', reference), ('native', [a.native])]:
        server = Server(case['tls'], case['dual'])
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        proxy = None
        proxy_worker = None
        if a.tunnel or case.get('tunnel', False):
            proxy = Server(case.get('proxy_tls'))
            proxy.tunnel_port = server.server_port
            proxy.tunnel_status = case.get('tunnel_status', 200)
            proxy_worker = threading.Thread(target=proxy.serve_forever, daemon=True)
            proxy_worker.start()
        try:
            endpoint = f'{"https" if a.tls else "http"}://127.0.0.1:{server.server_port}'
            proxy_endpoint = (
                f'{"https" if case.get("proxy_tls") else "http"}://127.0.0.1:{proxy.server_port}'
                if proxy else ''
            )
            root = str(certs / 'root.pem') if a.tls and not a.platform_trust else '-'
            commands = [
                c.replace('{plain}', f'http://127.0.0.1:{server.server_port}')
                .replace('{other}', f'https://localhost:{server.server_port}')
                .replace('{endpoint}', endpoint)
                .replace('{proxy}', proxy_endpoint)
                for c in case['commands']
            ]
            env = None
            if a.linux_platform_trust:
                isolated = w / 'isolated-home'
                if a.linux_aia:
                    isolated = Path(tempfile.mkdtemp(prefix=label + '-', dir=w))
                isolated.mkdir(exist_ok=True)
                env = dict(
                    os.environ,
                    HOME=str(isolated),
                    SSL_CERT_DIR='',
                    SSL_CERT_FILE=str(certs / 'root.pem') if case.get('trusted') else '',
                    NO_PROXY='*',
                    no_proxy='*',
                )
            issuer_start = len(issuer_requests)
            r = subprocess.run(
                command + [endpoint, root],
                env=env,
                input='\n'.join(commands) + '\n',
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=30,
            )
            if a.tls and label == 'source':
                expected = []
                for certificate, _ in (case['tls'], case.get('proxy_tls', case['tls'])):
                    pem = (certs / (certificate + '.pem')).read_text().split(
                        '-----END CERTIFICATE-----', 1
                    )[0] + '-----END CERTIFICATE-----\n'
                    fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()
                    if fingerprint not in expected:
                        expected.append(fingerprint)
                peers = re.findall(r'PEER_CERT:([A-Fa-f0-9]{64}):([^\r\n]+)', r.stderr)
                if peers and any(h.lower() not in expected for h, issuer in peers):
                    unavailable = dict(
                        passed=False,
                        status='unavailable',
                        environmentInterceptionDetected=True,
                        case=case['name'],
                        expectedPeerCertificateSHA256=expected[0],
                        expectedPeerCertificatesSHA256=expected,
                        observedPeerCertificates=peers,
                        sourceOutput=r.stdout,
                        sourceError=r.stderr,
                        applicationParityVerified=False,
                        scope='The reference received a certificate different from the isolated TLS peer; this cannot verify the controlled chain/hostname matrix.',
                    )
                    (w / 'environment-observation.json').write_text(
                        json.dumps(unavailable, indent=2) + '\n'
                    )
                    print(
                        'UNAVAILABLE: TLS peer certificate was replaced before reaching the reference; see '
                        + str(w / 'environment-observation.json')
                    )
                    sys.exit(77)
            assert r.returncode == 0, (case['name'], label, r.returncode, r.stdout, r.stderr)
            if a.tls:
                actual = any(line.startswith('O ') for line in r.stdout.splitlines())
                assert actual == case['expected_success'], (
                    case['name'],
                    label,
                    'incorrect certificate policy outcome',
                    r.stdout,
                    r.stderr,
                )
            with server.lock:
                results[label] = dict(output=r.stdout.splitlines(), requests=list(server.records))
            if a.linux_aia:
                saved = list((isolated / '.dotnet/corefx/cryptography/x509stores/ca').glob('*.pfx'))
                results[label]['issuerRequests'] = issuer_requests[issuer_start:]
                assert results[label]['issuerRequests'] == ['/issuer.der'] and len(saved) == 1, (
                    label,
                    results[label],
                    saved,
                )
                assert len({item['connection'] for item in results[label]['requests']}) == 2
                results[label]['savedIssuers'] = [path.name for path in saved]
            if a.sni:
                with server.lock:
                    results[label]['sni'] = list(server.sni)
            if proxy:
                # Error messages are emitted as UTF-8 hex by both real clients; normalize
                # only the generated proxy port before comparing their complete text.
                for index, line in enumerate(results[label]['output']):
                    if line.startswith('E '):
                        prefix, encoded = line.rsplit(' ', 1)
                        message = (
                            bytes.fromhex(encoded)
                            .decode('utf-8')
                            .replace(
                                f'{proxy_endpoint}/',
                                f'{urlsplit(proxy_endpoint).scheme}://127.0.0.1:<proxy>/',
                            )
                        )
                        results[label]['output'][index] = prefix + ' ' + message.encode().hex()
                with proxy.lock:
                    results[label]['proxyRequests'] = list(proxy.records)
            if not a.tls:
                results[label]['accepted'] = server.next_id
        finally:
            if proxy:
                proxy.shutdown()
                proxy.server_close()
                proxy_worker.join()
            server.shutdown()
            server.server_close()
            worker.join()
    outputs.append(dict(name=case['name'], commands=case['commands'], **results))
if issuer_server:
    issuer_server.shutdown()
    issuer_server.server_close()
    issuer_worker.join()
diffs = [x for x in outputs if x['source'] != x['native']]
report = dict(
    passed=not diffs,
    scenarios=len(cases),
    sourceRequests=sum(len(x['source']['requests']) for x in outputs),
    nativeRequests=sum(len(x['native']['requests']) for x in outputs),
    differences=diffs,
    cases=outputs,
    scope='Unchanged original global WebUtil factory, actual BCL clients and native shared factory/typed sockets. Explicit direct proxy boundary; TLS uses generated controlled custom roots and actual chain/name/protocol outcomes, not platform default trust, TLS wire/inner exception or general proxy/authentication parity.',
    applicationParityVerified=False,
)
report['scope'] = (
    'Unchanged original global WebUtil factory and actual BCL/native sockets. Selected HTTP proxy and HTTPS CONNECT cases compare complete request headers, connection IDs and exception messages. TLS uses isolated custom roots and observed peer fingerprints; platform default trust, general authenticated/SOCKS/TLS-proxy transports, inner exceptions and full simulator parity remain unverified.'
)
if a.platform_trust:
    trust_provider = 'macOS Keychain-backed OpenSSL verification' if sys.platform == 'darwin' else 'Windows Crypt32 current-user/system server-auth/NoCheck policy'
    report['scope'] = (
        f'Original WebUtil factory using platform default trust versus native {trust_provider} over actual peer chains, TLS1.2/1.3 direct+CONNECT, independent chain/name flags and retained connections. Generated issuers remain untrusted; no machine/user trust-store changes. Controlled peer fingerprints are mandatory; interception reports unavailable. This does not prove live trusted public roots, revocation or complete TLS/inner-error parity.'
    )
if a.linux_platform_trust:
    report['scope'] = (
        'Live Linux platform trust with isolated HOME and controlled SSL_CERT_FILE. TLS 1.2/1.3 direct and CONNECT, trusted and untrusted chains, expired certificates, hostname mismatch and independent WebUtil flag masking.'
    )
if a.linux_aia:
    report['scope'] = (
        'Live Linux TLS 1.2 with a missing intermediate, one HTTP AIA download, saved CA issuer and two distinct HTTPS connections in an isolated HOME.'
    )
if a.tunnel or a.sni or a.platform_trust:
    report['sourceTunnelRequests'] = sum(len(x['source'].get('proxyRequests', [])) for x in outputs)
    report['nativeTunnelRequests'] = sum(len(x['native'].get('proxyRequests', [])) for x in outputs)
    report['proxyCounterIncludesCleartextIsolationRequests'] = True
    for label in ['source', 'native']:
        report[label + 'ConnectRequests'] = sum(
            request['method'] == 'CONNECT'
            for case in outputs
            for request in case[label].get('proxyRequests', [])
        )
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not diffs, f'{len(diffs)} differences; see {w}/results.json'
print(
    f"PASS: {len(cases)} {'TLS' if a.tls else 'HTTP'} global factory scenarios, {report['nativeRequests']} requests"
)
