"""Concrete Caps, real activation wait and sockets; controlled listener/poll registry."""

import argparse, concurrent.futures, http.client, json, queue, subprocess, threading, time, xml.etree.ElementTree as ET
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen(
        [a.server], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    q = queue.Queue()
    checks = []

    def reader():
        for line in proc.stdout:
            q.put(line.strip())

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        line = q.get(timeout=20)
        assert line.startswith('LISTENING '), line
        port = int(line.split()[1])

        def request(path, method='GET', body=None):
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                c.request(method, path, body=body)
                r = c.getresponse()
                return r.status, r.read()
            finally:
                c.close()

        def check(ok, name):
            assert ok, name
            checks.append(name)

        def control(op, body=''):
            check(request('/control/' + op, 'POST', body)[0] == 200, 'control ' + op)

        pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        try:
            waiting = pool.submit(request, '/wait')
            deadline = time.monotonic() + 5
            while request('/state')[1].split(b':')[0] != b'1' and time.monotonic() < deadline:
                time.sleep(0.01)
            check(
                not waiting.done() and request('/state')[1].startswith(b'1:'),
                'native activation blocks a live HTTP request',
            )
            control('activate')
            check(
                waiting.result(timeout=5) == (200, b'true'), 'activation wakes waiting HTTP worker'
            )
            check(request('/wait') == (200, b'true'), 'manual-reset activation remains signaled')
        finally:
            if not waiting.done():
                request('/control/activate', 'POST')
            pool.shutdown(wait=True)
        control('normal', 'POST\n/caps/native')
        control('external', 'https://external/path?a=1&b=2')
        control('poll', '/poll/controlled')
        details = json.loads(request('/object-details')[1])
        check(
            details
            == {
                'X': 'http://127.0.0.1:0/caps/native',
                'External': 'https://external/path?a=1&b=2',
                'Poll': 'http://127.0.0.1:0/poll/controlled',
            },
            'native Caps composes normal poll and external capability URLs',
        )
        status, body = request('/object-xml')
        root = ET.fromstring(body)
        items = list(root.find('map'))
        xml = {items[i].text: items[i + 1].text for i in range(0, len(items), 2)}
        check(
            status == 200 and xml == details,
            'actual SDK-compatible XML includes all capability types and escapes external URLs',
        )
        check(request('/state')[1] == b'0:1', 'poll registration reaches listener registry')
        control('dispose')
        check(
            request('/caps/native', 'POST')[0] == 404 and request('/state')[1] == b'0:0',
            'dispose unregisters HTTP and poll handlers',
        )
        check(
            json.loads(request('/object-details')[1])
            == {'External': 'https://external/path?a=1&b=2'},
            'external registrations survive source disposal',
        )
        check(
            request('/control/activate', 'POST')[0] == 500 and request('/wait')[0] == 500,
            'disposed activation rejects set and wait',
        )
        control('dispose')
        check(
            request('/ready')[0] == 503, 'fixture does not claim seed endpoint or world readiness'
        )
    finally:
        if proc.poll() is None:
            proc.stdin.write('quit\n')
            proc.stdin.flush()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            raise
        t.join(timeout=5)
        if proc.returncode:
            raise RuntimeError(proc.stderr.read())
    (a.work / 'caps-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Concrete native Caps/handlers, HTTP sockets and actual condition-variable activation. Controlled exact-path listener/poll registry, without poll worker scheduler or seed endpoint/world parity.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} concrete caps socket checks')


if __name__ == '__main__':
    main()
