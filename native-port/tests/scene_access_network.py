"""HTTP query -> local connector -> native scene access policy with controlled world dependencies."""

import argparse, http.client, json, queue, subprocess, threading, urllib.parse
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
    for mode in ['public', 'private', 'no-crossing']:
        config = a.work / (mode + '.ini')
        config.write_text(
            '[Modules]\nSimulationServices = LocalSimulationConnectorModule\n[Access]\nEnabled=true\nPrivate='
            + str(mode == 'private')
            + '\nCrossing='
            + str(mode != 'no-crossing')
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
            check(address.startswith('LISTENING '), mode + ' receiver starts')
            port = urllib.parse.urlsplit(address.split(' ', 1)[1]).port

            def request(method, path, body=None):
                c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
                try:
                    c.request(
                        method,
                        path,
                        body=None if body is None else json.dumps(body).encode(),
                        headers={'Content-Type': 'application/json'},
                    )
                    r = c.getresponse()
                    return r.status, r.read()
                finally:
                    c.close()

            def query(agent=8, region=1, teleport=True):
                target = uid(agent) if isinstance(agent, int) else agent
                status, body = request(
                    'QUERYACCESS',
                    '/agent/' + target + '/' + uid(region) + '/',
                    {
                        'my_version': 'SIMULATION/0.4',
                        'position': '<1, 2, 30>',
                        'viaTeleport': teleport,
                        'features': [uid(8)],
                    },
                )
                check(status == 200, 'query returns negotiated HTTP response')
                return json.loads(body)

            def result(agent, allowed, reason='', region=1, teleport=True):
                value = query(agent, region, teleport)
                check(
                    value['success'] == allowed
                    and value['reason'] == reason
                    and value['features'] == [uid(8)],
                    mode + ' access decision ' + str(agent) + ' region ' + str(region),
                )

            result(1, True, region=2, teleport=False)
            result(3, False, 'authorization denied')
            result(9, False, 'Error authorizing agent: fixture authorization')
            result(
                4, False, 'Denied access to region Scene1: You have been banned from that region.'
            )
            result('6571e388-6218-4574-87db-f9379718315e', False)
            if mode == 'private':
                result(5, True)
                result(
                    8,
                    False,
                    'Denied access to private region Scene1: You do not have access to that region.',
                )
            else:
                result(8, True)
                s = json.loads(request('GET', '/stats')[1])
                check(
                    s['a']['access_existing'] is False
                    and s['a']['access_first'] == ''
                    and s['a']['access_last'] == '',
                    'unknown agent uses synthetic circuit with empty names',
                )
                circuit = {
                    'agent_id': uid(8),
                    'session_id': uid(7),
                    'secure_session_id': uid(8),
                    'circuit_code': '12345',
                    'destination_uuid': uid(1),
                    'first_name': 'Existing',
                    'last_name': 'Agent',
                }
                status, body = request('POST', '/agent/' + uid(8) + '/', circuit)
                check(
                    status == 200 and json.loads(body)['success'],
                    'native circuit admitted by controlled scene',
                )
                result(8, True)
                s = json.loads(request('GET', '/stats')[1])
                check(
                    s['a']['access_existing']
                    and s['a']['access_first'] == 'Existing'
                    and s['a']['access_last'] == 'Agent',
                    'access policy reuses actual circuit manager object',
                )
                result(8, False, 'The region is full', region=2)
                result(2, True, region=2)
                result(6, False, 'You dont have access to the region parcels')
                result(7, True)
                if mode == 'no-crossing':
                    result(8, False, 'Region Crossing not allowed', teleport=False)
                else:
                    result(6, False, 'You are banned from the parcel', teleport=False)
                    result(7, False, 'The parcel is restricted', teleport=False)
            check(
                request('GET', '/ready')[0] == 503,
                'controlled access dependencies do not claim a complete world',
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
    (a.work / 'scene-access-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Native HTTP/local service/scene policy with actual circuit manager and controlled permissions, authorization, estate, groups, capacity and parcels. Full concrete world services/admission/viewer/platform behavior remain pending.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} scene access HTTP checks')


if __name__ == '__main__':
    main()
