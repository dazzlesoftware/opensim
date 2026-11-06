"""Real native PUT routing, full/position updates and both outgoing connectors."""

import argparse, gzip, http.client, json, queue, struct, subprocess, threading, urllib.parse
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
        url = address.split(' ', 1)[1] + '/'
        port = urllib.parse.urlsplit(url).port
        agent = '00000000-0000-0000-0000-000000000007'
        region = agent[:-1] + '8'
        path = '/agent/' + agent[:-1] + '6/'

        def request(body=None, method='PUT', target=path, headers=None):
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                c.request(
                    method,
                    target,
                    body=body,
                    headers={'Content-Type': 'application/json', **(headers or {})},
                )
                r = c.getresponse()
                return r.status, dict(r.getheaders()), r.read()
            finally:
                c.close()

        def stats():
            return json.loads(request(method='GET', target='/stats')[2])

        circuit = {
            'agent_id': agent,
            'session_id': region,
            'secure_session_id': agent,
            'circuit_code': '123456',
            'destination_uuid': region,
            'teleport_flags': 128,
        }
        check(
            request(json.dumps(circuit).encode(), 'POST')[0] == 200,
            'POST circuit handoff remains available',
        )
        base = {
            'message_type': 'AgentData',
            'agent_uuid': agent,
            'destination_uuid': region,
            'destination_x': '25600',
            'destination_y': '51200',
            'destination_name': 'Destination',
            'position': '<3, 4, 30>',
            'context': {'InboundVersion': 0.3, 'OutboundVersion': 0.4, 'WearablesCount': 2},
            'attach_objects': [
                {'sog': '<object/>', 'extra': 'extra', 'modified': True, 'state': 'scripts'}
            ],
        }
        for kind in ['plain', 'x-encoding', 'encoding', 'content-type']:
            body = json.dumps(base).encode()
            headers = {}
            if kind != 'plain':
                body = gzip.compress(body)
                headers = (
                    {'X-Content-Encoding': 'gzip'}
                    if kind == 'x-encoding'
                    else (
                        {'Content-Encoding': 'gzip'}
                        if kind == 'encoding'
                        else {'Content-Type': 'application/x-gzip'}
                    )
                )
            before = stats()
            status, meta, data = request(body, headers=headers)
            after = stats()
            check(status == 200 and data == b'True', kind + ' full-data PUT accepted')
            check(
                meta.get('Connection', '').lower() == 'close'
                and meta.get('Content-Type') == 'application/json',
                kind + ' original response metadata',
            )
            check(
                after['data_updates'] == before['data_updates'] + 1
                and after['scene_calls'] == before['scene_calls'] + 1
                and after['data_agent'] == agent
                and after['data_position'] == '<3, 4, 30>'
                and after['data_objects'] == 1,
                kind + ' scene callback/model preserves body identity rather than path identity',
            )
            v = struct.unpack('<f', struct.pack('<f', 0.8))[0]
            check(
                after['update_context']
                == {'InboundVersion': v, 'OutboundVersion': v, 'WearablesCount': -1},
                kind + ' full update forwards fresh default context',
            )
        before = stats()
        position = {**base, 'message_type': 'AgentPosition', 'position': '<9, 8, 7>'}
        status, _, body = request(json.dumps(position).encode())
        after = stats()
        check(
            status == 200
            and body == b'True'
            and after['position_updates'] == before['position_updates'] + 1
            and after['position_agent'] == agent
            and after['position_value'] == '<9, 8, 7>'
            and after['circuit_position'] == '<9, 8, 7>'
            and after['authenticated'] is True,
            'position branch updates native model and retained circuit',
        )
        before = stats()
        check(
            request(b'{}')[2] == b'True' and request(b'{"message_type":"unknown"}')[2] == b'True',
            'absent/unknown SDK message type source no-op success',
        )
        after = stats()
        check(
            after['scene_calls'] == before['scene_calls']
            and after['data_updates'] == before['data_updates']
            and after['position_updates'] == before['position_updates'],
            'unknown message does not invoke scene/service',
        )
        denied = {**base, 'agent_uuid': agent[:-1] + '9'}
        check(
            request(json.dumps(denied).encode())[2] == b'False',
            'service rejection uses uppercase raw boolean',
        )
        malformed = {**base, 'children_seeds': [{'handle': '3'}, {'handle': '3'}]}
        before = stats()['data_updates']
        check(
            request(json.dumps(malformed).encode())[0] == 400 and stats()['data_updates'] == before,
            'unpack failure maps to 400 before update callback',
        )
        bad_scene = {**base, 'attach_objects': [{'sog': 'throw'}]}
        check(
            request(json.dumps(bad_scene).encode())[0] == 400,
            'scene deserialization failure stays within unpack boundary',
        )
        check(
            request(b'not gzip', headers={'X-Content-Encoding': 'gzip'})[0] == 400
            and request(b'[]')[0] == 400
            and request(b'{}', target='/agent/bad/')[0] == 404,
            'compressed/map/path validation precedes update',
        )
        check(
            request(
                json.dumps(base).encode(),
                target='/agent/00000000-0000-0000-0000-000000000000/',
                headers={'X-SecondLife-Shard': 'fixture'},
            )[0]
            == 200,
            'PUT keeps source zero-path-ID and shard non-return behavior',
        )
        for client, kind, key in [
            (a.data_client, 'data', 'data_updates'),
            (a.position_client, 'position', 'position_updates'),
        ]:
            before = stats()[key]
            run = subprocess.run([client, url], capture_output=True, text=True, timeout=30)
            check(
                run.returncode == 0
                and json.loads(run.stdout) == {'first': True, 'second': True, 'failed': 0},
                kind + ' native outgoing client accepts native PUT replies',
            )
            check(
                stats()[key] == before + 2,
                kind + ' native-to-native compressed update handoff occurs twice',
            )
        check(
            stats()['data_active_group'] == '00000000-0000-0000-0000-000000000008',
            'active group survives native compressed PUT and destination decoding',
        )
        check(
            request(method='GET', target='/ready')[0] == 503,
            'controlled update receiver does not claim scene readiness',
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
    (a.work / 'agent-put-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Controlled native receiver/service/scene object adapter and circuit manager; no actual world scene/root avatar/physics/viewer proof',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} native agent PUT socket checks')


if __name__ == '__main__':
    main()
