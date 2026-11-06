"""Object HTTP -> actual local service with controlled object/scene state callbacks."""

import argparse, gzip, hashlib, http.client, json, queue, subprocess, threading, urllib.parse
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--client', required=True)
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

    threading.Thread(target=read, daemon=True).start()
    try:
        address = lines.get(timeout=20)
        check(address.startswith('LISTENING '), 'receiver starts')
        port = urllib.parse.urlsplit(address.split(' ', 1)[1]).port

        def request(
            method='POST',
            value=None,
            path='/object/ignored/path',
            compressed=False,
            shard=False,
            raw=None,
        ):
            body = json.dumps(value).encode() if raw is None else raw
            headers = {'Content-Type': 'application/json'}
            if compressed:
                body = gzip.compress(body)
                headers['X-Content-Encoding'] = 'gzip'
            if shard:
                headers['X-SecondLife-Shard'] = ''
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                c.request(method, path, body=body, headers=headers)
                r = c.getresponse()
                return r.status, r.read(), r.getheader('Connection')
            finally:
                c.close()

        def stats():
            return json.loads(request('GET', path='/stats')[1])

        uid = lambda n: '00000000-0000-0000-0000-00000000000' + str(n)
        base = {
            'destination_uuid': uid(1),
            'sog': '<object/>',
            'extra': 'extra',
            'modified': True,
            'state': 'scripts',
            'new_position': [1, 2, 30],
        }
        status, body, connection = request(value=base, compressed=True)
        s = stats()
        check(
            status == 200 and body == b'True' and connection == 'close',
            'gzip object POST ignores path IDs and closes connection',
        )
        check(
            s['a']['objects'] == 1
            and s['b']['objects'] == 0
            and s['a']['object_xml'] == '<object/>'
            and s['a']['object_extra'] == 'extra'
            and s['a']['object_modified']
            and s['a']['object_state'] == 'scripts'
            and s['a']['object_position'] == '<1, 2, 30>',
            'selected scene receives object XML extra modified state and position',
        )
        status, body, _ = request(value={**base, 'destination_uuid': uid(2)})
        s = stats()
        check(
            status == 200
            and body == b'False'
            and s['b']['objects'] == 1
            and s['b']['object_state'] == '',
            'scene script-crossing policy skips state and callback false is preserved',
        )
        status, body, _ = request(value={**base, 'state': 'throw'})
        s = stats()
        check(
            status == 200
            and body == b'True'
            and s['a']['objects'] == 2
            and s['a']['object_state'] == '',
            'script-state failure is ignored before object admission',
        )
        for patch in [{'sog': 'throw'}, {'extra': 'throw'}]:
            before = stats()['a']['objects']
            status, body, _ = request(value={**base, **patch})
            check(
                status == 400 and body == b'' and stats()['a']['objects'] == before,
                'deserialization or extra failure returns empty 400 without admission',
            )
        before = stats()
        status, body, _ = request(value={**base, 'destination_uuid': uid(5)})
        s = stats()
        check(
            status == 200
            and body == b'False'
            and s['a']['deserialized'] == before['a']['deserialized'] + 1
            and s['a']['objects'] == before['a']['objects'],
            'missing destination uses source fallback deserialize before local rejection',
        )
        status, body, _ = request(value=base, shard=True)
        check(
            status == 200 and body == b'True',
            'source shard header continues through successful object POST',
        )
        status, body, _ = request(value={}, shard=True)
        check(
            status == 406 and body == b'false', 'early missing-field return retains shard response'
        )
        for raw in [b'bad', b'[]', b'null']:
            status, body, _ = request(raw=raw, compressed=True)
            check(status == 400 and body == b'false', 'invalid compressed JSON map rejected')
        status, body, _ = request('DELETE', shard=True)
        check(
            status == 405 and body == b'false',
            'unsupported method preserves shard body and returns 405',
        )
        before = stats()
        run = subprocess.run(
            [a.client, f'http://127.0.0.1:{port}/', '1'], capture_output=True, text=True, timeout=15
        )
        s = stats()
        check(
            run.returncode == 0
            and run.stdout == 'True'
            and s['a']['objects'] == before['a']['objects'] + 1
            and s['a']['object_extra_size'] == 150000
            and s['a']['object_extra_md5'] == hashlib.md5(b'x' * 150000).hexdigest()
            and s['a']['object_state'] == 'scripts'
            and s['a']['object_modified']
            and s['a']['object_position'] == '<1.25, 2, 30>',
            'native outgoing client interoperates with handler and local scene preserving large payload',
        )
        run = subprocess.run(
            [a.client, f'http://127.0.0.1:{port}/', '2'], capture_output=True, text=True, timeout=15
        )
        check(
            run.returncode == 0 and run.stdout == 'False',
            'native outgoing client preserves scene rejection',
        )
        check(
            request('GET', path='/ready')[0] == 503,
            'controlled object receiver does not claim a runnable world',
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
    (a.work / 'object-handler-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Native HTTP object handler/local connector with controlled scene/object callbacks. Concrete XML scene serialization, scripts and physics remain pending.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} object HTTP integration checks')


if __name__ == '__main__':
    main()
