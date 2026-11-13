"""Exercise the outbound script HTTP worker against a loopback server."""

from base64 import b64decode, b64encode
import ctypes
from hashlib import md5, sha256
import hmac
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from select import select
import socket
from socketserver import StreamRequestHandler, ThreadingTCPServer
import ssl
import struct
import subprocess
import sys
from tempfile import NamedTemporaryFile
from threading import Event, Thread
from time import sleep
from urllib.parse import urlsplit
from urllib.request import parse_http_list, parse_keqv_list


def digest_proxy_auth(handler, received, body):
    algorithm, qop = handler.server.digest_proxy_auth
    userhash = getattr(handler.server, 'digest_userhash', False)
    attempt = len(handler.server.proxy_authorizations)
    nonce = 'fresh' if attempt > 2 else 'first'
    stale = attempt == 2 and handler.server.digest_stale
    if received:
        assert received.startswith('Digest ')
        params = parse_keqv_list(parse_http_list(received[len('Digest '):]))
        digest = sha256 if (algorithm or '').startswith('SHA-256') else md5

        def hash_text(text):
            return digest(text.encode()).hexdigest()

        user, password, realm = 'fïxture', 'päss', 'fixture'
        expected_nonce = 'first' if stale else nonce
        uri = '/' if handler.command == 'CONNECT' else urlsplit(handler.path).path
        a1 = f'{user}:{realm}:{password}'
        if (algorithm or '').endswith('-sess'):
            a1 = f"{hash_text(a1)}:{expected_nonce}:{params['cnonce']}"
        a2 = f'{handler.command}:{uri}'
        if qop == 'auth-int':
            a2 += ':' + digest(getattr(handler.server, 'digest_decoded_body', body)).hexdigest()
        response = f'{hash_text(a1)}:{expected_nonce}:'
        if qop:
            assert params['nc'] == '00000001'
            response += f"{params['nc']}:{params['cnonce']}:{qop}:"
        response += hash_text(a2)
        if userhash:
            assert params['userhash'] == 'true' and params['username'] == hash_text(f'{user}:{realm}')
        else:
            assert params['username*'] == "utf-8''f%C3%AFxture"
        assert params.get('algorithm') == algorithm and params.get('qop') == qop
        assert params['uri'] == uri and params['nonce'] == expected_nonce
        assert params['response'] == hash_text(response)
        if not stale:
            return True, ''
    if stale:
        nonce = 'fresh'
    challenge = f'Digest Realm="fixture", Nonce="{nonce}"'
    if algorithm:
        challenge += f', algorithm={algorithm}'
    if qop:
        challenge += f', qop="{qop}"'
    if userhash:
        challenge += ', userhash=true'
    if stale:
        challenge += ', stale=true'
    return False, challenge


def check_proxy_auth(handler, body=b''):
    if not getattr(handler.server, 'require_basic_proxy_auth', False):
        return True
    received = handler.headers.get('Proxy-Authorization')
    handler.server.proxy_authorizations.append(received)
    expected = 'Basic ' + b64encode('fïxture:päss'.encode()).decode()
    if getattr(handler.server, 'digest_proxy_auth', None):
        accepted, challenge = digest_proxy_auth(handler, received, body)
        if accepted:
            return True
    else:
        if received == expected:
            return True
        challenge = getattr(handler.server, 'proxy_challenge', 'Unknown realm="ignore, Basic", bAsIc realm="fixture"')
    handler.send_response(407)
    handler.send_header('Proxy-Authenticate', challenge)
    handler.send_header('Content-Length', '0')
    handler.end_headers()
    return False


class Fixture(BaseHTTPRequestHandler):
    def handle(self):
        if getattr(self.server, 'reject_redirect_tls', False):
            if self.connection.recv(1, socket.MSG_PEEK) == b'\x16':
                self.server.rejected_tls_connections += 1
                # TLS fatal handshake_failure alert.
                self.connection.sendall(b'\x15\x03\x03\x00\x02\x02\x28')
                return
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            # A client rejecting the TLS certificate can close before sending HTTP.
            if not isinstance(self.connection, ssl.SSLSocket):
                raise

    def do_POST(self):
        content_length = self.headers.get('Content-Length', '0')
        assert content_length == getattr(self.server, 'expected_content_length', content_length)
        request_body = self.rfile.read(int(content_length.split(',')[0]))
        proxy_redirect = f'http://script-proxy.invalid:{self.server.server_port}/proxy-redirect'
        forwarding = self.path in ('http://script-proxy.invalid/proxied', proxy_redirect)
        if forwarding and not check_proxy_auth(self, request_body):
            return
        if (
            request_body != getattr(self.server, 'expected_body', b'payload')
            or self.headers.get('X-Fixture') != getattr(self.server, 'expected_fixture_header', 'kept')
            or self.headers.get('Pragma') != 'no-cache'
            or self.headers.get('Connection') != 'close'
            or self.headers.get('Content-Type') != getattr(self.server, 'expected_content_type', 'text/plain;charset=utf-8')
            or (not forwarding and self.headers.get('Proxy-Authorization') is not None)
        ):
            self.send_error(400)
            return
        if getattr(self.server, 'expected_fixture_header', None):
            assert self.headers.get_all('X-Fixture') == ['first, kept']
            assert self.headers.get_all('User-Agent') == ['one two']
            assert self.headers.get_all('Cookie') == ['one; two']
            assert self.headers.get_all('Transfer-Encoding') == ['gzip; level="a,b"']
            assert self.headers.get('Allow') is None
            assert self.headers.get('ETag') == '"fixture"'

        if self.path == '/start':
            self.reply(302, location='finish')
        elif self.path == '/finish':
            self.reply(418, 'A\r\nB\u2028C\rD\fE'.encode())
        elif self.path == '/blocked':
            self.reply(307, location='http://127.0.0.2:12345/blocked')
        elif self.path == '/unsupported-redirect':
            self.reply(302, location=f'ftp://127.0.0.1:{self.server.server_port}/finish')
        elif self.path == '/blocked-ftp-redirect':
            self.reply(302, location='ftp://127.0.0.2:12345/finish')
        elif self.path == '/no-location':
            self.reply(301)
        elif self.path == '/cycle':
            self.reply(303, location='cycle')
        elif self.path == '/preserve-308':
            self.reply(308, b'not redirected', 'finish')
        elif self.path == '/tls-redirect':
            self.reply(302, location=f'https://127.0.0.1:{self.server.server_port}/finish')
        elif self.path == 'http://script-proxy.invalid/proxied':
            if self.headers.get('Host') == 'script-proxy.invalid':
                self.reply(202, b'forward proxy')
            else:
                self.send_error(400, 'Expected origin Host header')
        elif self.path == proxy_redirect:
            self.reply(302, location=f'http://127.0.0.1:{self.server.server_port}/finish')
        elif self.path == '/body-timeout':
            self.send_response(200)
            self.send_header('Content-Length', '16')
            self.end_headers()
            self.wfile.flush()
            sleep(7)
        else:
            self.send_error(404)

    def do_propfind(self):
        self.do_POST()

    def do_CONNECT(self):
        assert self.path == 'fixture.invalid:443'
        assert self.headers.get('Host') == self.path
        framing = getattr(self.server, 'opaque_connect', None)
        if framing:
            self.send_response(200)
            self.send_header(*framing)
            self.end_headers()
            self.wfile.write(b'opaque\r\nstream')
            self.close_connection = True
        else:
            self.reply(418, b'authority')

    def do_GET(self):
        if self.path == '/socks-pool':
            assert self.headers.get('Proxy-Authorization') is None
            self.protocol_version = 'HTTP/1.1'
            self.close_connection = False
            self.reply(200, b'ok')
            return
        self.server.shared_requests.append(self.path)
        if self.path == '/shared-first':
            if not self.server.shared_queued.wait(3):
                self.send_error(500)
                return
            self.server.shared_requests.append('first-finished')
            self.reply(302, location='shared-final')
        elif self.path == '/shared-final':
            self.reply(200, b'first')
        elif self.path == '/shared-second':
            self.reply(200, b'second')
        elif self.path == '/shared-restart':
            self.reply(200, b'restarted')
        else:
            self.send_error(404)

    def reply(self, status, body=b'', location=None):
        self.send_response(status)
        if location is not None:
            self.send_header('Location', location)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


class TunnelProxy(BaseHTTPRequestHandler):
    def do_CONNECT(self):
        if not check_proxy_auth(self):
            return
        if self.path != f'script-proxy.invalid:{self.server.target_port}':
            self.send_error(400)
            return
        self.close_connection = True
        with socket.create_connection(('127.0.0.1', self.server.target_port), timeout=5) as peer:
            self.wfile.write(self.tunnel_response())
            self.wfile.flush()
            relay_bytes(self.connection, peer)

    def tunnel_response(self):
        return b'HTTP/1.1 200 Connection Established\r\n\r\n'

    def log_message(self, format, *args):
        pass


class NtlmTunnelProxy(TunnelProxy):
    protocol_version = 'HTTP/1.1'
    nonce = b'fixture!'
    auth_header = 'Proxy-Authorization'
    challenge_header = 'Proxy-Authenticate'
    challenge_status = 407

    def challenge(self, authorization, body, chunked=False, close=False):
        self.send_response(self.challenge_status)
        self.send_header(self.challenge_header, authorization)
        self.close_connection = close
        if close:
            self.send_header('Connection', 'close')
        if chunked:
            self.send_header('Transfer-Encoding', 'chunked')
            self.send_header('Trailer', 'X-Fixture-Trailer')
        else:
            self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if chunked:
            self.wfile.write(f'{len(body):x}\r\n'.encode() + body + b'\r\n0\r\nX-Fixture-Trailer: drained\r\n\r\n')
        else:
            self.wfile.write(body)

    def authenticate(self):
        expected_scheme = getattr(self.server, 'auth_scheme', 'NTLM')
        authorization = self.headers.get(self.auth_header)
        if not authorization:
            self.server.initial_connection = self.client_address[1]
            self.challenge(f'Basic realm="fixture", {expected_scheme}', b'authenticate',
                           close=getattr(self.server, 'close_first', False))
            return False
        scheme, token = authorization.split(' ', 1)
        assert scheme == expected_scheme
        message = b64decode(token, validate=True)
        spnego_wrapped = False
        if scheme == 'Negotiate':
            offset = message.index(b'NTLMSSP\0')
            spnego_wrapped = offset != 0
            message = message[offset:]
        assert message.startswith(b'NTLMSSP\0')
        kind, = struct.unpack_from('<I', message, 8)
        if kind == 1:
            if getattr(self.server, 'close_first', False):
                assert self.client_address[1] != self.server.initial_connection
            self.negotiated = True
            domain = 'FIXTURE'.encode('utf-16-le')
            computer = 'PROXY'.encode('utf-16-le')
            info = (struct.pack('<HH', 2, len(domain)) + domain
                    + struct.pack('<HH', 1, len(computer)) + computer + b'\0' * 4)
            version = bytes.fromhex('0a00654a0000000f')
            requested_flags = struct.unpack_from('<I', message, 12)[0]
            signing = (requested_flags & 0x40000010
                       if requested_flags & 0x10 and getattr(self.server, 'allow_signing', True) else 0)
            challenge = (b'NTLMSSP\0' + struct.pack('<IHHII', 2, len(domain), len(domain), 56, 0xa28a8205 | signing)
                         + self.nonce + b'\0' * 8
                         + struct.pack('<HHI', len(info), len(info), 56 + len(domain))
                         + version + domain + info)
            if spnego_wrapped:
                def der(tag, value):
                    size = len(value)
                    length = bytes([size]) if size < 128 else bytes([0x81, size])
                    return bytes([tag]) + length + value

                ntlm_mechanism = bytes.fromhex('060a2b06010401823702020a')
                response = bytes.fromhex('a0030a0101') + der(0xa1, ntlm_mechanism) + der(0xa2, der(0x04, challenge))
                challenge = der(0xa1, der(0x30, response))
            self.challenge(scheme + ' ' + b64encode(challenge).decode(), b'continue', chunked=True,
                           close=getattr(self.server, 'close_second', False))
            return False

        assert kind == 3 and self.negotiated

        def field(offset):
            length, _, start = struct.unpack_from('<HHI', message, offset)
            return message[start:start + length]

        domain = field(28).decode('utf-16-le')
        username = field(36).decode('utf-16-le')
        assert username == 'fïxture'
        response = field(20)
        password_hash = bytes.fromhex('411b68984d6b19bbb302455798320a06')
        key = hmac.digest(password_hash, (username.upper() + domain).encode('utf-16-le'), 'md5')
        assert len(response) > 16 and hmac.compare_digest(response[:16], hmac.digest(key, self.nonce + response[16:], 'md5'))
        expected_binding = getattr(self.server, 'expected_binding', None)
        if expected_binding is not None:
            av_pairs = {}
            offset = 44  # NT proof plus the NTLMv2 response header.
            while offset + 4 <= len(response):
                av_type, length = struct.unpack_from('<HH', response, offset)
                offset += 4
                if av_type == 0:
                    break
                av_pairs[av_type] = response[offset:offset + length]
                offset += length
            # GSS NTLM may omit the AV pair when no channel binding is supplied.
            assert av_pairs.get(10, bytes(16)) == expected_binding
        self.server.completed += 1
        return True

    def do_CONNECT(self):
        if self.authenticate():
            super().do_CONNECT()


class NtlmForwardProxy(NtlmTunnelProxy):
    def do_POST(self):
        assert self.path == 'http://script-proxy.invalid/proxied'
        assert self.headers.get('Host') == 'script-proxy.invalid'
        assert self.rfile.read(int(self.headers.get('Content-Length', '0'))) == b'payload'
        if self.authenticate():
            self.send_response(202)
            self.send_header('Content-Length', '13')
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(b'forward proxy')
            self.close_connection = True

    def do_GET(self):
        if getattr(self, 'authenticated', False):
            assert self.headers.get('Proxy-Authorization') is None
        else:
            if not self.authenticate():
                return
            self.authenticated = True
        self.server.pooled_requests += 1
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')


class GssBuffer(ctypes.Structure):
    _fields_ = [('length', ctypes.c_size_t), ('value', ctypes.c_void_p)]


class GssChannelBindings(ctypes.Structure):
    _fields_ = [
        ('initiator_type', ctypes.c_uint32), ('initiator', GssBuffer),
        ('acceptor_type', ctypes.c_uint32), ('acceptor', GssBuffer),
        ('application_data', GssBuffer),
    ]


class GssNegotiateForwardProxy(NtlmForwardProxy):
    def authenticate(self):
        header = self.headers.get(self.auth_header)
        if not header:
            self.challenge('Negotiate', b'authenticate')
            return False
        scheme, encoded = header.split(' ', 1)
        assert scheme == 'Negotiate'
        if not hasattr(self, 'security_context'):
            self.security_context = ctypes.c_void_p()
        token = b64decode(encoded, validate=True)
        if getattr(self.server, 'require_kerberos', False):
            assert b'NTLMSSP\0' not in token
        data = ctypes.create_string_buffer(token)
        incoming = GssBuffer(len(token), ctypes.cast(data, ctypes.c_void_p))
        outgoing = GssBuffer()
        minor = ctypes.c_uint32()
        flags = ctypes.c_uint32()
        binding_data = getattr(self.server, 'channel_binding', None)
        bindings = None
        if binding_data:
            application = ctypes.create_string_buffer(binding_data)
            bindings = GssChannelBindings(application_data=GssBuffer(
                len(binding_data), ctypes.cast(application, ctypes.c_void_p),
            ))
        major = self.server.gss.gss_accept_sec_context(
            ctypes.byref(minor), ctypes.byref(self.security_context), None, ctypes.byref(incoming),
            ctypes.byref(bindings) if bindings is not None else None, None, None,
            ctypes.byref(outgoing), ctypes.byref(flags) if bindings is not None else None, None, None,
        )
        try:
            reply = ctypes.string_at(outgoing.value, outgoing.length) if outgoing.length else b''
        finally:
            self.server.gss.gss_release_buffer(ctypes.byref(minor), ctypes.byref(outgoing))
        if major == 1:  # GSS_S_CONTINUE_NEEDED
            self.challenge('Negotiate ' + b64encode(reply).decode(), b'continue')
            return False
        assert major == 0, (major, minor.value)
        if bindings is not None:
            channel_bound = 2048  # GSS_C_CHANNEL_BOUND_FLAG
            assert flags.value & channel_bound
        self.final_token = reply
        self.server.completed += 1
        return True

    def end_headers(self):
        if getattr(self, 'final_token', b'') and getattr(self.server, 'send_final_token', False):
            self.send_header(self.challenge_header, 'Negotiate ' + b64encode(self.final_token).decode())
            self.final_token = b''
        super().end_headers()

    def tunnel_response(self):
        if getattr(self, 'final_token', b'') and getattr(self.server, 'send_final_token', False):
            return (b'HTTP/1.1 200 Connection Established\r\nProxy-Authenticate: Negotiate '
                    + b64encode(self.final_token) + b'\r\n\r\n')
        return super().tunnel_response()

    def finish(self):
        try:
            super().finish()
        finally:
            if getattr(self, 'security_context', None):
                minor = ctypes.c_uint32()
                self.server.gss.gss_delete_sec_context(
                    ctypes.byref(minor), ctypes.byref(self.security_context), None,
                )


def verify_gss_negotiate(request, context, scheme, target=None):
    with NamedTemporaryFile(mode='w', encoding='utf-8') as users:
        users.write(':fïxture:päss\nWORKGROUP:fïxture:päss\n')
        users.flush()
        previous = os.environ.get('NTLM_USER_FILE')
        os.environ['NTLM_USER_FILE'] = users.name
        try:
            with ThreadingHTTPServer(('127.0.0.1', 0), GssNegotiateForwardProxy) as forwarding:
                if scheme == 'https':
                    forwarding.socket = context.wrap_socket(forwarding.socket, server_side=True)
                forwarding.gss = ctypes.CDLL('libgssapi_krb5.so.2')
                forwarding.gss.gss_accept_sec_context.restype = ctypes.c_uint32
                forwarding.completed = 0
                if target:
                    forwarding.target_port = urlsplit(target).port
                relay = Thread(target=forwarding.serve_forever, daemon=True)
                relay.start()
                try:
                    proxy = f'{scheme}://127.0.0.1:{forwarding.server_port}'
                    url = target or 'http://script-proxy.invalid/proxied'
                    expected = (418, 'A\nB\nC\nD\nE') if target else (202, 'forward proxy')
                    assert request(url, proxy=proxy, credentials=True, verify=not target) == expected, scheme
                    assert forwarding.completed == 1
                    forwarding.send_final_token = True
                    status, error = request(url, proxy=proxy, credentials=True, verify=not target)
                    assert status == 401 and 'InvalidToken' in error, (scheme, status, error)
                    assert forwarding.completed == 2
                finally:
                    forwarding.shutdown()
                    relay.join()
        finally:
            if previous is None:
                os.environ.pop('NTLM_USER_FILE', None)
            else:
                os.environ['NTLM_USER_FILE'] = previous


def verify_kerberos_proxy(request, context, scheme, target=None, credentials='kerberos'):
    host = os.environ.get('OPENSIM_KERBEROS_PROXY_HOST', 'proxy-alias.opensim.test')
    address = os.environ.get('OPENSIM_KERBEROS_PROXY_BIND', '127.0.0.2')
    with ThreadingHTTPServer((address, 0), GssNegotiateForwardProxy) as proxy:
        if scheme == 'https':
            proxy.socket = context.wrap_socket(proxy.socket, server_side=True)
        proxy.gss = ctypes.CDLL('libgssapi_krb5.so.2')
        proxy.gss.gss_accept_sec_context.restype = ctypes.c_uint32
        proxy.require_kerberos = True
        proxy.send_final_token = True
        proxy.completed = 0
        if target:
            proxy.target_port = urlsplit(target).port
        worker = Thread(target=proxy.serve_forever, daemon=True)
        worker.start()
        try:
            endpoint = f'{scheme}://{host}:{proxy.server_port}'
            url = target or 'http://script-proxy.invalid/proxied'
            expected = (418, 'A\nB\nC\nD\nE') if target else (202, 'forward proxy')
            assert request(url, proxy=endpoint, credentials=credentials, verify=False) == expected, (
                scheme, target, credentials
            )
            assert proxy.completed == 1
        finally:
            proxy.shutdown()
            worker.join()


def relay_bytes(client, peer):
    while True:
        ready = [stream for stream in (client, peer) if isinstance(stream, ssl.SSLSocket) and stream.pending()]
        if not ready:
            ready, _, _ = select([client, peer], [], [], 5)
        if not ready:
            return
        for stream in ready:
            try:
                data = stream.recv(65536)
                if not data:
                    return
                (peer if stream is client else client).sendall(data)
            except OSError:
                return


class SocksProxy(StreamRequestHandler):
    def handle(self):
        self.server.handshakes.append(self.client_address)
        version = self.rfile.read(1)
        assert version == (b'\x05' if self.server.mode == 'socks5' else b'\x04')
        if version == b'\x05':
            if self.server.authenticated:
                assert self.rfile.read(3) == b'\x02\x00\x02'
                self.wfile.write(b'\x05\x02')
                assert self.rfile.read(1) == b'\x01'
                assert self.rfile.read(self.rfile.read(1)[0]) == 'fïxture'.encode()
                assert self.rfile.read(self.rfile.read(1)[0]) == 'päss'.encode()
                if self.server.reject_auth:
                    self.wfile.write(b'\x01\x01')
                    return
                self.wfile.write(b'\x01\x00')
            else:
                assert self.rfile.read(2) == b'\x01\x00'
                self.wfile.write(b'\x05\x00')
            assert self.rfile.read(4) == b'\x05\x01\x00\x03'
            length = self.rfile.read(1)[0]
            host = self.rfile.read(length)
            port = int.from_bytes(self.rfile.read(2), 'big')
            response = b'\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00'
        else:
            assert self.rfile.read(1) == b'\x01'
            port = int.from_bytes(self.rfile.read(2), 'big')
            ip = self.rfile.read(4)
            if self.server.authenticated:
                assert self.rfile.read(len('fïxture'.encode())) == 'fïxture'.encode()
            assert self.rfile.read(1) == b'\x00'
            if ip == b'\x00\x00\x00\xff':
                assert self.server.mode == 'socks4a'
                host = bytearray()
                while (byte := self.rfile.read(1)) not in (b'', b'\x00'):
                    host.extend(byte)
                host = bytes(host)
            else:
                assert self.server.mode == 'socks4'
                assert ip == b'\xc0\x00\x02\x09'
                host = b'192.0.2.9'
            response = b'\x00\x5a\x00\x00\x00\x00\x00\x00'
        assert host in (b'192.0.2.9', b'script-proxy.invalid')
        assert port == self.server.target_port
        if self.server.reject:
            self.wfile.write(b'\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00')
            return
        with socket.create_connection(('127.0.0.1', port), timeout=5) as peer:
            self.wfile.write(response)
            self.wfile.flush()
            relay_bytes(self.connection, peer)


def main():
    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as server:
        server.shared_requests = []
        server.shared_queued = Event()
        tls = '--tls' in sys.argv[2:]
        if tls:
            fixtures = Path(__file__).with_name('fixtures')
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(fixtures / 'localhost-cert.pem', fixtures / 'localhost-key.pem')
            server.socket = context.wrap_socket(server.socket, server_side=True)
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        scheme = 'https' if tls else 'http'
        origin = f'{scheme}://127.0.0.1:{server.server_port}'

        def request(path, maximum=64, proxy=None, verify=True, credentials=False, encoding=None):
            url = path if path.startswith(('http://', 'https://')) else origin + path
            command = '--http-transport-probe' if verify else '--http-transport-insecure-probe'
            args = [sys.argv[1], command, url, str(maximum)]
            if proxy:
                args.extend([proxy, ''])
            if credentials:
                args.append({
                    'environment': '--environment-credentials',
                    'kerberos': '--kerberos-credentials',
                    'kerberos-default': '--kerberos-default-credentials',
                }.get(credentials, '--credentials'))
            if encoding:
                args.append(encoding)
            result = subprocess.check_output(
                args, text=True, timeout=8,
            )
            status, body = result.rstrip('\n').split(' ', 1)
            return int(status), bytes.fromhex(body).decode()

        try:
            with ThreadingTCPServer(('127.0.0.1', 0), SocksProxy) as socks:
                socks.target_port = server.server_port
                socks.reject = False
                socks.reject_auth = False
                socks.handshakes = []
                socks_worker = Thread(target=socks.serve_forever, daemon=True)
                socks_worker.start()
                try:
                    for mode in ('socks4', 'socks4a', 'socks5'):
                        socks.mode = mode
                        host = '192.0.2.9' if mode == 'socks4' else 'script-proxy.invalid'
                        destination = f'{scheme}://{host}:{server.server_port}/finish'
                        proxy = f'{mode}://127.0.0.1:{socks.server_address[1]}'
                        for authenticated in (False, True):
                            socks.authenticated = authenticated
                            assert request(destination, proxy=proxy, verify=not tls, credentials=authenticated) == (
                                418, 'A\nB\nC\nD\nE'
                            ), mode
                    socks.mode = 'socks5'
                    socks.authenticated = True
                    socks5_destination = f'{scheme}://script-proxy.invalid:{server.server_port}/finish'
                    socks5_proxy = f'socks5://127.0.0.1:{socks.server_address[1]}'
                    socks.reject_auth = True
                    assert request(socks5_destination, proxy=socks5_proxy, verify=not tls, credentials=True)[0] == 499
                    socks.reject_auth = False
                    assert request(
                        socks5_destination, proxy=socks5_proxy, verify=not tls, credentials='environment'
                    ) == (418, 'A\nB\nC\nD\nE')
                    handshakes_before = len(socks.handshakes)
                    pool_destination = f'{scheme}://script-proxy.invalid:{server.server_port}/socks-pool'
                    subprocess.run(
                        [sys.argv[1], '--http-transport-probe', pool_destination, '64', socks5_proxy, '',
                         '--pooled-credentials'],
                        check=True, timeout=8,
                    )
                    assert len(socks.handshakes) == handshakes_before + 1, 'authenticated tunnel was not reused'
                    socks.authenticated = False
                    socks.reject = True
                    assert request(socks5_destination, proxy=socks5_proxy, verify=not tls) == (
                        499, 'An error occurred while establishing a connection to the proxy tunnel.'
                    )
                finally:
                    socks.shutdown()
                    socks_worker.join()
            if tls:
                assert request('/finish') == (418, 'A\nB\nC\nD\nE')
                assert request('/tls-redirect') == (418, 'A\nB\nC\nD\nE')
                assert request('http://script-proxy.invalid/proxied', proxy=origin) == (
                    202, 'forward proxy'
                )
                server.require_basic_proxy_auth = True
                server.proxy_authorizations = []
                assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True) == (
                    202, 'forward proxy'
                )
                assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
                server.digest_proxy_auth = ('SHA-256', 'auth')
                server.digest_stale = False
                server.proxy_authorizations.clear()
                assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True) == (
                    202, 'forward proxy'
                )
                assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
                for proxy_scheme in ('http', 'https'):
                    with ThreadingHTTPServer(('127.0.0.1', 0), TunnelProxy) as tunnel:
                        if proxy_scheme == 'https':
                            tunnel.socket = context.wrap_socket(tunnel.socket, server_side=True)
                        tunnel.target_port = server.server_port
                        relay = Thread(target=tunnel.serve_forever, daemon=True)
                        relay.start()
                        target = f'https://script-proxy.invalid:{server.server_port}/finish'
                        proxy = f'{proxy_scheme}://127.0.0.1:{tunnel.server_port}'
                        try:
                            status, _ = request(target, proxy=proxy)
                            assert status == 499, proxy_scheme
                            assert request(target, proxy=proxy, verify=False) == (
                                418, 'A\nB\nC\nD\nE'
                            ), proxy_scheme
                            tunnel.require_basic_proxy_auth = True
                            tunnel.proxy_authorizations = []
                            assert request(target, proxy=proxy, verify=False, credentials=True) == (
                                418, 'A\nB\nC\nD\nE'
                            ), proxy_scheme
                            assert len(tunnel.proxy_authorizations) == 2 and tunnel.proxy_authorizations[0] is None
                            tunnel.digest_proxy_auth = ('MD5', 'auth')
                            tunnel.digest_stale = True
                            tunnel.proxy_authorizations.clear()
                            assert request(target, proxy=proxy, verify=False, credentials=True) == (
                                418, 'A\nB\nC\nD\nE'
                            ), proxy_scheme
                            assert len(tunnel.proxy_authorizations) == 3 and tunnel.proxy_authorizations[0] is None
                        finally:
                            tunnel.shutdown()
                            relay.join()
                if sys.platform == 'win32' or '--integrated-proxy' in sys.argv[2:]:
                    for proxy_scheme in ('http', 'https'):
                        with ThreadingHTTPServer(('127.0.0.1', 0), NtlmTunnelProxy) as tunnel:
                            if proxy_scheme == 'https':
                                tunnel.socket = context.wrap_socket(tunnel.socket, server_side=True)
                                tunnel.expected_binding = bytes(16)
                            tunnel.target_port = server.server_port
                            tunnel.completed = 0
                            relay = Thread(target=tunnel.serve_forever, daemon=True)
                            relay.start()
                            try:
                                target = f'https://script-proxy.invalid:{server.server_port}/finish'
                                proxy = f'{proxy_scheme}://127.0.0.1:{tunnel.server_port}'
                                assert request(target, proxy=proxy, verify=False, credentials=True) == (
                                    418, 'A\nB\nC\nD\nE'
                                ), proxy_scheme
                                assert tunnel.completed == 1
                                tunnel.close_first = True
                                assert request(target, proxy=proxy, verify=False, credentials=True) == (
                                    418, 'A\nB\nC\nD\nE'
                                ), proxy_scheme
                                assert tunnel.completed == 2
                            finally:
                                tunnel.shutdown()
                                relay.join()
                        with ThreadingHTTPServer(('127.0.0.1', 0), NtlmForwardProxy) as forwarding:
                            if proxy_scheme == 'https':
                                forwarding.socket = context.wrap_socket(forwarding.socket, server_side=True)
                                forwarding.expected_binding = bytes(16)
                            forwarding.completed = 0
                            relay = Thread(target=forwarding.serve_forever, daemon=True)
                            relay.start()
                            try:
                                proxy = f'{proxy_scheme}://127.0.0.1:{forwarding.server_port}'
                                for close_first in (False, True):
                                    forwarding.close_first = close_first
                                    assert request('http://script-proxy.invalid/proxied', proxy=proxy,
                                                   credentials=True) == (202, 'forward proxy'), proxy_scheme
                                    assert forwarding.completed == 1 + int(close_first)
                                forwarding.close_first = False
                                forwarding.close_second = True
                                assert request('http://script-proxy.invalid/proxied', proxy=proxy,
                                               credentials=True)[0] == 407, proxy_scheme
                                assert forwarding.completed == 2
                                forwarding.close_second = False
                                forwarding.pooled_requests = 0
                                subprocess.run(
                                    [sys.argv[1], '--http-transport-probe', 'http://script-proxy.invalid/ok',
                                     '64', proxy, '', '--pooled-credentials'],
                                    check=True, timeout=8,
                                )
                                assert (forwarding.completed, forwarding.pooled_requests) == (3, 2)
                                if sys.platform == 'win32':
                                    forwarding.auth_scheme = 'Negotiate'
                                    assert request('http://script-proxy.invalid/proxied', proxy=proxy,
                                                   credentials=True) == (202, 'forward proxy'), proxy_scheme
                                    assert forwarding.completed == 4
                            finally:
                                forwarding.shutdown()
                                relay.join()
                        if sys.platform == 'linux':
                            verify_gss_negotiate(request, context, proxy_scheme)
                            verify_gss_negotiate(request, context, proxy_scheme,
                                                 f'https://script-proxy.invalid:{server.server_port}/finish')
                print('PASS: script HTTPS hostname policy and CONNECT over plain TCP and TLS')
                for flag, credentials in (('--kerberos-proxy', 'kerberos'),
                                          ('--kerberos-default-proxy', 'kerberos-default')):
                    if flag in sys.argv[2:]:
                        for proxy_scheme in ('http', 'https'):
                            verify_kerberos_proxy(request, context, proxy_scheme, credentials=credentials)
                            verify_kerberos_proxy(request, context, proxy_scheme,
                                                  f'https://script-proxy.invalid:{server.server_port}/finish',
                                                  credentials=credentials)
                        print(f'PASS: {credentials} proxy forwarding and CONNECT')
                return
            assert request('/start') == (418, 'A\nB\nC\nD\nE')
            server.expected_fixture_header = 'first, kept'
            invalid_header_result = subprocess.check_output(
                [sys.argv[1], '--http-transport-probe', origin + '/start', '64', '--invalid-header-name'],
                text=True, timeout=8,
            )
            assert invalid_header_result.strip() == '418 ' + b'A\nB\nC\nD\nE'.hex()
            del server.expected_fixture_header
            for method, status, body in (
                ('pOsT', 418, 'A\nB\nC\nD\nE'),
                ('propfind', 418, 'A\nB\nC\nD\nE'),
                ('CONNECT', 418, 'authority'),
                ('connect', 499, 'CONNECT request must contain Host header.'),
                ('bad method', 499, 'The format of the HTTP method is invalid.'),
                ('', 499, "The value cannot be an empty string or composed entirely of whitespace. (Parameter 'method')"),
            ):
                output = subprocess.check_output(
                    [sys.argv[1], '--http-transport-probe', origin + '/start', '64', f'--method={method}'],
                    text=True, timeout=8,
                )
                assert output.strip() == f'{status} ' + body.encode().hex(), (method, output)
            for framing, status, expected in (
                (('Transfer-Encoding', 'chunked'), 200, 'opaque\nstream'),
                (('Content-Length', '4'), 200, 'opaq'),
                (('Content-Length', '0'), 200, ''),
                (('Content-Length', '4398041390080'), 200, 'opaque\nstream'),
                (('Content-Length', '2199023255552'), 499,
                 "Specified argument was out of the range of valid values. (Parameter 'millisecondsDelay')"),
            ):
                server.opaque_connect = framing
                output = subprocess.check_output(
                    [sys.argv[1], '--http-transport-probe', origin + '/start', '64', '--method=CONNECT'],
                    text=True, timeout=8,
                )
                assert output.rstrip('\n') == f'{status} ' + expected.encode().hex(), (framing, output)
            del server.opaque_connect
            server.expected_content_type = 'text/custom, text/plain;charset=utf-8'
            result = subprocess.check_output(
                [sys.argv[1], '--http-transport-probe', origin + '/start', '64', '--custom-content-type'],
                text=True, timeout=8,
            )
            assert result.strip() == '418 ' + b'A\nB\nC\nD\nE'.hex(), result
            del server.expected_content_type

            def request_with_length(length, destination=origin + '/start'):
                output = subprocess.check_output(
                    [sys.argv[1], '--http-transport-probe', destination, '64',
                     f'--content-length={length}'], text=True, timeout=8,
                )
                status, body = output.rstrip('\n').split(' ', 1)
                return int(status), bytes.fromhex(body).decode()

            for supplied, expected in [('007', '7, 7'), ('invalid', '7')]:
                server.expected_content_length = expected
                assert request_with_length(supplied) == (418, 'A\nB\nC\nD\nE')
            del server.expected_content_length
            assert request_with_length('3') == (
                499, 'Unable to write content to request stream; content would exceed Content-Length.',
            )
            assert request_with_length('9') == (
                499, 'Sent 7 request content bytes, but Content-Length promised 9.',
            )
            with socket.socket() as unavailable:
                unavailable.bind(('127.0.0.1', 0))
                destination = f'http://127.0.0.1:{unavailable.getsockname()[1]}/start'
                for length in ('7', '3', '9'):
                    status, message = request_with_length(length, destination)
                    assert status == 499 and any(
                        reason in message.lower()
                        for reason in ('refused', 'timeout', 'an error occurred while sending the request')
                    ), (length, status, message)
            assert request('/start', 5) == (418, 'A\nB\ufffd')
            assert request('/blocked') == (499, 'URL from HTTP redirect blocked: http://127.0.0.2:12345/blocked')
            assert request('/unsupported-redirect') == (499, "The 'ftp' scheme is not supported.")
            assert request('/blocked-ftp-redirect') == (
                499, 'URL from HTTP redirect blocked: ftp://127.0.0.2:12345/finish',
            )
            server.reject_redirect_tls = True
            server.rejected_tls_connections = 0
            try:
                assert request('/tls-redirect') == (
                    499, 'The SSL connection could not be established, see inner exception.',
                )
                assert server.rejected_tls_connections == 1
            finally:
                server.reject_redirect_tls = False
            assert request('/no-location') == (499, 'HTTP redirect code but no location header')
            assert request('/cycle') == (499, 'Number of redirects exceeded max redirects')
            assert request('/preserve-308') == (308, 'not redirected')
            assert request('http://script-proxy.invalid/proxied', proxy=f'127.0.0.1:{server.server_port}') == (
                202, 'forward proxy'
            )
            server.require_basic_proxy_auth = True
            server.proxy_authorizations = []
            assert request('http://script-proxy.invalid/proxied', proxy=origin)[0] == 407
            assert server.proxy_authorizations == [None]
            server.proxy_authorizations.clear()
            assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials='environment') == (
                202, 'forward proxy'
            )
            assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
            server.proxy_authorizations.clear()
            proxy_redirect = f'http://script-proxy.invalid:{server.server_port}/proxy-redirect'
            assert request(proxy_redirect, proxy=origin, credentials=True) == (
                418, 'A\nB\nC\nD\nE'
            )
            assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
            server.proxy_authorizations.clear()
            server.digest_proxy_auth = ('SHA-256-sess', 'auth-int')
            server.digest_stale = False
            server.digest_userhash = True
            assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True) == (
                202, 'forward proxy'
            )
            assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
            server.digest_proxy_auth = (None, None)
            server.digest_userhash = False
            server.proxy_authorizations.clear()
            assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True) == (
                202, 'forward proxy'
            )
            assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
            server.digest_proxy_auth = ('SHA-256', 'auth-int')
            server.digest_decoded_body = 'café'.encode()
            for encoding, body, media_type in (
                ('latin1', 'café'.encode('latin1'), 'text/plain; charset="iso-8859-1"'),
                ('utf16-bom', b'\xff\xfe' + 'café'.encode('utf-16-le'), 'text/plain'),
                ('utf32be', b'\x00\x00\xfe\xff' + 'café'.encode('utf-32-be'), 'text/plain; charset=utf-32BE'),
                ('utf8-bom', 'café'.encode('utf-8-sig'), 'text/plain;charset=utf-8'),
            ):
                server.expected_body = body
                server.expected_content_type = media_type
                server.proxy_authorizations.clear()
                assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True,
                               encoding=encoding) == (202, 'forward proxy'), encoding
                assert len(server.proxy_authorizations) == 2 and server.proxy_authorizations[0] is None
            del server.expected_body, server.expected_content_type, server.digest_decoded_body
            server.proxy_authorizations.clear()
            status, error = request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True,
                                    encoding='invalid', maximum=256)
            assert status == 499 and 'invalid character set' in error
            assert server.proxy_authorizations == [None]
            server.digest_proxy_auth = None
            server.proxy_authorizations.clear()
            server.proxy_challenge = 'NTLM !, Basic realm="fixture"'
            assert request('http://script-proxy.invalid/proxied', proxy=origin, credentials=True)[0] == 407
            assert server.proxy_authorizations == [None]
            assert request('/finish', proxy='127.0.0.1:1') == (418, 'A\nB\nC\nD\nE')
            for target, expected in [
                ('http://proxy-bypass.invalid:80/path', '1'),
                ('http://proxy-bypass.invalid:81/path', '0'),
                ('https://proxy-bypass.invalid:8443/path', '1'),
                ('http://single-label/path', '1'),
            ]:
                result = subprocess.check_output(
                    [sys.argv[1], '--http-proxy-bypass-probe', target, origin,
                     r'^HTTP://PROXY-BYPASS\.INVALID$;^https://proxy-bypass\.invalid:8443$'],
                    text=True, timeout=8,
                )
                assert result.strip() == expected, target
            assert request('/body-timeout') == (408, 'Request timeout')
            cancelled = subprocess.check_output(
                [sys.argv[1], '--http-transport-probe', origin + '/body-timeout', '64', '250'],
                text=True, timeout=3,
            )
            assert cancelled == 'cancelled\n'
            with subprocess.Popen(
                [sys.argv[1], '--shared-http-probe', origin], stdout=subprocess.PIPE, text=True,
            ) as probe:
                def release_first():
                    if probe.stdout.readline().strip() == 'queued':
                        server.shared_queued.set()

                release = Thread(target=release_first, daemon=True)
                release.start()
                try:
                    assert probe.wait(timeout=12) == 0
                finally:
                    if probe.poll() is None:
                        probe.kill()
                    release.join()
            assert server.shared_requests == [
                '/shared-first', 'first-finished', '/shared-second', '/shared-final',
                '/shared-restart', '/shared-restart',
            ]
        finally:
            server.shutdown()
            worker.join()
    print('PASS: native script HTTP worker against loopback')


if __name__ == '__main__':
    main()
