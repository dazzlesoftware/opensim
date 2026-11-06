"""Exercise the actual native authentication handler and SQLite service over TCP."""

import argparse, base64, contextlib, hashlib, http.client, json, queue, sqlite3, subprocess, tempfile, threading, urllib.parse, uuid
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    exe = str(Path(a.native).resolve())
    checks = []

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

    with tempfile.TemporaryDirectory(prefix='auth-api-', dir=a.work) as folder:
        root = Path(folder)
        config = root / 'server.ini'
        dbpath = root / 'accounts.db'
        settings = '''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=accounts.db
AuthenticationDatabase=accounts.db
EnableAuthenticationEndpoint=true
[Network]
http_listener_port=0
[LoginService]
RequireInventory=false
[AuthenticationService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=auth-service
HttpAuthPassword=service-secret
'''
        config.write_text(settings, encoding='utf-8')
        for options, password in [
            (['--init-db'], None),
            (['--create-user', 'Auth', 'User'], 'original-password\n'),
        ]:
            r = subprocess.run(
                [exe, '--config', str(config), *options],
                input=password,
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=20,
            )
            check(r.returncode == 0, 'CLI ' + options[0] + ': ' + r.stderr)
            if password:
                check(
                    password.strip() not in r.stdout + r.stderr, 'CLI keeps password out of output'
                )
        with contextlib.closing(sqlite3.connect(dbpath)) as db, db:
            principal = db.execute('SELECT PrincipalID FROM UserAccounts').fetchone()[0]
            db.execute('ALTER TABLE auth RENAME TO AuthRealm')
        settings += 'Realm=AuthRealm\n'

        class Running:
            def __init__(self):
                self.process = subprocess.Popen(
                    [exe, '--config', str(config), '--console'],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding='utf-8',
                )
                self.lines = queue.Queue()

                def read():
                    for line in self.process.stdout:
                        self.lines.put(line.strip())

                self.reader = threading.Thread(target=read, daemon=True)
                self.reader.start()
                try:
                    while True:
                        line = self.lines.get(timeout=20)
                        if line.startswith('LISTENING '):
                            self.port = urllib.parse.urlsplit(line.split(' ', 1)[1]).port
                            break
                except BaseException:
                    self.process.kill()
                    self.process.wait()
                    raise

            def request(
                self, body='', path='/auth/plain', method='POST', authorized=True, script=False
            ):
                headers = {'Content-Type': 'application/x-www-form-urlencoded'}
                if authorized:
                    headers['Authorization'] = (
                        'Basic ' + base64.b64encode(b'auth-service:service-secret').decode()
                    )
                if script:
                    headers['X-SecondLife-Shard'] = 'fixture'
                connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
                try:
                    connection.request(method, path, body, headers)
                    response = connection.getresponse()
                    return response.status, response.read()
                finally:
                    connection.close()

            def call(self, method, **values):
                status, body = self.request(
                    urllib.parse.urlencode(dict(METHOD=method, PRINCIPAL=principal, **values))
                )
                check(status == 200, 'auth ' + method + ' HTTP 200')
                return ET.fromstring(body)

            def __enter__(self):
                return self

            def __exit__(self, *_):
                if self.process.poll() is None:
                    self.process.stdin.write('quit\n')
                    self.process.stdin.flush()
                try:
                    self.process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                    raise
                self.reader.join(timeout=3)
                error = self.process.stderr.read()
                check(self.process.returncode == 0, 'clean native auth shutdown: ' + error)
                self.process.stdin.close()
                self.process.stdout.close()
                self.process.stderr.close()

        config.write_text(settings, encoding='utf-8')
        with Running() as server:
            check(server.request(method='GET')[0] == 405, 'authentication endpoint enforces POST')
            check(
                server.request(authorized=False)[0] == 401,
                'authentication endpoint enforces configured Basic authentication',
            )
            check(
                server.request(script=True)[0] == 403,
                'authentication endpoint rejects script-origin requests',
            )
            for method in ['getauthinfo', 'setauthinfo', 'setpassword']:
                check(
                    server.call(method, PASSWORD='blocked').findtext('Result') == 'Failure',
                    'default deny ' + method,
                )
            check(
                server.call('authenticate', PASSWORD='wrong').findtext('Result') == 'Failure',
                'wrong prehash rejected',
            )
            result = server.call(
                'authenticate', PASSWORD=hashlib.md5(b'original-password').hexdigest(), LIFETIME=100
            )
            token = result.findtext('Token')
            check(
                result.findtext('Result') == 'Success' and str(uuid.UUID(token)) == token,
                'source capitalized auth response carries UUID token',
            )
            with contextlib.closing(sqlite3.connect(dbpath)) as db:
                seconds = db.execute(
                    "SELECT (julianday(validity)-julianday('now','localtime'))*86400 FROM tokens WHERE token=?",
                    (token,),
                ).fetchone()[0]
                check(
                    1700 < seconds <= 1800, 'authentication handler clamps lifetime above30 minutes'
                )
            check(
                server.call('verify', TOKEN=token, LIFETIME=100).findtext('Result') == 'Success',
                'token verification through provider',
            )
            check(
                server.call('release', TOKEN=token).findtext('Result') == 'Success',
                'token release through provider',
            )
            check(
                server.call('verify', TOKEN=token).findtext('Result') == 'Failure',
                'released token cannot be renewed',
            )
            for path in ['/auth', '/auth/crypt', '/auth/unknown']:
                check(
                    server.request('LIFETIME=bad', path)[0:2] == (200, b''),
                    'empty source result for ' + path,
                )
            check(
                server.request('LIFETIME=bad')[0] == 500,
                'invalid lifetime propagates through HTTP failure boundary',
            )
            check(
                server.request('METHOD=unknown&LIFETIME=bad')[0] == 500,
                'lifetime conversion precedes mandatory fields',
            )
            check(
                ET.fromstring(
                    server.request('METHOD=authenticate&PRINCIPAL=bad&PASSWORD=x')[1]
                ).findtext('Result')
                == 'Failure',
                'invalid principal returns failure XML',
            )
        config.write_text(
            settings + 'AllowGetAuthInfo=true\nAllowSetAuthInfo=true\nAllowSetPassword=true\n',
            encoding='utf-8',
        )
        with Running() as server:
            info = server.call('getauthinfo').find('result')
            check(
                info.findtext('PrincipalID') == principal
                and info.findtext('AccountType') == 'UserAccount',
                'auth-info returns source DTO fields using configured realm',
            )
            original_hash = info.findtext('PasswordHash')
            key = str(uuid.uuid4())
            check(
                server.call(
                    'setauthinfo', AccountType='Custom<&>試験', WebLoginKey=key, Ignored='discard'
                ).findtext('Result')
                == 'Success',
                'auth-info supports partial updates',
            )
            info = server.call('getauthinfo').find('result')
            check(
                info.findtext('AccountType') == 'Custom<&>試験'
                and info.findtext('PasswordHash') == original_hash
                and info.findtext('WebLoginKey') == key,
                'partial auth-info preserves omitted password fields and XML text',
            )
            check(
                server.call('setpassword', PASSWORD='new-password').findtext('Result') == 'Success',
                'allowed remote password replacement',
            )
            check(
                server.call(
                    'authenticate', PASSWORD=hashlib.md5(b'original-password').hexdigest()
                ).findtext('Result')
                == 'Failure',
                'replaced password is rejected',
            )
            check(
                server.call(
                    'authenticate', PASSWORD=hashlib.md5(b'new-password').hexdigest()
                ).findtext('Result')
                == 'Success',
                'replacement password authenticates',
            )
            info = server.call('getauthinfo').find('result')
            check(
                info.findtext('AccountType') == 'Custom<&>試験'
                and info.findtext('WebLoginKey') == key
                and info.findtext('PasswordHash') != original_hash,
                'SetPassword preserves auth-info metadata and web key',
            )
            check(
                server.call('authenticate', PASSWORD=key).findtext('Result') == 'Success',
                'updated web key authenticates once',
            )
            check(
                server.call('authenticate', PASSWORD=key).findtext('Result') == 'Failure',
                'updated web key cannot be reused',
            )
            check(
                server.call('getauthinfo').findtext('result/WebLoginKey') == str(uuid.UUID(int=0)),
                'web key consumption persists through auth-info retrieval',
            )
            for method in ['getauthinfo', 'setauthinfo']:
                status, body = server.request(
                    urllib.parse.urlencode(
                        dict(METHOD=method, PRINCIPAL=str(uuid.uuid4()), AccountType='missing')
                    )
                )
                check(
                    status == 200 and ET.fromstring(body).findtext('Result') == 'Failure',
                    'handler does not synthesize missing auth-info for ' + method,
                )
        with contextlib.closing(sqlite3.connect(dbpath)) as db:
            check(
                db.execute(
                    'SELECT accountType,webLoginKey FROM AuthRealm WHERE UUID=?', (principal,)
                ).fetchone()
                == ('Custom<&>試験', str(uuid.UUID(int=0))),
                'auth-info changes persist across process shutdown',
            )
            check(
                db.execute('SELECT name,version FROM migrations ORDER BY name').fetchall()
                == [('AuthStore', 2), ('UserAccount', 3), ('migrations', 1)],
                'auth API preserves independent migration metadata',
            )
    report = dict(
        passed=True,
        checks=len(checks),
        observations=checks,
        scope='Actual native HTTP/SQLite authentication API, Basic/script gates, default-denied administrative flags, lifetime cap, token issuance/verify/release, custom realm, auth-info/password/web-key persistence and process restart. HTTP-only fixture; actual Unix and full application parity remain unverified.',
    )
    (a.work / 'authentication-network-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    print(f'PASS: {len(checks)} native authentication HTTP/persistence checks')


if __name__ == '__main__':
    main()
