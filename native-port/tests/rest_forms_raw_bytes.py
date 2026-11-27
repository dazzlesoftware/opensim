"""Raw inventory response bytes remain intact through both HTTP transports."""
import argparse
import http.server
import json
from pathlib import Path
import subprocess
import threading


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    text = '<?xml version="1.0"?><ServerResponse><a>café</a></ServerResponse>'
    payloads = [text.encode(), b'\xef\xbb\xbf' + text.encode(),
        b'\xff\xfe' + text.encode('utf-16-le'), b'\0\0\xfe\xff' + text.encode('utf-32-be'),
        b'<ServerResponse><a>\x80\xc0\xaf\xe2\x82</a></ServerResponse>']

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', '0')))
            body = payloads[int(self.path[1:])]
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    observations = []
    try:
        for unshared in [False, True]:
            for index, body in enumerate(payloads):
                command = [args.native, f'http://127.0.0.1:{server.server_port}/{index}', 'METHOD=probe', '--raw']
                if unshared:
                    command.append('--unshared')
                actual = subprocess.run(command, text=True, capture_output=True, timeout=30)
                expected = 'OK|' + body.hex()
                observations.append(dict(unshared=unshared, payload=index, expected=expected,
                                         actual=actual.stdout.strip(), exit=actual.returncode))
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    differences = [row for row in observations if row['exit'] or row['actual'] != row['expected']]
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / 'results.json').write_text(json.dumps(dict(passed=not differences,
        observations=len(observations), differences=differences), indent=2) + '\n')
    assert not differences, differences
    print(f'PASS: {len(observations)} raw response byte transport checks')
