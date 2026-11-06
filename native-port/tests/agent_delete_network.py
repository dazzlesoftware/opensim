"""Native DELETE routing/auth, queued removal, and outgoing connector interop."""

import argparse, http.client, json, queue, subprocess, threading, time, urllib.parse
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--client', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

    process = subprocess.Popen(
        [a.server, 'admission'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    lines = queue.Queue()

    def read():
        for line in process.stdout:
            lines.put(line.strip())

    thread = threading.Thread(target=read, daemon=True)
    thread.start()
    try:
        address = lines.get(timeout=20)
        check(address.startswith('LISTENING '), 'receiver starts')
        url = address.split(' ', 1)[1]
        port = urllib.parse.urlsplit(url).port
        agent = '00000000-0000-0000-0000-000000000007'
        region = '00000000-0000-0000-0000-000000000008'
        path = '/agent/' + agent + '/' + region + '/'

        def request(target, method='DELETE', body=None, headers=None):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                connection.request(method, target, body=body, headers=headers or {})
                response = connection.getresponse()
                return response.status, dict(response.getheaders()), response.read()
            finally:
                connection.close()

        def stats():
            return json.loads(request('/stats', 'GET')[2])

        def await_count(key, count):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                result = stats()
                if result[key] >= count:
                    return result
                time.sleep(0.01)
            raise AssertionError('queued ' + key + ' not completed')

        def create():
            data = {
                'agent_id': agent,
                'session_id': region,
                'secure_session_id': agent,
                'circuit_code': '123456',
                'destination_uuid': region,
                'teleport_flags': 128,
            }
            status, _, body = request(
                '/agent/' + agent + '/',
                'POST',
                json.dumps(data).encode(),
                {'Content-Type': 'application/json'},
            )
            check(
                status == 200 and json.loads(body)['success'],
                'native POST creates circuit before lifecycle request',
            )
            check(
                stats()['circuits'] == 1 and stats()['authenticated'] is True,
                'circuit credentials valid before removal',
            )

        queries = [
            ('', None),
            ('?auth=', ''),
            ('?auth=a+b%2Bc%20d', 'a b+c d'),
            ('?auth=a=b=c', 'a=b=c'),
            ('?auth=one&Auth=two', 'one,two'),
            ('?Auth=one&auth=two', None),
            ('?auth=%u0041', '%u0041'),
            ('?auth=%E8%A9%A6%E9%A8%93', '試験'),
        ]
        for index, (query, auth) in enumerate(queries):
            create()
            before = stats()['closes']
            status, headers, body = request(path + query, body=b'not JSON')
            check(
                status == 200 and body == ('OpenSim agent ' + agent).encode(),
                'close ' + str(index) + ' exact response ignores non-JSON body',
            )
            check(
                headers.get('Connection', '').lower() == 'close'
                and headers.get('Content-Type') == 'application/json',
                'close ' + str(index) + ' source response metadata',
            )
            result = await_count('closes', before + 1)
            check(
                result['circuits'] == 0
                and result['last_agent'] == agent
                and result['last_region'] == region,
                'close ' + str(index) + ' queued callback removes correct native circuit',
            )
            check(
                result['last_auth_null'] == (auth is None) and result['last_auth'] == (auth or ''),
                'close ' + str(index) + ' original query auth value',
            )
        create()
        before = stats()['releases']
        status, _, body = request(path + 'release/?auth=ignored')
        result = await_count('releases', before + 1)
        check(
            status == 200
            and result['last_auth'] == ''
            and result['last_auth_null'] is False
            and result['circuits'] == 1,
            'exact release action passes empty URI and retains controlled circuit',
        )
        before = stats()['closes']
        status, _, _ = request(
            path + 'Release/?auth=token', headers={'X-SecondLife-Shard': 'fixture'}
        )
        result = await_count('closes', before + 1)
        check(
            status == 200 and result['last_auth'] == 'token' and result['circuits'] == 0,
            'case-sensitive action and shard non-return behavior',
        )
        before = stats()['closes'] + stats()['releases']
        check(
            request('/agent/bad/')[0] == 404
            and request('/agent/' + agent + '/')[0] == 400
            and request('/agent/' + agent + '/00000000-0000-0000-0000-000000000000/')[0] == 400,
            'path and nonzero region gates precede lifecycle',
        )
        check(
            stats()['closes'] + stats()['releases'] == before,
            'invalid requests do not dispatch lifecycle',
        )
        create()
        before = stats()
        run = subprocess.run(
            [a.client, url + '/', 'receiver'], capture_output=True, text=True, timeout=20
        )
        check(
            run.returncode == 0 and run.stdout == '1|1',
            'outgoing native connector release/close complete',
        )
        result = await_count('closes', before['closes'] + 1)
        check(
            result['releases'] == before['releases'] + 1
            and result['circuits'] == 0
            and result['last_auth'] == 'close+token',
            'native connector interoperates with original DELETE routing and auth decode',
        )
        check(
            request('/ready', 'GET')[0] == 503,
            'controlled lifecycle fixture does not claim scene readiness',
        )
    finally:
        if process.poll() is None:
            process.stdin.write('quit\n')
            process.stdin.flush()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        if process.returncode:
            raise RuntimeError(process.stderr.read())
    (a.work / 'agent-delete-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Controlled native HTTP receiver and circuit manager, queued close callbacks; actual scene transfer/removal and full ProcessRequest/URI host differential remain pending',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} native DELETE routing socket checks')


if __name__ == '__main__':
    main()
