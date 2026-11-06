"""Real authentication connector forms, headers, response decoding and reinitialization."""

import argparse, base64, http.server, json, subprocess, tempfile, threading, urllib.parse
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

    principal = '00000000-0000-0000-0000-000000000007'
    zero = '00000000-0000-0000-0000-000000000000'
    value = 'secret & 試験 😃'
    token = 'token & 試験'

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        profile = 'normal'
        requests = []

        def log_message(self, *args):
            pass

        def handle(self):
            try:
                super().handle()
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
                # Expected after a client discards a reply at its headers. All request,
                # response-value and operation-count assertions still run in the parent.
                if not getattr(self, 'response_started', False):
                    raise

        def do_POST(self):
            body = self.rfile.read(int(self.headers['Content-Length'])).decode()
            self.requests.append((self.path, dict(self.headers), body))
            self.response_started = True
            data = (
                '<ServerResponse><Result>'
                + (
                    'Failure'
                    if self.profile == 'failure'
                    else 'True' if self.profile == 'boolean' else 'Success'
                )
                + '</Result><Token>token &amp; 試験</Token></ServerResponse>'
            ).encode()
            if self.profile == 'redirect' and not self.path.startswith('/redirected'):
                self.send_response(307)
                self.send_header('Location', '/redirected/auth/plain')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            if self.profile == 'utf16':
                data = b'\xff\xfe' + data.decode().encode('utf-16-le')
            if self.profile == 'empty':
                data = b''
            if self.profile == 'malformed':
                data = b'bad xml'
            self.send_response(401 if self.profile == 'unauthorized' else 200)
            self.send_header('Content-Type', 'application/xml')
            if self.profile == 'chunked':
                self.send_header('Transfer-Encoding', 'chunked')
                self.end_headers()
                self.wfile.write(f'{len(data):x}\r\n'.encode() + data + b'\r\n0\r\n\r\n')
            elif self.profile == 'no-length':
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(data)
                self.close_connection = True
            else:
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            with tempfile.TemporaryDirectory(prefix='auth-client-', dir=a.work) as folder:
                root = Path(folder)
                config = root / 'auth.ini'
                base = f'[AuthenticationService]\nAuthenticationServerURI=http://127.0.0.1:{server.server_port}/base/\n'
                config.write_text(
                    base
                    + 'AuthType=BasicHttpAuthentication\nHttpAuthUsername=auth-user\nHttpAuthPassword=auth-password\n',
                    encoding='utf-8',
                )

                def t(s):
                    return s.encode().hex() or '.'

                commands = [f'{mode} 2147483647 {t(principal)} {t(value)} .' for mode in range(7)]
                for profile in [
                    'normal',
                    'failure',
                    'boolean',
                    'utf16',
                    'redirect',
                    'chunked',
                    'no-length',
                    'empty',
                    'malformed',
                    'unauthorized',
                    'sticky-none',
                    'new-basic',
                ]:
                    Handler.profile = profile
                    Handler.requests = []
                    args = [str(config)]
                    credentials = 'auth-user:auth-password'
                    if profile in ['sticky-none', 'new-basic']:
                        second = root / 'second.ini'
                        credentials = (
                            'other-user:other-password' if profile == 'new-basic' else credentials
                        )
                        second.write_text(
                            base
                            + (
                                'AuthType=None\n'
                                if profile == 'sticky-none'
                                else 'AuthType=BasicHttpAuthentication\nHttpAuthUsername=other-user\nHttpAuthPassword=other-password\n'
                            ),
                            encoding='utf-8',
                        )
                        args.append(str(second))
                    r = subprocess.run(
                        [a.native, '--network', *args],
                        input='\n'.join(commands) + '\n',
                        capture_output=True,
                        text=True,
                        encoding='utf-8',
                        timeout=50,
                    )
                    check(r.returncode == 0, profile + ' connector process finishes')
                    rows = r.stdout.splitlines()
                    check(
                        len(rows) == 7
                        and len(Handler.requests) == (8 if profile == 'redirect' else 4),
                        profile + ' only authenticate/verify/release send HTTP',
                    )
                    expected = ['authenticate', 'authenticate', 'verify', 'release']
                    for index, (path, headers, body) in enumerate(Handler.requests):
                        mode = index // 2 if profile == 'redirect' else index
                        fields = urllib.parse.parse_qs(body, keep_blank_values=True)
                        h = {k.lower(): v for k, v in headers.items()}
                        check(
                            path
                            == (
                                '/redirected/auth/plain'
                                if profile == 'redirect' and index % 2
                                else '/base//auth/plain'
                            )
                            and fields['METHOD'] == [expected[mode]],
                            profile + ' URI and method ' + str(index),
                        )
                        expected_auth = (
                            None
                            if profile == 'redirect' and index % 2
                            else 'Basic ' + base64.b64encode(credentials.encode()).decode()
                        )
                        check(
                            h.get('authorization') == expected_auth
                            and h.get('content-type') == 'application/x-www-form-urlencoded',
                            profile + ' Basic/form headers ' + str(index),
                        )
                        check(
                            h.get('connection', '').lower() == 'keep-alive'
                            and h.get('keep-alive') == 'timeout=30, max=10',
                            profile + ' source keepalive ' + str(index),
                        )
                        check(
                            fields['PRINCIPAL'] == [principal]
                            and fields.get('PASSWORD' if mode < 2 else 'TOKEN') == [value],
                            profile + ' principal and Unicode credential ' + str(index),
                        )
                        check(
                            (
                                ('LIFETIME' not in fields)
                                if mode == 3
                                else fields.get('LIFETIME') == ['2147483647']
                            ),
                            profile + ' connector preserves unclamped lifetime ' + str(index),
                        )
                    normal = profile in ['normal', 'utf16', 'redirect', 'sticky-none', 'new-basic']
                    failure = profile in ['failure', 'boolean']
                    prefixes = (
                        [t(token), t(token), 'T', 'T']
                        if normal
                        else ['.', '.', 'F', 'F'] if failure else ['E'] * 4
                    )
                    check(
                        rows[:4]
                        == [
                            '|' + out + '|' + (zero if i == 1 else principal)
                            for i, out in enumerate(prefixes)
                        ],
                        profile + ' strict Result/Token and zero real-ID behavior',
                    )
                    check(
                        rows[4:] == ['|F|' + principal, '|N|' + principal, '|F|' + principal],
                        profile + ' remote administration remains inert',
                    )
        finally:
            server.shutdown()
            worker.join(timeout=5)
    (a.work / 'authentication-connector-network-results.json').write_text(
        json.dumps(
            dict(
                passed=True,
                checks=len(checks),
                observations=checks,
                scope='Native HTTP form transport with real socket server, URI preservation, Basic/keepalive headers, lifetime bytes, redirects, BOMs, no-content-length behavior, strict replies, propagated errors and sticky/replaced Basic auth on reinitialization. Full .NET HttpClient runtime and actual Unix execution remain pending.',
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} authentication connector HTTP checks')


if __name__ == '__main__':
    main()
