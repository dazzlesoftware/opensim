"""Run the native proxy fixtures against a disposable MIT Kerberos realm."""

import os
import ctypes
import ssl
from hashlib import sha256
from pathlib import Path
import socket
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
from time import monotonic, sleep
from script_http_transport_network import GssNegotiateForwardProxy, ThreadingHTTPServer


def verify_reconnected_origin(probe, openssl, root, env):
    fixture = Path(__file__).with_name('fixtures')
    original = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    original.load_cert_chain(fixture / 'localhost-cert.pem', fixture / 'localhost-key.pem')
    certificate, key = root / 'origin.pem', root / 'origin-key.pem'
    subprocess.run(
        [openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
         '-subj', '/CN=localhost', '-days', '1', '-keyout', key, '-out', certificate],
        check=True, capture_output=True, timeout=10,
    )
    replacement = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    replacement.load_cert_chain(certificate, key)
    endpoint = ssl.PEM_cert_to_DER_cert(certificate.read_text())

    class Origin(GssNegotiateForwardProxy):
        auth_header = 'Authorization'
        challenge_header = 'WWW-Authenticate'
        challenge_status = 401

        def do_GET(self):
            assert self.path == '/auth-kerberos'
            if not self.headers.get(self.auth_header):
                self.challenge('Negotiate', b'reconnect', close=True)
            elif self.authenticate():
                self.send_response(200)
                self.send_header('Content-Length', '2')
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(b'ok')
                self.close_connection = True

    class RotatingOrigin(ThreadingHTTPServer):
        connections = 0

        def get_request(self):
            connection, address = super().get_request()
            context = original if self.connections == 0 else replacement
            self.connections += 1
            return context.wrap_socket(connection, server_side=True), address

    with RotatingOrigin(('127.0.0.1', 0), Origin) as server:
        server.completed = 0
        server.require_kerberos = server.send_final_token = True
        server.channel_binding = b'tls-server-end-point:' + sha256(endpoint).digest()
        server.gss = ctypes.CDLL('libgssapi_krb5.so.2')
        server.gss.gss_accept_sec_context.restype = ctypes.c_uint32
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = subprocess.run([probe], input=f'https://localhost:{server.server_port}/auth-kerberos\n',
                                    env=env, text=True, capture_output=True, check=True, timeout=15)
            assert result.stdout.strip() == 'ok:6f6b', result
            assert server.connections == 2 and server.completed == 1
        finally:
            server.shutdown()
            worker.join()
    print('PASS Kerberos origin binds its first token to the replacement TLS certificate')


def main():
    native, database_tool, admin, kdc, kinit, *options = sys.argv[1:]
    with TemporaryDirectory(prefix='opensim-kerberos-') as directory:
        root = Path(directory)
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        config = root / 'krb5.conf'
        config.write_text(f'''[libdefaults]
 default_realm = OPENSIM.TEST
 dns_lookup_kdc = false
 dns_lookup_realm = false
 dns_canonicalize_hostname = false
 rdns = false
[realms]
 OPENSIM.TEST = {{
  kdc = 127.0.0.1:{port}
 }}
''')
        profile = root / 'kdc.conf'
        profile.write_text(f'''[kdcdefaults]
 kdc_listen = 127.0.0.1:{port}
 kdc_tcp_listen = 127.0.0.1:{port}
[realms]
 OPENSIM.TEST = {{
  database_name = {root / 'principal'}
  key_stash_file = {root / 'stash'}
 }}
''')
        keytab = root / 'proxy.keytab'
        env = dict(os.environ, KRB5_CONFIG=str(config), KRB5_KDC_PROFILE=str(profile),
                   KRB5_KTNAME=f'FILE:{keytab}', KRB5CCNAME=f'FILE:{root / "tickets"}',
                   OPENSIM_KERBEROS_PROXY_HOST='localhost', OPENSIM_KERBEROS_PROXY_BIND='127.0.0.1')
        subprocess.run([database_tool, 'create', '-s', '-P', 'fixture-master'], env=env, check=True)
        for command in ('addprinc -pw fixture-password fixture', 'addprinc -randkey HTTP/localhost',
                        f'ktadd -k {keytab} HTTP/localhost'):
            subprocess.run([admin, '-p', 'fixture/admin@OPENSIM.TEST', '-q', command],
                           env=env, check=True, timeout=5)

        with (root / 'kdc.log').open('w+') as log:
            process = subprocess.Popen([kdc, '-n'], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = monotonic() + 5
                while True:
                    if process.poll() is not None or monotonic() >= deadline:
                        log.seek(0)
                        raise RuntimeError(f'Kerberos test realm failed to start: {log.read()}')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=0.2):
                            break
                    except OSError:
                        sleep(0.05)
                if options:
                    subprocess.run([sys.executable, __file__, '--verify-origin', native, *options, str(root)],
                                   env=env, check=True, timeout=30)
                else:
                    fixture = Path(__file__).with_name('script_http_transport_network.py')
                    subprocess.run([sys.executable, fixture, native, '--tls', '--kerberos-proxy'],
                                   env=env, check=True, timeout=25)
                    subprocess.run([kinit, 'fixture@OPENSIM.TEST'], input='fixture-password\n',
                                   text=True, env=env, check=True, timeout=5)
                    subprocess.run([sys.executable, fixture, native, '--tls', '--kerberos-default-proxy'],
                                   env=env, check=True, timeout=25)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == '__main__':
    if sys.argv[1] == '--verify-origin':
        probe, openssl, directory = sys.argv[2:]
        verify_reconnected_origin(probe, openssl, Path(directory), os.environ)
    else:
        main()
