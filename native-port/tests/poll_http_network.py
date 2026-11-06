"""Native registry + polling manager/response execution + actual asynchronous sockets."""

import argparse, http.client, json, queue, socket, ssl, subprocess, threading, time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--tls', action='store_true')
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []
    sockets = []
    command = [a.server]
    if a.tls:
        command += [
            str(Path(__file__).parent / 'fixtures/localhost-cert.pem'),
            str(Path(__file__).parent / 'fixtures/localhost-key.pem'),
        ]
    proc = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    lines = queue.Queue()

    def output():
        for line in proc.stdout:
            lines.put(line.strip())

    threading.Thread(target=output, daemon=True).start()
    try:
        line = lines.get(timeout=30)
        assert line.startswith('LISTENING '), line
        port = int(line.split()[1])
        context = ssl._create_unverified_context() if a.tls else None

        def connect():
            s = socket.create_connection(('127.0.0.1', port), timeout=5)
            if a.tls:
                s = context.wrap_socket(s, server_hostname='localhost')
            sockets.append(s)
            return s

        def request(path, body=None):
            c = (
                http.client.HTTPSConnection('127.0.0.1', port, timeout=5, context=context)
                if a.tls
                else http.client.HTTPConnection('127.0.0.1', port, timeout=5)
            )
            try:
                c.request('POST' if body is not None else 'GET', path, body)
                r = c.getresponse()
                return r.status, r.read(), dict(r.getheaders())
            finally:
                c.close()

        def read(f):
            line = f.readline()
            assert line.startswith((b'HTTP/1.1 ', b'HTTP/1.0 ')), line
            headers = {}
            while True:
                h = f.readline()
                assert h, h
                if h == b'\r\n':
                    break
                k, v = h.decode('latin1').split(':', 1)
                headers[k.lower()] = v.strip()
            return line.strip(), f.read(int(headers.get('content-length', 0))), headers

        def check(value, name):
            assert value, name
            checks.append(name)

        def stats():
            return list(map(int, request('/stats')[1].split(b':')))

        def until(predicate, name):
            end = time.monotonic() + 5
            while not predicate():
                assert time.monotonic() < end, name
                time.sleep(0.01)

        def begin(key, path='/poll'):
            s = connect()
            b = key.encode()
            s.sendall(
                f'POST {path} HTTP/1.1\r\nHost: localhost\r\nContent-Length: {len(b)}\r\n\r\n'.encode()
                + b
            )
            return s, s.makefile('rb')

        held = [begin('held' + str(i)) for i in range(24)]
        until(lambda: stats()[8] == 24, 'Pending polls never reached retries')
        check(
            request('/ordinary')[1] == b'ordinary:/ordinary',
            '24 native polls leave one ordinary HTTP worker available',
        )
        # Original RequestsHandled++ is a separate property read/write. Keep all24
        # requests pending, then release each before asserting its exact counter;
        # simultaneous increments can legitimately lose updates in the source.
        for i, (s, f) in enumerate(held):
            check(request('/event', 'held' + str(i))[0] == 200, 'event queued ' + str(i))
            status, body, headers = read(f)
            check(
                status == b'HTTP/1.1 200 OK'
                and body == ('held' + str(i)).encode()
                and headers.get('server') == 'OSWebServer'
                and headers.get('keep-alive') == 'timeout=60, max=19',
                'native event response ' + str(i),
            )
            f.close()
            s.close()
            until(lambda: stats()[2] == i + 1, 'Handled counter missing for event ' + str(i))
        until(lambda: stats()[2] == 24, 'Handled counter missing')
        check(stats()[:4] == [0, 24, 24, 0], 'native received/handled counters and queue cleanup')
        for mode, expected in [
            ('binary', bytes([1, 2, 253])),
            ('range', bytes([1, 2, 253, 254, 255])),
            ('base64', b'abc'),
            ('custom', b'custom'),
            ('created', b'created'),
            ('large', bytes([42]) * 1024 * 1024),
            ('redirect', b''),
        ]:
            s, f = begin(mode)
            until(lambda: stats()[8] >= 1, 'Mode poll not queued')
            check(request('/event', mode + '|' + mode)[0] == 200, 'mode event ' + mode)
            status, body, headers = read(f)
            check(body == expected, 'response body ' + mode)
            if mode in ['binary', 'range']:
                check(headers.get('x-test') == 'present', 'extra header ' + mode)
            if mode == 'custom':
                check(
                    status == b'HTTP/1.0 200 Custom Ready' and headers['connection'] == 'close',
                    'source protocol/reason override',
                )
            if mode == 'created':
                check(
                    status == b'HTTP/1.1 201 Created' and headers['connection'] == 'close',
                    'source non-keepalive success status policy',
                )
            if mode == 'redirect':
                check(
                    status.startswith(b'HTTP/1.1 301 ')
                    and headers.get('location') == '/moved'
                    and headers['connection'] == 'close',
                    'source redirect response',
                )
            f.close()
            s.close()
        slow, f = begin('slow')
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        until(lambda: stats()[8] >= 1, 'Slow poll not queued')
        check(request('/event', 'slow|slow')[0] == 200, 'large delayed response queued')
        time.sleep(0.2)
        check(
            request('/responsive')[1] == b'ordinary:/responsive',
            'slow delayed reader leaves HTTP reactor responsive',
        )
        f.close()
        slow.close()
        check(
            request('/direct')[0:2] == (202, b'direct'),
            'source direct Request callback sends prebuilt response',
        )
        check(stats()[4:6] == [1, 0], 'direct callback does not increment RequestsHandled')
        code, body, headers = request('/body')
        headers = {k.lower(): v for k, v in headers.items()}
        check(
            code == 200 and body == 'raw:stream body é'.encode(),
            'raw and stream body stages are both delivered',
        )
        check(
            headers['content-length'] == str(len(body)) and 'transfer-encoding' not in headers,
            'declared length and Chunked flag retain source header behavior',
        )
        check(
            headers['set-cookie'] == 'session=a%2bb+c; path=/scope; domain=example.test; ',
            'response cookie URL encoding, path and domain',
        )
        check(
            headers['x-priority'] == '0'
            and headers['x-keep'] == '400'
            and headers['keep-alive'] == 'timeout=120, max=19',
            'source priority setter quirk and keepalive/context clamps',
        )
        check(
            int(headers['x-request-id']) > 0 and headers['x-arrival-positive'] == 'yes',
            'response constructor retains actual request identity and arrival timestamp',
        )
        until(
            lambda: request('/body-state')[1] == b'1:1:0:0:0',
            'Response send lifecycle did not release headers/raw/body',
        )
        check(True, 'async completion sets Sent and disposes promoted body stream')
        code, body, headers = request('/keep-negative')
        check(
            code == 200
            and dict((k.lower(), v) for k, v in headers.items())['connection'] == 'close',
            'negative keepalive property clamps to zero and closes response',
        )
        stream = connect()
        stream.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        stream.sendall(b'GET /slow-body HTTP/1.1\r\nHost: localhost\r\n\r\n')
        until(
            lambda: request('/body-state')[1] == b'1:0:0:0:1',
            'Slow body was not promoted while send remained active',
        )
        check(True, 'Sent remains false while stream-backed data waits for a slow reader')
        stream.close()
        until(
            lambda: request('/body-state')[1] == b'1:0:0:0:0',
            'Disconnect did not release active stream-backed data',
        )
        check(
            True, 'disconnect clears pending response buffers without marking successful delivery'
        )
        check(
            request('/timeout')[0:2] == (502, b''),
            'scheduler NoEvents timeout emits502 with empty body',
        )
        until(lambda: stats()[6] == 1, 'Timeout handled counter missing')
        s, f = begin('gone')
        until(lambda: stats()[8] >= 1, 'Disconnect poll not queued')
        f.close()
        s.close()
        until(lambda: stats()[3] == 1, 'Drop callback not invoked')
        check(stats()[0] == 0, 'dropped HTTP poll removes retained request')
        s = connect()
        f = s.makefile('rb')
        for i in range(1, 21):
            s.sendall(b'GET /instant HTTP/1.1\r\nHost: localhost\r\n\r\n')
            status, body, headers = read(f)
            check(
                body == b'instant'
                and (
                    headers.get('keep-alive') == f'timeout=60, max={20-i}'
                    if i < 20
                    else headers['connection'] == 'close' and 'keep-alive' not in headers
                ),
                'source keepalive request budget ' + str(i),
            )
        check(
            f.read(1) == b'',
            'twentieth response advertises close after pre-dispatch budget decrement',
        )
        f.close()
        s.close()
        stopped = [begin('stop' + str(i)) for i in range(3)]
        until(lambda: stats()[8] == 3, 'Stop polls not queued')
        check(request('/stop')[1] == b'stopped', 'poll service stop returns')
        for s, f in stopped:
            status, body, headers = read(f)
            check(
                status.startswith(b'HTTP/1.1 503 ')
                and body == b''
                and headers['connection'] == 'close',
                'native pending request shutdown503',
            )
            f.close()
            s.close()
        check(stats()[7] == 0, 'no native polling transport warnings')
        check(request('/ready')[0] == 503, 'production world readiness remains503')
        proc.stdin.write('stop\n')
        proc.stdin.flush()
        proc.wait(timeout=10)
        assert proc.returncode == 0, proc.stderr.read()
        (a.work / 'poll-http-results.json').write_text(
            json.dumps(
                {
                    'passed': True,
                    'checks': checks,
                    'tls': a.tls,
                    'scope': 'Actual native registry, Request callback, poll worker/retry scheduler, source response execution and asynchronous TCP/TLS transport. No controlled poll receiver. Full original HttpResponse/parser/queue/header/async-send differential coverage, world/event queue and Unix remain pending.',
                },
                indent=2,
            ),
            encoding='utf-8',
        )
        print(f'PASS: {len(checks)} asynchronous polling HTTP observations')
    finally:
        for s in sockets:
            s.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == '__main__':
    main()
