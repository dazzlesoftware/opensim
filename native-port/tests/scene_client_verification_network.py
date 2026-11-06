"""Seed -> native scene verification -> real circuit manager/presence service cleanup."""

import argparse, http.client, json, queue, subprocess, threading
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen(
        [a.server], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    q = queue.Queue()
    checks = []

    def reader():
        for line in process.stdout:
            q.put(line.strip())

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        line = q.get(timeout=20)
        assert line.startswith('LISTENING '), line
        port = int(line.split()[1])

        def request(path, method='GET', body=None):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                connection.request(
                    method, path, body=body, headers={'Content-Type': 'application/llsd+xml'}
                )
                response = connection.getresponse()
                return response.status, response.read()
            finally:
                connection.close()

        def check(ok, name):
            assert ok, name
            checks.append(name)

        def control(op, body=''):
            check(request('/control/' + op, 'POST', body)[0] == 200, 'control ' + op)

        def state():
            return json.loads(request('/state')[1])

        def seed():
            return request(
                '/CAPS/seed0000', 'POST', b'<llsd><array><string>A</string></array></llsd>'
            )[0]

        control('activate')
        s = state()
        check(
            s['circuits'] == 1 and s['online'] and s['present'],
            'fixture begins with real admitted circuit and presence-service session',
        )
        check(
            seed() == 200 and state()['verifications'] == 1,
            'Hypergrid login invokes native verification module and accepts registered circuit',
        )
        control('remove-circuit')
        before = state()
        check(
            seed() == 403
            and state()['verifications'] == before['verifications']
            and state()['online'],
            'missing circuit rejects seed without invoking verifier or logging out unrelated retained session',
        )
        control('admit')
        control('deny')
        before = state()
        check(seed() == 403, 'Hypergrid verifier rejection reaches seed authorization')
        s = state()
        check(
            s['circuits'] == 0
            and not s['online']
            and not s['present']
            and s['closed'] == before['closed'] + 1,
            'native rejection logs out actual presence service, closes actor callback and removes circuit',
        )
        control('admit')
        control('no-module')
        before = state()
        check(
            seed() == 200
            and state()['verifications'] == before['verifications']
            and state()['circuits'] == 1,
            'source Hypergrid path accepts when optional verifier module is absent',
        )
        control('module')
        control('flags', '128')
        before = state()
        check(
            seed() == 200 and state()['verifications'] == before['verifications'],
            'regular login preserves source bypass of client-IP verifier',
        )
        control('flags', '0')
        before = state()
        check(
            seed() == 200 and state()['verifications'] == before['verifications'],
            'non-login incoming circuit preserves source direct acceptance',
        )
        control('flags', str(0x40000080))
        control('no-service')
        before = state()
        check(seed() == 403, 'Hypergrid flag takes precedence over regular login flag')
        s = state()
        check(
            s['circuits'] == 1
            and s['online']
            and s['present']
            and s['closed'] == before['closed']
            and s['cleanup_errors'] == before['cleanup_errors'] + 1,
            'missing presence service catches cleanup failure and retains circuit/source partial state',
        )
        control('service')
        control('close-fail')
        before = state()
        check(seed() == 403, 'close callback failure still rejects authorization')
        s = state()
        check(
            s['circuits'] == 1
            and not s['online']
            and s['present']
            and s['cleanup_errors'] == before['cleanup_errors'] + 1,
            'logout succeeds before close failure and later circuit removal is skipped',
        )
        control('close-ok')
        before = state()
        check(
            not before['online'] and before['circuits'] == 1,
            'cleanup retry begins with an already logged-out session and retained circuit',
        )
        check(
            seed() == 403
            and state()['circuits'] == 0
            and not state()['online']
            and state()['closed'] == before['closed'] + 1,
            'subsequent complete rejection cleans up retained partial state',
        )
        control('allow')
        control('admit')
        check(
            seed() == 200 and state()['online'] and state()['circuits'] == 1,
            'new admission restores successful seed access with same native policy',
        )
        check(
            request('/ready')[0] == 503,
            'controlled HG verifier/actor callbacks do not claim full simulator readiness',
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
        t.join(timeout=5)
        if process.returncode:
            raise RuntimeError(process.stderr.read())
    (a.work / 'scene-client-verification-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Actual native registry/seed handler/route/Caps activation/scene verification/circuit manager/presence store+service. Controlled HG verifier and scene presence/client-close callbacks; complete production module/world/viewer/platform remains pending.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} scene client verification socket checks')


if __name__ == '__main__':
    main()
