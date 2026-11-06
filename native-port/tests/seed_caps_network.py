"""Registered native seed route over sockets, real activation and native circuit/presence verification with controlled HG module/scene lifecycle."""

import argparse, concurrent.futures, http.client, json, queue, subprocess, threading, time, urllib.parse, xml.etree.ElementTree as ET
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

        def request(path, method='GET', body=None, ctype='application/llsd+xml'):
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            try:
                c.request(method, path, body=body, headers={'Content-Type': ctype})
                r = c.getresponse()
                return r.status, r.read()
            finally:
                c.close()

        def check(ok, name):
            assert ok, name
            checks.append(name)

        def control(op, body=''):
            check(request('/control/' + op, 'POST', body)[0] == 200, 'control ' + op)

        def state():
            return json.loads(request('/state')[1])

        path = '/CAPS/seed0000'

        def body(names):
            llsd = ET.Element('llsd')
            array = ET.SubElement(llsd, 'array')
            for name in names:
                ET.SubElement(array, 'string').text = name
            return ET.tostring(llsd)

        names = [
            'A',
            'Poll',
            'External',
            'SEED',
            'ViewerBenefits',
            'VTPBR',
            'ObjectAnimation',
            'EnvironmentSettings',
            'ExtEnvironment',
            'ModifyMaterialParams',
            'A',
            'Missing',
        ]
        for method, ctype in [
            ('GET', 'application/llsd+xml'),
            ('POST', 'application/llsd+xml; charset=utf-8'),
            ('POST', 'application/json'),
        ]:
            check(
                request(path, method, b'bad', ctype)[0] == 404 and state()['checks'] == 0,
                'strict method/content type precedes activation/client lookup',
            )
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(request, path, 'POST', body(names))
            deadline = time.monotonic() + 5
            try:
                while state()['pending'] != 1 and time.monotonic() < deadline:
                    time.sleep(0.01)
                check(
                    not pending.done() and state()['pending'] == 1,
                    'seed request waits on actual native activation',
                )
            finally:
                control('activate')
            status, xml = pending.result(timeout=5)
        check(status == 200, 'activated seed request succeeds')
        items = list(ET.fromstring(xml).find('map'))
        urls = {items[i].text: items[i + 1].text for i in range(0, len(items), 2)}
        check(
            set(urls)
            == {
                'A',
                'Poll',
                'External',
                'ObjectAnimation',
                'EnvironmentSettings',
                'ExtEnvironment',
                'ModifyMaterialParams',
            }
            and urls['A'].endswith('/cap/action')
            and urls['Poll'].endswith('/poll/controlled')
            and urls['External'] == 'https://external/cap?a=1&b=2',
            'seed response composes actual capability registrations and omits protocol-only feature names',
        )
        check(
            state()['flags'] == 0x3F01 and state()['remote_port'] > 0,
            'viewer feature flags and accepted socket peer port reach native scene backend',
        )
        endpoint = urllib.parse.urlsplit(urls['A'])
        check(
            endpoint.scheme == 'http'
            and endpoint.hostname == '127.0.0.1'
            and endpoint.port == port
            and urllib.parse.urlsplit(urls['Poll']).port == port,
            'advertised local and poll URLs use the actual bound port',
        )
        connection = http.client.HTTPConnection(endpoint.hostname, endpoint.port, timeout=10)
        try:
            connection.request('POST', endpoint.path, body=b'payload')
            response = connection.getresponse()
            check(
                (response.status, response.read()) == (200, b'action:POST:payload'),
                'viewer follows the advertised absolute capability URL',
            )
        finally:
            connection.close()
        check(
            request(path + '/', 'POST', body(['A']))[0] == 200,
            'native registry accepts viewer seed URL trailing slash',
        )
        before = state()['flags']
        control('deny')
        check(
            request(path, 'POST', b'bad')[0] == 403 and state()['flags'] == before,
            'unauthorized client rejected before parsing malformed body',
        )
        control('allow')
        control('admit')
        check(request(path, 'POST', b'not xml')[0] == 400, 'malformed LLSD returns bad request')
        control('reset')
        control('size', '512')
        check(
            request(path, 'POST', body(['VTPBR']))[0] == 200 and state()['flags'] == 0x801,
            'variable-sized region negotiates PBR without TPBR',
        )
        control('reset')
        check(
            request(
                path,
                'POST',
                b'<llsd><array><string>ViewerBenefits</string><unknown></unknown></array></llsd>',
            )[0]
            == 500
            and state()['flags'] == 0x1000,
            'null SDK item preserves earlier flags without marking seeds sent',
        )
        control('dispose')
        check(
            request(path, 'POST', body(['A']))[0] == 404
            and request('/cap/action', 'POST')[0] == 404,
            'disposal unregisters seed and advertised local route',
        )
        check(
            request('/ready')[0] == 503,
            'registered seed fixture does not claim full world/poll scheduling readiness',
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
    (a.work / 'seed-caps-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Native seed/SDK-compatible parsing, registered capability route, Caps activation, socket peer endpoint and cleanup; actual native route registry/scene verification/circuit manager/presence service and controlled HG verifier/actor callbacks, without complete world/viewer or poll scheduling proof.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} seed capability socket checks')


if __name__ == '__main__':
    main()
