"""Native HTTP agent handlers -> actual local connector -> controlled scene adapters."""

import argparse, gzip, http.client, json, queue, subprocess, threading, time, urllib.parse
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--data-client', required=True)
    p.add_argument('--position-client', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []
    config = a.work / 'local.ini'
    config.write_text(
        '[Modules]\nSimulationServices = LocalSimulationConnectorModule\n', encoding='utf-8'
    )

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

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
        check(address.startswith('LISTENING '), 'local connector receiver starts')
        url = address.split(' ', 1)[1] + '/'
        port = urllib.parse.urlsplit(url).port
        uid = lambda n: '00000000-0000-0000-0000-00000000000' + str(n)
        path = '/agent/' + uid(7) + '/'

        def request(method, target=path, value=None, compressed=False):
            body = None if value is None else json.dumps(value).encode()
            headers = {'Content-Type': 'application/json'}
            if compressed:
                body = gzip.compress(body)
                headers['X-Content-Encoding'] = 'gzip'
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                connection.request(method, target, body=body, headers=headers)
                response = connection.getresponse()
                return response.status, response.read()
            finally:
                connection.close()

        def stats():
            return json.loads(request('GET', '/stats')[1])

        check(
            stats()['enabled'] is True
            and stats()['scenes'] == 2
            and stats()['a']['registered']
            and stats()['b']['registered'],
            'source module configuration registers two scene interfaces',
        )
        query = {'my_version': 'SIMULATION/0.4', 'position': '<1, 2, 30>'}
        status, body = request('QUERYACCESS', path + uid(1) + '/', query)
        check(
            status == 200
            and json.loads(body)['success'] is True
            and stats()['a']['queries'] == 1
            and stats()['b']['queries'] == 0,
            'query routes only to destination scene',
        )
        status, body = request('QUERYACCESS', path + uid(5) + '/', query)
        check(
            status == 200
            and json.loads(body)['success'] is False
            and json.loads(body)['reason'] == 'Communications failure',
            'missing query destination preserves source failure reason',
        )
        circuit = {
            'agent_id': uid(7),
            'session_id': uid(8),
            'secure_session_id': uid(7),
            'circuit_code': '123456',
            'destination_uuid': uid(1),
            'teleport_flags': 128,
        }
        status, body = request('POST', value=circuit, compressed=True)
        check(
            status == 200
            and json.loads(body)['success'] is True
            and stats()['a']['circuits'] == 1
            and stats()['b']['circuits'] == 0,
            'create routes to selected scene and hands actual native circuit credentials',
        )
        check(
            stats()['a']['authenticated'] is True, 'local admission retains authenticatable circuit'
        )
        status, body = request(
            'POST', value={**circuit, 'destination_uuid': uid(5), 'destination_name': 'Missing'}
        )
        check(
            status == 200 and json.loads(body)['reason'] == 'Did not find region Missing',
            'missing create destination preserves local service reason',
        )
        data = {
            'message_type': 'AgentData',
            'agent_uuid': uid(7),
            'destination_uuid': uid(1),
            'position': '<3, 4, 30>',
            'attach_objects': [{'sog': '<object/>', 'extra': 'extra', 'state': 'scripts'}],
        }
        status, body = request('PUT', value=data, compressed=True)
        s = stats()
        check(
            status == 200
            and body == b'True'
            and s['a']['data'] == 1
            and s['b']['data'] == 0
            and s['a']['deserialized'] == 1,
            'full update/deserialization routes to selected local scene',
        )
        status, body = request('PUT', value={**data, 'destination_uuid': uid(5)})
        s = stats()
        check(
            status == 200
            and body == b'False'
            and s['a']['deserialized'] == 2
            and s['a']['data'] == 1
            and s['b']['data'] == 0,
            'unknown scene getter fallback deserializes on first scene before missing update is rejected',
        )
        status, body = request(
            'PUT',
            value={
                'message_type': 'AgentPosition',
                'agent_uuid': uid(7),
                'destination_uuid': uid(5),
                'position': '<9, 8, 7>',
            },
            compressed=True,
        )
        s = stats()
        check(
            status == 200
            and body == b'True'
            and s['a']['positions'] == 1
            and s['b']['positions'] == 1,
            'position broadcasts to every local scene despite unknown destination and false callback',
        )
        check(
            s['a']['circuit_position'] == '<9, 8, 7>' and s['a']['authenticated'] is True,
            'broadcast reaches real circuit storage',
        )
        status, body = request('DELETE', path + uid(1) + '/release/')
        s = stats()
        check(
            status == 200
            and body == ('OpenSim agent ' + uid(7)).encode()
            and s['a']['arrived'] == 1
            and s['b']['arrived'] == 0,
            'release routes origin to entity-transfer arrival callback',
        )
        status, body = request('DELETE', path + uid(1) + '/?auth=token%2Bvalue')
        deadline = time.monotonic() + 5
        while stats()['a']['closed'] == 0 and time.monotonic() < deadline:
            time.sleep(0.01)
        s = stats()
        check(
            status == 200
            and body == ('OpenSim agent ' + uid(7)).encode()
            and s['a']['closed'] == 1
            and s['a']['auth'] == 'token+value'
            and s['a']['circuits'] == 0
            and s['b']['closed'] == 0,
            'queued close routes to scene, ignores false return and removes native circuit',
        )
        for client, key in [(a.data_client, 'data'), (a.position_client, 'positions')]:
            before = stats()
            run = subprocess.run([client, url], capture_output=True, text=True, timeout=30)
            check(
                run.returncode == 0,
                key + f' native client completes (exit={run.returncode}, stderr={run.stderr!r})',
            )
            try:
                result = json.loads(run.stdout)
            except json.JSONDecodeError as error:
                raise AssertionError(
                    f'{key} native client returned invalid JSON: executable={client!r}, exit={run.returncode}, stdout={run.stdout!r}, stderr={run.stderr!r}'
                ) from error
            if key == 'data':
                check(
                    result == {'first': False, 'second': False, 'failed': 0}
                    and stats()['a']['data'] == before['a']['data']
                    and stats()['b']['data'] == before['b']['data'],
                    'full connector targets its unavailable region and preserves local rejection without cooldown',
                )
            else:
                check(
                    result == {'first': True, 'second': True, 'failed': 0}
                    and stats()['a']['positions'] == before['a']['positions'] + 2
                    and stats()['b']['positions'] == before['b']['positions'] + 2,
                    'position connector interoperates with local broadcast and ignores false scene result',
                )
        check(
            request('GET', '/ready')[0] == 503,
            'controlled scene callbacks do not claim full world/physics readiness',
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
    (a.work / 'local-simulation-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Native local connector and HTTP handlers with controlled scene/entity callbacks, actual circuit manager and native clients; no actual world scene/physics/script/viewer proof',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} local simulation HTTP integration checks')


if __name__ == '__main__':
    main()
