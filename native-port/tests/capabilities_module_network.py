"""HTTP admission -> real native capability module; controlled Caps runtime/event hooks."""

import argparse, http.client, json, queue, subprocess, threading, time, urllib.parse
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

    uid = lambda n: '00000000-0000-0000-0000-00000000000' + str(n)
    for mode in ['normal', 'register-failure']:
        config = a.work / (mode + '.ini')
        config.write_text(
            '[Modules]\nSimulationServices=LocalSimulationConnectorModule\n[Admission]\nEnabled=true\nFailRegister='
            + str(mode == 'register-failure')
            + '\n',
            encoding='utf-8',
        )
        process = subprocess.Popen(
            [a.server, str(config)],
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
            check(address.startswith('LISTENING '), 'capability receiver starts')
            port = urllib.parse.urlsplit(address.split(' ', 1)[1]).port

            def request(method, path, value=None):
                c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
                try:
                    c.request(
                        method,
                        path,
                        body=None if value is None else json.dumps(value).encode(),
                        headers={'Content-Type': 'application/json'},
                    )
                    r = c.getresponse()
                    return r.status, r.read()
                finally:
                    c.close()

            def stats():
                return json.loads(request('GET', '/stats')[1])['a']

            base = {
                'agent_id': uid(8),
                'session_id': uid(7),
                'secure_session_id': uid(8),
                'circuit_code': '12348',
                'destination_uuid': uid(1),
                'teleport_flags': 128,
                'first_name': 'Test',
                'last_name': 'User',
                'viewer': 'Viewer 1',
                'caps_path': 'first',
                'children_seeds': [{'handle': '256', 'seed': 'http://child/seed'}],
            }
            status, body = request('POST', '/agent/' + uid(8) + '/', base)
            s = stats()
            if mode == 'register-failure':
                check(
                    status == 500
                    and s['caps_objects'] == 1
                    and s['caps_created'] == 1
                    and s['caps_registered'] == 1
                    and not s['caps_active']
                    and s['circuits'] == 1,
                    'registration callback failure retains inserted inactive caps and optimistic circuit',
                )
                continue
            check(
                status == 200
                and json.loads(body)['success']
                and s['caps_objects'] == 1
                and s['caps_created'] == 1
                and s['caps_registered'] == 1
                and s['caps_active'],
                'native module creates registers and activates capabilities',
            )
            check(
                s['stored_caps_path'] == 'first'
                and s['caps_path'] == 'first'
                and s['caps_children'] == 1,
                'native module stores actual circuit path and child seeds without rewriting them',
            )
            status, body = request('POST', '/agent/' + uid(8) + '/', base)
            s = stats()
            check(
                status == 200
                and json.loads(body)['success']
                and s['caps_objects'] == 1
                and s['caps_created'] == 1
                and s['caps_registered'] == 1
                and s['activated'] == 2,
                'same circuit and path reuse capability object without registration callback',
            )
            status, body = request(
                'POST', '/agent/' + uid(8) + '/', {**base, 'caps_path': 'second'}
            )
            s = stats()
            check(
                status == 200
                and json.loads(body)['success']
                and s['caps_objects'] == 1
                and s['caps_created'] == 2
                and s['caps_registered'] == 2
                and s['handlers_deregistered'] == 1
                and s['caps_disposed'] == 0,
                'path replacement deregisters old handlers without disposing old caps object',
            )
            check(
                s['stored_caps_path'] == 'second' and s['caps_active'],
                'replacement path is activated',
            )
            status, body = request('DELETE', '/agent/' + uid(8) + '/' + uid(1) + '/?auth=token')
            deadline = time.monotonic() + 5
            while stats()['caps_objects'] and time.monotonic() < deadline:
                time.sleep(0.01)
            s = stats()
            check(
                status == 200
                and s['circuits'] == 0
                and s['caps_objects'] == 0
                and s['caps_deregistered'] == 1
                and s['caps_disposed'] == 1,
                'queued native close removes module object and disposes capabilities',
            )
            check(
                s['stored_caps_path'] == 'second' and s['caps_children'] == 0,
                'source removal retains caps path while dropping child-seed state',
            )
            check(
                request('GET', '/ready')[0] == 503,
                'controlled Caps runtime does not claim seed endpoint or world parity',
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
            thread.join(timeout=5)
            if process.returncode:
                raise RuntimeError(process.stderr.read())
    (a.work / 'capabilities-module-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Actual native capability module via HTTP/local routing/admission/circuits and controlled Caps object/event/console backend. Concrete Caps/seed HTTP handlers and full world/platform integration remain pending.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} capabilities module HTTP checks')


if __name__ == '__main__':
    main()
