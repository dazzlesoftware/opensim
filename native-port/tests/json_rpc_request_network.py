"""Real native JSON-RPC sender with SDK-derived expected wire/data outcomes."""

import argparse, subprocess, json, threading, http.server, sys, struct, uuid
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
commands = [
    f'M {kind} {fill} 18 {args}' for kind in range(8) for fill in range(2) for args in [0, 1]
] + [f'O {kind} 0 18 {args}' for kind in range(1, 8) for args in [0, 1, 2]]
commands += ['M 1 1 4 0', 'O 2 0 4 0', 'M 1 1 3 0', 'O 2 0 3 0']
all_commands = [
    f'M {model} {fill} {reply} {args}'
    for model in range(9)
    for fill in range(2)
    for reply in range(19)
    for args in range(10)
] + [f'O {kind} 0 {reply} {args}' for kind in range(8) for reply in range(19) for args in range(10)]
source_rows = a.reference.joinpath('reference.txt').read_text().splitlines()
assert len(source_rows) == len(all_commands)
reference = dict(zip(all_commands, source_rows))
normal = bytes.fromhex(reference['O 2 0 18 0'].split('|')[1])
denied = b'{"jsonrpc":"2.0","id":"reply","error":{"code":-3,"message":"denied"}}'
active = []
records = []


class Peer(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        records.append((dict(self.headers), body))
        index = len(records) - 1
        command = commands[index]
        reply = int(command.split()[3])
        data = denied if reply == 4 else b'false' if reply == 3 else normal
        if index % 6 == 1:
            data = b'\xef\xbb\xbf' + data
        elif index % 6 == 2:
            data = b'\xff\xfe' + data.decode().encode('utf-16-le')
        elif index % 6 == 3:
            data = b'\xfe\xff' + data.decode().encode('utf-16-be')
        elif index % 6 == 4:
            data = b'\xff\xfe\x00\x00' + data.decode().encode('utf-32-le')
        elif index % 6 == 5:
            data = b'\x00\x00\xfe\xff' + data.decode().encode('utf-32-be')
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(data)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Peer)
worker = threading.Thread(target=server.serve_forever, daemon=True)
worker.start()
try:
    run = subprocess.run(
        [a.native, f'http://127.0.0.1:{server.server_port}/rpc'],
        input='\n'.join(commands) + '\n',
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert run.returncode == 0, run.stderr
    rows = run.stdout.splitlines()
    assert len(rows) == len(records) == len(commands), (len(rows), len(records), len(commands))
    for index, (command, row, (headers, body)) in enumerate(zip(commands, rows, records)):
        expected = reference[command].split('|')
        assert row == '|'.join(expected[:2]), (command, row, expected[:2])
        lower = {k.lower(): v for k, v in headers.items()}
        assert lower['content-type'] == 'application/json-rpc'
        assert lower['connection'].lower() == 'close'
        assert lower['opensim-request-id'] == str(index)
        assert int(lower['content-length']) == len(body)
        assert all(
            name not in lower for name in ['accept', 'accept-encoding', 'user-agent', 'cookie']
        ), lower
        request = json.loads(body)
        expected_body = bytes.fromhex(expected[5])
        if command.startswith('O ') and command.split()[-1] in ['1', '2']:
            generated = request['id']
            assert uuid.UUID(generated).version == 4
            body = body.replace(generated.encode(), b'<generated>')
        assert body == expected_body, (command, body, expected_body)
    report = {
        'passed': True,
        'requests': len(commands),
        'scope': 'Native default JSON-RPC sender through shared WebUtil OSD transport. SDK-derived request bytes and original request-manager outcomes, request IDs, Content-Type/length/close headers, absence of extra default headers, and BOM UTF8/16/32 replies.',
    }
    a.work.joinpath('results.json').write_text(json.dumps(report, indent=2))
    print('PASS:', len(commands), 'native outgoing JSON-RPC HTTP requests')
finally:
    server.shutdown()
    server.server_close()
    worker.join(timeout=3)
