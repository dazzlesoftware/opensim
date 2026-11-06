"""Native event queue, real Caps registry and asynchronous polling transport."""

import argparse, http.client, json, ssl, subprocess, time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--tls', action='store_true')
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    command = [a.server]
    if a.tls:
        command += [
            str(Path(__file__).parent / 'fixtures/localhost-cert.pem'),
            str(Path(__file__).parent / 'fixtures/localhost-key.pem'),
        ]
    proc = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    connections = []
    checks = []

    def check(value, label):
        assert value, label
        checks.append(label)

    try:
        line = proc.stdout.readline()
        assert line.startswith('LISTENING '), line + proc.stderr.read()
        port = int(line.split()[1])

        def connect():
            c = (
                http.client.HTTPSConnection(
                    '127.0.0.1', port, timeout=10, context=ssl._create_unverified_context()
                )
                if a.tls
                else http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            )
            connections.append(c)
            return c

        def request(path, body=''):
            c = connect()
            c.request('POST', path, body)
            r = c.getresponse()
            v = (r.status, r.read())
            c.close()
            return v

        def wait_pending(n):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if request('/pending')[1] == str(n).encode():
                    return
                time.sleep(0.01)
            raise AssertionError('pending count ' + str(n))

        path = request('/discover')[1].decode()
        check(path.startswith('/CE/'), 'real Caps registers generated EventQueueGet path')
        # Each delayed request uses the same actual agent queue; one worker must still serve controls.
        held = []
        for _ in range(12):
            c = connect()
            c.request('POST', path, b'')
            held.append(c)
        wait_pending(12)
        check(
            request('/ready')[0] == 503, 'twelve pending viewer polls leave HTTP worker available'
        )
        # Source HasEvents and GetEvents are separate calls. Overlapping polls for one
        # agent can legitimately race to an empty502. Drain the capacity demonstration
        # before checking deterministic event bytes with one viewer poll at a time.
        for c in held:
            c.close()
        wait_pending(0)
        check(True, 'disconnected pending polls release transport requests')
        for i in range(12):
            c = connect()
            c.request('POST', path, b'')
            wait_pending(1)
            check(
                request('/event', f'<string>{i}</string>')[0] == 200,
                'controlled producer ' + str(i),
            )
            r = c.getresponse()
            body = r.read()
            check(
                r.status == 200
                and f'<integer>{40+i}</integer>'.encode() in body
                and f'<string>{i}</string>'.encode() in body,
                'native queue event/sequence ' + str(i),
            )
            c.close()
        request('/event', '<string>old</string>')
        request('/register')
        request('/event', '<string>new</string>')
        check(request('/discover')[1].decode() == path, 're-registration preserves Caps route')
        code, body = request(path)
        check(
            code == 200
            and b'<string>old</string>' in body
            and b'<string>new</string>' not in body
            and b'<integer>52</integer>' in body,
            'reconnect marker separates old response',
        )
        code, body = request(path)
        check(
            code == 200 and b'<string>new</string>' in body and b'<integer>41</integer>' in body,
            'reconnect installs new random sequence',
        )
        producers = [
            ('enable', 'EnableSimulator'),
            ('teleport', 'TeleportFinish'),
            ('cross', 'CrossedRegion'),
            ('establish', 'EstablishAgentCommunication'),
            ('invite', 'ChatterBoxInvitation'),
            ('updates', 'ChatterBoxSessionAgentListUpdates'),
            ('chat-start', 'ChatterBoxSessionStartReply'),
            ('chat-close', 'ForceCloseChatterBoxSession'),
            ('groups', 'AgentGroupDataUpdate'),
            ('places', 'PlacesReplyMessage'),
            ('script', 'ScriptRunningReply'),
            ('physics', 'ObjectPhysicsProperties'),
            ('windlight', 'WindLightRefresh'),
            ('inventory', 'BulkUpdateInventory'),
            ('generic', 'LargeGenericMessage'),
            ('generic-bytes', 'LargeGenericMessage'),
            ('build', 'Custom'),
        ]
        for i, (producer, event) in enumerate(producers):
            c = connect()
            c.request('POST', path, b'')
            wait_pending(1)
            check(request('/produce', producer)[0] == 200, 'native producer ' + producer)
            r = c.getresponse()
            data = r.read()
            check(
                r.status == 200
                and ('<key>message</key><string>' + event + '</string>').encode() in data
                and f'<integer>{42+i}</integer>'.encode() in data,
                'native producer HTTP event ' + producer,
            )
            c.close()
        request('/closed')
        check(
            request(path) == (404, b''),
            'closed agent returns native404 through retained poll handler',
        )
        request('/remove')
        check(request(path)[0] == 404, 'capability deregistration removes HTTP route')
        proc.stdin.write('stop\n')
        proc.stdin.flush()
        proc.wait(timeout=10)
        assert proc.returncode == 0, proc.stderr.read()
        (a.work / 'event-queue-network-results.json').write_text(
            json.dumps(
                {
                    'passed': True,
                    'checks': checks,
                    'tls': a.tls,
                    'scope': 'Real native EventQueueModule, Caps, registry, polling manager and TCP/TLS output. Controlled producer/admission; seed negotiation, scene/world and Unix execution remain pending.',
                },
                indent=2,
            ),
            encoding='utf-8',
        )
        print(f'PASS: {len(checks)} native event queue network observations')
    finally:
        for c in connections:
            c.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == '__main__':
    main()
