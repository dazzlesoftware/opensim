import argparse, hashlib, json, socket, socketserver, ssl, struct, sys, threading, subprocess, xml.etree.ElementTree as ET
from urllib.request import parse_http_list, parse_keqv_list
from pathlib import Path
from script_http_transport_network import NtlmTunnelProxy, ThreadingHTTPServer, check_proxy_auth, relay_bytes

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--tls-origin', action='store_true')
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
g = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(g, k).text = v
g = ET.SubElement(project, 'ItemGroup')
for f in [Path(__file__).with_name('config_xml_http_reference.cs')]:
    ET.SubElement(g, 'Compile', Include=str(f))
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs, encoding='utf-8')
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'],
    check=True,
    capture_output=True,
)
trace = []
port = 0
values = [
    b'foo',
    b'http:foo',
    b'http:',
    b'http:?q',
    b'http:#f',
    b'http:/next',
    b'../echo',
    b'?q',
    b'#f',
    b'#',
    b'',
    b'http://',
    b'http://x:65536/',
    b' /echo ',
    b'/echo#own',
    b'/echo#',
    b'/x%41?y=%2F#z',
    b'/x%GG#%20',
    b'/x\xc3\x83\xc2\xa9%41#%20',
    b'/x\xc3\xa9%41#%20',
    b'/x\xe9%41#%20',
    b'/echo#%C3%A9%20',
    b'/echo#%GG%20',
    b'/echo?a%00b#%20',
    b'/echo?a b#f',
]
values += [
    b'file:///tmp/opensim-http-scheme-probe',
    b'ftp://127.0.0.1:{PORT}/echo',
    b'ws://127.0.0.1:{PORT}/echo',
    b'mailto:user@127.0.0.1:{PORT}',
    b'a+b:c',
    b'news:thing',
    b'net.tcp://127.0.0.1:{PORT}/echo',
    b'gopher://127.0.0.1:{PORT}/echo',
    b'telnet://127.0.0.1:{PORT}/echo',
    b'uuid:thing',
]
values += [
    b'file://127.0.0.1/share',
    b'a+b://127.0.0.1/a',
    b'net.pipe://127.0.0.1/a',
    b'file://[::1]/share',
    b'a+b://[::1]/a',
]
headers = [b'Location: ' + v + b'\r\n' for v in values]
headers += [
    b'Location: \r\nLocation: /echo\r\n',
    b'Location: http://\r\nLocation: /echo\r\n',
    b'Location: /echo\r\nLocation: /different\r\n',
    b'Location: /echo\r\nSet-Cookie: a=first;Path=/\r\n',
    b'Location: /echo\r\nSet-Cookie: a=\xe9;Path=/\r\n',
    b'Location: /echo\r\nSet-Cookie: a="x\xffy";Path=/\r\n',
]
headers += [
    b'Location: ' + target + b'\r\nSet-Cookie: ' + cookie + b'\r\n'
    for target in [
        b'ftp://127.0.0.1:{PORT}/echo',
        b'ws://127.0.0.1:{PORT}/echo',
        b'mailto:user@127.0.0.1:{PORT}',
        b'net.tcp://127.0.0.1:{PORT}/echo',
    ]
    for cookie in [b'a=one;Path=/', b'a=one;Path=/;Secure', b'a=one;Version=1;Path=/']
]


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            data = b''
            while b'\r\n\r\n' not in data:
                b = self.request.recv(65536)
                if not b:
                    return
                data += b
            trace.append(data.hex())
            target = data.split(b' ')[1]
            if target.startswith(b'/auth-'):
                authorization = next((line.split(b':', 1)[1].strip() for line in data.split(b'\r\n')
                                      if line.lower().startswith(b'authorization:')), b'')
                digest = target == b'/auth-digest'
                if digest and authorization.startswith(b'Digest '):
                    fields = parse_keqv_list(parse_http_list(authorization[7:].decode()))
                    a1 = hashlib.md5(b'fixture:fixture:secret').hexdigest()
                    a2 = hashlib.md5(b'GET:/auth-digest').hexdigest()
                    valid = fields.get('response') == hashlib.md5(f'{a1}:fixture-nonce:{a2}'.encode()).hexdigest()
                    valid &= fields.get('uri') == '/auth-digest' and fields.get('username') == 'fixture'
                else:
                    expected = (b'Basic Og==' if target == b'/auth-default'
                                else b'Basic Zml4dHVyZTpzZWNyZXQ=')
                    valid = not digest and authorization == expected
                    if target == b'/auth-basic':
                        valid &= b'\r\ncookie: challenge=seen\r\n' in data.lower()
                if target == b'/auth-scoped/cache-redirect' and valid:
                    status, head, body = b'302 Found', b'Location: /auth-scoped/allowed\r\n', b''
                elif target == b'/auth-scoped/redirect' and valid:
                    status, head, body = b'302 Found', b'Location: /auth-scoped-denied\r\n', b''
                elif target == b'/auth-redirect' and valid:
                    status, head, body = b'302 Found', b'Location: /auth-result\r\n', b''
                elif target == b'/auth-result':
                    status, head, body = b'200 OK', b'', authorization or b'no authorization'
                elif valid and target != b'/auth-reject':
                    status, head, body = b'200 OK', b'', b'authenticated'
                else:
                    challenge = (b'Digest realm="fixture", nonce="fixture-nonce"' if digest
                                 else b'Basic realm="fixture"')
                    if target.startswith(b'/auth-scoped'):
                        challenge = b'Negotiate, NTLM, Digest realm="fixture", nonce="fixture-nonce", ' + challenge
                    status, head, body = b'401 Unauthorized', b'WWW-Authenticate: ' + challenge + b'\r\n', b''
                    if target == b'/auth-basic':
                        head += b'Set-Cookie: challenge=seen; Path=/\r\n'
            elif target.startswith(b'/case/'):
                index = int(target[6:].split(b'?')[0])
                head = headers[index].replace(b'{PORT}', str(port).encode())
                body = b''
                status = b'302 Found'
            elif target.startswith(b'/chain-'):
                mode, n = target.decode().rsplit('/', 1)
                n = int(n)
                location = (mode + '/' + str(n + 1) + '#step').encode() if n < 50 else b'/echo'
                if mode == '/chain-invalid' and n == 50:
                    location = b'http://'
                head = b'Location: ' + location + b'\r\n'
                body = b''
                status = b'302 Found'
            else:
                cookie = b''
                for h in data.split(b'\r\n')[1:]:
                    if h.lower().startswith(b'cookie:'):
                        cookie = h.split(b':', 1)[1].strip()
                body = target + b'|' + cookie
                head = b''
                status = b'200 OK'
            self.request.sendall(
                b'HTTP/1.1 '
                + status
                + b'\r\n'
                + head
                + b'Content-Length: '
                + str(len(body)).encode()
                + b'\r\nConnection: close\r\n\r\n'
                + body
            )
            # Hold the response side until the client completes; do not inject a reset race.
            self.request.settimeout(None)
            while self.request.recv(4096):
                pass
        except (ConnectionError, OSError, ValueError):
            pass


s = Server(('127.0.0.1', 0), Handler)
port = s.server_address[1]
threading.Thread(target=s.serve_forever, daemon=True).start()
base = 'http://127.0.0.1:' + str(port)
urls = [base + '/case/' + str(i) for i in range(len(headers))] + [
    base + '/case/' + str(i) + '#base' for i in range(len(headers))
]
urls += [
    base + '/chain-ok/0#base',
    base + '/chain-invalid/0#base',
    'http://:pass@127.0.0.1:' + str(port) + '/chain-cap/0#base',
]
urls += [base + '/auth-' + suffix for suffix in ('basic', 'digest', 'redirect', 'reject', 'default')]
urls += [base + '/auth-scoped' + suffix for suffix in ('/allowed', '/stateful', '/redirect', '-denied', '/cache-redirect')]
urls += [
    'ftp://127.0.0.1:' + str(port) + '/echo',
    'ws://127.0.0.1:' + str(port) + '/echo',
    'a+b:thing',
    'mailto:user@127.0.0.1:' + str(port),
]


def run(command):
    result = subprocess.run(
        command, input='\n'.join(urls) + '\n', text=True, capture_output=True, timeout=120
    )
    assert result.returncode == 0, (result.returncode, result.stderr)
    return result


source = run([a.dotnet, str(w / 'app/Reference.dll')])
source_trace = trace[:]
trace.clear()
native = run([a.native])
s.shutdown()
def credential_trace(result):
    return [line for line in result.stderr.splitlines() if line.startswith('CREDENTIAL ')]


assert credential_trace(source) == credential_trace(native), (credential_trace(source), credential_trace(native))
source_lines = source.stdout.splitlines()
native_lines = native.stdout.splitlines()
assert len(source_lines) == len(native_lines) == len(urls), (
    len(source_lines),
    len(native_lines),
    len(urls),
)
for path, body in (('basic', b'authenticated'), ('digest', b'authenticated'),
                   ('redirect', b'no authorization')):
    assert source_lines[urls.index(base + '/auth-' + path)] == 'ok:' + body.hex(), path
assert ':401:' in source_lines[urls.index(base + '/auth-reject')]
assert source_lines[urls.index(base + '/auth-default')] == 'ok:' + b'authenticated'.hex()
for suffix in ('/allowed', '/stateful', '/cache-redirect'):
    assert source_lines[urls.index(base + '/auth-scoped' + suffix)] == 'ok:' + b'authenticated'.hex()
for suffix in ('/redirect', '-denied'):
    assert ':401:' in source_lines[urls.index(base + '/auth-scoped' + suffix)]
diff = [
    dict(url=u, source=x, native=y) for u, x, y in zip(urls, source_lines, native_lines) if x != y
]
for name, text in [
    ('inputs.txt', '\n'.join(urls) + '\n'),
    ('actual-bcl.txt', source.stdout),
    ('native.txt', native.stdout),
    ('source-stderr.txt', source.stderr),
    ('native-stderr.txt', native.stderr),
]:
    (w / name).write_text(text, encoding='utf-8')
result = dict(
    cases=len(urls),
    passed=not diff and source_trace == trace,
    differences=diff,
    source_requests=source_trace,
    native_requests=trace,
    hashes={
        n: hashlib.sha256((w / n).read_bytes()).hexdigest()
        for n in ['inputs.txt', 'actual-bcl.txt', 'native.txt']
    },
    provider='explicit empty WebProxy/direct HTTP; source per-resource default CookieContainer; Connection close responses held open until client completion',
)
(w / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(len(urls), len(diff), len(source_trace), len(trace))
print(diff[:3])
assert not diff
assert source_trace == trace, [
    (i, x, y) for i, (x, y) in enumerate(zip(source_trace, trace)) if x != y
][:3]
print('PASS exact wire traces')


class StaleDigestProxy(NtlmTunnelProxy):
    def do_GET(self):
        assert self.headers.get('Authorization') is None
        if check_proxy_auth(self):
            self.send_response(200)
            self.send_header('Content-Length', '2')
            self.end_headers()
            self.wfile.write(b'ok')


with ThreadingHTTPServer(('127.0.0.1', 0), StaleDigestProxy) as proxy:
    proxy.require_basic_proxy_auth = True
    proxy.digest_proxy_auth = ('MD5', 'auth')
    proxy.digest_stale = True
    worker = threading.Thread(target=proxy.serve_forever, daemon=True)
    worker.start()
    try:
        for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
            proxy.proxy_authorizations = []
            result = subprocess.run([*command, str(proxy.server_port), '--proxy-cache'],
                                    input='http://127.0.0.2/fixture\n', text=True,
                                    capture_output=True, timeout=15, check=True)
            assert result.stdout.strip() == 'ok:6f6b', result
            assert len(proxy.proxy_authorizations) == 3 and proxy.proxy_authorizations[0] is None
    finally:
        proxy.shutdown()
        worker.join()

for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
    result = subprocess.run([*command, '--credential-cache-host'], text=True, capture_output=True,
                            timeout=15, check=True)
    assert result.stdout.splitlines() == ['fixture', '-', '-'], result

if sys.platform == 'win32':
    class NtlmOrigin(NtlmTunnelProxy):
        auth_header = 'Authorization'
        challenge_header = 'WWW-Authenticate'
        challenge_status = 401

        def handle(self):
            try:
                super().handle()
            except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
                # Failed authentication and certificate observers may close
                # before the server's next keep-alive read.
                pass

        def do_GET(self):
            assert self.path == getattr(self.server, 'request_target', '/auth-ntlm')
            if getattr(self.server, 'tunneled', False):
                assert self.headers.get('Proxy-Authorization') is None
            if hasattr(self.server, 'authorization_headers'):
                self.server.authorization_headers.append(self.headers.get(self.auth_header))
            if self.authenticate():
                self.send_response(200)
                self.send_header('Content-Length', '13')
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(b'authenticated')
                self.close_connection = True

        def end_headers(self):
            if getattr(self.server, 'supports_session_authentication', False):
                self.send_header('Proxy-Support', 'Session-Based-Authentication')
            super().end_headers()

    with ThreadingHTTPServer(('127.0.0.1', 0), NtlmOrigin) as origin:
        origin.completed = 0
        worker = threading.Thread(target=origin.serve_forever, daemon=True)
        worker.start()
        try:
            url = f'http://127.0.0.1:{origin.server_port}/auth-ntlm\n'
            for scheme in ('NTLM', 'Negotiate'):
                origin.auth_scheme = scheme
                for close in (False, True):
                    origin.close_first = close
                    for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
                        before = origin.completed
                        result = subprocess.run(command, input=url, text=True, capture_output=True, timeout=15, check=True)
                        assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (scheme, close, result)
                        assert origin.completed == before + 1
            origin.auth_scheme = 'NTLM'
            origin.close_first = False
            origin.allow_signing = False
            before = origin.completed
            for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
                result = subprocess.run(command, input=url, text=True, capture_output=True, timeout=15, check=True)
                assert ':401:' in result.stdout and origin.completed == before, result
        finally:
            origin.shutdown()
            worker.join()
    print('PASS direct origin NTLM and Negotiate with and without reconnect')

    with ThreadingHTTPServer(('127.0.0.1', 0), NtlmOrigin) as proxy:
        proxy.completed = 0
        proxy.request_target = 'http://127.0.0.2/auth-ntlm'
        worker = threading.Thread(target=proxy.serve_forever, daemon=True)
        worker.start()
        try:
            for scheme in ('NTLM', 'Negotiate'):
                proxy.auth_scheme = scheme
                for supports, close in ((True, False), (True, True), (False, False)):
                    proxy.supports_session_authentication = supports
                    proxy.close_first = close
                    for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
                        before = proxy.completed
                        proxy.authorization_headers = []
                        result = subprocess.run(
                            [*command, str(proxy.server_port)], input=proxy.request_target + '\n',
                            text=True, capture_output=True, timeout=15, check=True,
                        )
                        if supports:
                            assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (scheme, close, result)
                            assert proxy.completed == before + 1
                        else:
                            assert ':401:' in result.stdout and proxy.completed == before, result
                            assert proxy.authorization_headers == [None], proxy.authorization_headers
        finally:
            proxy.shutdown()
            worker.join()
    print('PASS forwarding proxy origin authentication with advertised session support')

    class NtlmProxyAndOrigin(NtlmOrigin):
        def do_GET(self):
            assert self.path == self.server.request_target
            if not getattr(self, 'proxy_authenticated', False):
                if not self.server.origin_closed:
                    assert self.headers.get('Authorization') is None
                else:
                    origin_token = self.headers.get('Authorization')
                    assert origin_token is not None
                    if not hasattr(self, 'origin_token'):
                        self.origin_token = origin_token
                    assert origin_token == self.origin_token
                self.auth_header = 'Proxy-Authorization'
                self.challenge_header = 'Proxy-Authenticate'
                self.challenge_status = 407
                try:
                    if (self.server.origin_closed and not getattr(self, 'negotiated', False)
                            and self.headers.get(self.auth_header) == self.server.previous_proxy_authorization):
                        self.challenge(self.server.auth_scheme, b'reauthenticate')
                        return
                    if not self.authenticate():
                        return
                    self.proxy_authenticated = True
                    self.proxy_authorization = self.headers[self.auth_header]
                    self.negotiated = False
                finally:
                    self.auth_header = 'Authorization'
                    self.challenge_header = 'WWW-Authenticate'
                    self.challenge_status = 401
            assert self.headers.get('Proxy-Authorization') == self.proxy_authorization
            if self.server.close_origin and not self.server.origin_closed:
                self.server.origin_closed = True
                self.server.previous_proxy_authorization = self.proxy_authorization
                self.server.authorization_headers.append(self.headers.get('Authorization'))
                self.challenge(self.server.auth_scheme, b'reconnect', close=True)
                return
            super().do_GET()

    with ThreadingHTTPServer(('127.0.0.1', 0), NtlmProxyAndOrigin) as proxy:
        proxy.completed = 0
        proxy.request_target = 'http://127.0.0.2/auth-ntlm'
        worker = threading.Thread(target=proxy.serve_forever, daemon=True)
        worker.start()
        try:
            for scheme in ('NTLM', 'Negotiate'):
                proxy.auth_scheme = scheme
                for supports, close_origin in ((True, False), (False, False), (True, True), (False, True)):
                    proxy.supports_session_authentication = supports
                    proxy.close_origin = close_origin
                    for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
                        before = proxy.completed
                        proxy.origin_closed = False
                        proxy.authorization_headers = []
                        result = subprocess.run(
                            [*command, str(proxy.server_port), '--proxy-cache' if supports else '--proxy-auth'],
                            input=proxy.request_target + '\n', text=True, capture_output=True,
                            timeout=15, check=True,
                        )
                        if supports:
                            assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (scheme, result)
                            assert proxy.completed == before + (3 if close_origin else 2)
                        else:
                            assert ':401:' in result.stdout and proxy.completed == before + 1, result
                            assert proxy.authorization_headers == [None], proxy.authorization_headers
        finally:
            proxy.shutdown()
            worker.join()
    print('PASS proxy and origin integrated authentication with and without an origin reconnect')

    if a.tls_origin:
        from itertools import product
        from tempfile import TemporaryDirectory

        class AuthenticatedOriginTunnel(NtlmTunnelProxy):
            def do_CONNECT(self):
                assert self.path == f'127.0.0.1:{self.server.target_port}'
                assert self.headers.get('Authorization') is None
                if getattr(self.server, 'digest_proxy_auth', None):
                    if not check_proxy_auth(self):
                        return
                elif self.server.authenticate_proxy and not self.authenticate():
                    return
                self.server.tunnels += 1
                self.close_connection = True
                with socket.create_connection(('127.0.0.1', self.server.target_port), timeout=5) as peer:
                    self.wfile.write(self.tunnel_response())
                    self.wfile.flush()
                    relay_bytes(self.connection, peer)

        fixture = Path(__file__).with_name('fixtures')
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(fixture / 'localhost-cert.pem', fixture / 'localhost-key.pem')
        commands = ([a.dotnet, str(w / 'app/Reference.dll')], [a.native])
        proxy_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        with TemporaryDirectory(prefix='opensim-proxy-certificate-') as directory:
            subprocess.run([*commands[0], '--create-proxy-certificate', directory], timeout=15, check=True)
            proxy_context.load_cert_chain(Path(directory) / 'certificate.pem', Path(directory) / 'key.pem')
        with ThreadingHTTPServer(('127.0.0.1', 0), NtlmOrigin) as proxy:
            proxy.socket = proxy_context.wrap_socket(proxy.socket, server_side=True)
            proxy.request_target = 'http://127.0.0.2/auth-ntlm'
            proxy.completed = 0
            proxy.expected_binding = bytes(16)
            worker = threading.Thread(target=proxy.serve_forever, daemon=True)
            worker.start()
            try:
                for supports, command in product((True, False), commands):
                    proxy.supports_session_authentication = supports
                    proxy.authorization_headers = []
                    before = proxy.completed
                    result = subprocess.run(
                        [*command, str(proxy.server_port), '--tls-proxy'],
                        input=proxy.request_target + '\n', text=True, capture_output=True, timeout=15, check=True,
                    )
                    if supports:
                        assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), result
                        assert proxy.completed == before + 1
                    else:
                        assert ':401:' in result.stdout and proxy.completed == before, result
                        assert proxy.authorization_headers == [None], proxy.authorization_headers
            finally:
                proxy.shutdown()
                worker.join()
        print('PASS HTTP origin NTLM through HTTPS forwarding')

        with ThreadingHTTPServer(('127.0.0.1', 0), NtlmOrigin) as origin:
            origin.socket = context.wrap_socket(origin.socket, server_side=True)
            origin.completed = 0
            worker = threading.Thread(target=origin.serve_forever, daemon=True)
            worker.start()
            try:
                observer = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                observer.check_hostname = False
                observer.verify_mode = ssl.CERT_NONE
                with socket.create_connection(origin.server_address, timeout=5) as raw:
                    with observer.wrap_socket(raw, server_hostname='127.0.0.1') as peer:
                        certificate = peer.getpeercert(binary_form=True)
                binding = b'tls-server-end-point:' + hashlib.sha256(certificate).digest()
                origin.expected_binding = hashlib.md5(struct.pack('<IIIII', 0, 0, 0, 0, len(binding)) + binding).digest()
                url = f'https://127.0.0.1:{origin.server_port}/auth-ntlm\n'
                for close in (False, True):
                    origin.close_first = close
                    for command in ([a.dotnet, str(w / 'app/Reference.dll')], [a.native]):
                        before = origin.completed
                        result = subprocess.run(command, input=url, text=True, capture_output=True, timeout=15, check=True)
                        assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (close, result)
                        assert origin.completed == before + 1
                origin.tunneled = True
                def verify_tunnel(proxy, secure_proxy):
                    proxy.target_port = origin.server_port
                    proxy.tunnels = proxy.completed = 0
                    tunnel_worker = threading.Thread(target=proxy.serve_forever, daemon=True)
                    tunnel_worker.start()
                    try:
                        commands = ([a.dotnet, str(w / 'app/Reference.dll')], [a.native])
                        for authenticate_proxy, close, command in product((False, True), (False, True), commands):
                            proxy.authenticate_proxy = authenticate_proxy
                            origin.close_first = close
                            before = origin.completed
                            tunnels = proxy.tunnels
                            proofs = proxy.completed
                            arguments = [*command, str(proxy.server_port)]
                            if secure_proxy:
                                arguments.append('--tls-proxy')
                            if authenticate_proxy:
                                arguments.append('--proxy-cache' if secure_proxy else '--proxy-auth')
                            result = subprocess.run(
                                arguments, input=url, text=True, capture_output=True, timeout=15, check=True,
                            )
                            assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (
                                authenticate_proxy, close, result,
                            )
                            assert origin.completed == before + 1
                            expected_tunnels = 2 if close else 1
                            assert proxy.tunnels - tunnels == expected_tunnels
                            assert proxy.completed - proofs == (expected_tunnels if authenticate_proxy else 0)
                        origin.close_first = False
                        proxy.require_basic_proxy_auth = True
                        proxy.digest_proxy_auth = ('MD5', 'auth')
                        proxy.digest_stale = True
                        for command in commands:
                            proxy.proxy_authorizations = []
                            before = origin.completed
                            tunnels = proxy.tunnels
                            arguments = [*command, str(proxy.server_port), '--proxy-cache']
                            if secure_proxy:
                                arguments.append('--tls-proxy')
                            result = subprocess.run(
                                arguments, input=url, text=True, capture_output=True, timeout=15, check=True,
                            )
                            assert result.stdout.strip() == 'ok:' + b'authenticated'.hex(), (secure_proxy, result)
                            assert origin.completed == before + 1 and proxy.tunnels == tunnels + 1
                            assert len(proxy.proxy_authorizations) == 3 and proxy.proxy_authorizations[0] is None
                    finally:
                        proxy.shutdown()
                        tunnel_worker.join()

                for secure_proxy in (False, True):
                    with ThreadingHTTPServer(('127.0.0.1', 0), AuthenticatedOriginTunnel) as proxy:
                        if secure_proxy:
                            proxy.socket = proxy_context.wrap_socket(proxy.socket, server_side=True)
                        verify_tunnel(proxy, secure_proxy)
            finally:
                origin.shutdown()
                worker.join()
        print('PASS HTTPS origin NTLM TLS channel binding with and without reconnect')
        print('PASS HTTPS origin NTLM TLS channel binding through HTTP and HTTPS CONNECT')
        print('PASS cached Digest stale nonce retry through HTTP and HTTPS CONNECT')
