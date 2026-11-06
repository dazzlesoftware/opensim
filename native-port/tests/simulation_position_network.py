"""Outgoing real compressed position PUTs, wrapper booleans and per-simulator suppression."""

import argparse, gzip, http.server, json, subprocess, threading, struct
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        profile = 'success'
        requests = []

        def log_message(self, *args):
            pass

        def do_PUT(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
            headers = {k.lower(): v for k, v in self.headers.items()}
            self.requests.append((self.path, headers, body))
            if self.profile == 'disconnect':
                self.close_connection = True
                return
            data = {
                'success': b'True',
                'false': b'False',
                'inner_false': b'{"success":false}',
                'whitespace_false': b' false ',
                'malformed': b'not JSON',
                'error': b'failure',
            }[self.profile]
            self.send_response(503 if self.profile == 'error' else 200)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(data)
            self.close_connection = True

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f'http://127.0.0.1:{server.server_port}/base/'
    try:
        for profile in [
            'success',
            'false',
            'inner_false',
            'whitespace_false',
            'malformed',
            'error',
            'disconnect',
        ]:
            Handler.profile = profile
            Handler.requests = []
            run = subprocess.run([a.native, url], capture_output=True, text=True, timeout=20)
            check(run.returncode == 0, profile + ' native client completes')
            value = json.loads(run.stdout)
            success = profile in ['success', 'inner_false', 'whitespace_false', 'malformed']
            check(
                value == {'first': success, 'second': success, 'failed': 0 if success else 1},
                profile + ' source wrapper-result and failure-cache behavior',
            )
            check(
                len(Handler.requests) == (2 if success else 1),
                profile
                + f' no uncompressed retry and cached failure suppresses second update (observed {len(Handler.requests)} requests, markers {[h.get("x-content-encoding") for _,h,_ in Handler.requests]})',
            )
            for index, (path, headers, body) in enumerate(Handler.requests):
                label = profile + '/' + str(index)
                check(
                    path == '/base/agent/00000000-0000-0000-0000-000000000007/',
                    label + ' exact position target without region suffix',
                )
                check(
                    headers.get('content-type') == 'application/json'
                    and headers.get('x-content-encoding') == 'gzip'
                    and 'content-encoding' not in headers,
                    label + ' original compressed request markers',
                )
                check(
                    headers.get('connection', '').lower() == 'close'
                    and 'keep-alive' not in headers
                    and headers.get('opensim-request-id', '').isdigit(),
                    label + ' source connection policy and request ID',
                )
                check(
                    int(headers['content-length']) == len(body)
                    and 'transfer-encoding' not in headers,
                    label + ' fixed compressed body framing',
                )
                data = json.loads(gzip.decompress(body))
                check(
                    data['message_type'] == 'AgentPosition'
                    and data['position'] == '<1.25, 2, 30>'
                    and data['far'] == (10 if index == 0 else 20),
                    label + ' complete position data crosses gzip/JSON transport',
                )
                check(
                    data['destination_x'] == '25600'
                    and data['destination_y'] == '51200'
                    and data['destination_name'] == 'Destination'
                    and data['destination_uuid'] == '00000000-0000-0000-0000-000000000009'
                    and data['context']
                    == {
                        'InboundVersion': struct.unpack('<f', struct.pack('<f', 0.8))[0],
                        'OutboundVersion': struct.unpack('<f', struct.pack('<f', 0.8))[0],
                        'WearablesCount': -1,
                    },
                    label + ' destination and source-default transfer context',
                )
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    (a.work / 'simulation-position-network-results.json').write_text(
        json.dumps(
            {
                'passed': True,
                'checks': checks,
                'scope': 'Native connector and real HTTP/gzip position PUT against controlled receiver; full managed HTTP/URI/scene/platform parity remains pending',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'PASS: {len(checks)} native position-update socket checks')


if __name__ == '__main__':
    main()
