import argparse, json, subprocess, xml.etree.ElementTree as ET, os, threading, time, socketserver, hashlib
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--overlap-payload', choices=['same', 'distinct', 'valid-status'], default='same')
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(
    items,
    'Compile',
    Include=str(Path(__file__).resolve().with_name('config_http_x509_resource_reference.cs')),
)
ET.ElementTree(project).write(w / 'Reference.csproj')
timings = {}


def run(command, **kwargs):
    r = subprocess.run(command, capture_output=True, text=True, timeout=120, **kwargs)
    assert r.returncode == 0, r.stdout + r.stderr
    if 'active' in globals() and active:
        timings[active] = r.stderr
    return r.stdout


run([a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'])
reference = [a.dotnet, str(w / 'app/Reference.dll')]
records = []
closures = []
active = ''
serial = 0
lock = threading.Lock()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Handler(socketserver.StreamRequestHandler):
    def setup(self):
        global serial
        super().setup()
        self.group = active
        with lock:
            serial += 1
            self.connection_id = serial

    def handle(self):
        try:
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                target = line.decode('ascii').split()[1]
                headers = []
                while (line := self.rfile.readline()) not in [b'\r\n', b'']:
                    key, value = line.decode('latin1').strip().split(':', 1)
                    headers.append((key.lower(), value.strip()))
                records.append(
                    dict(
                        group=self.group,
                        target=target,
                        headers=headers,
                        connection=self.connection_id,
                        time=time.monotonic(),
                    )
                )
                path = target.split('?', 1)[0]
                status = 200
                body = b'ok'
                extra = []
                close = False
                chunks = False
                declared = None
                if path.startswith('/status/'):
                    status = int(path.rsplit('/', 1)[1])
                    body = b'' if status in [204, 304] else b'body'
                elif path.startswith('/redirect/'):
                    parts = path.split('/')
                    status = int(parts[2])
                    destination = '/ok' if len(parts) < 4 else '/' + parts[3]
                    extra = [('Location', destination)]
                    body = b'redirect-body'
                elif path.startswith('/chain/'):
                    left = int(path.rsplit('/', 1)[1])
                    status = 302
                    body = b'r'
                    extra = [('Location', '/chain/' + str(left - 1) if left else '/ok')]
                elif path == '/no-location':
                    status = 302
                    body = b'not-followed'
                elif path == '/https-location':
                    status = 302
                    extra = [
                        ('Location', 'https://127.0.0.1:' + str(server.server_address[1]) + '/ok')
                    ]
                elif path == '/bad-location':
                    status = 302
                    extra = [('Location', 'http://[bad]/')]
                elif path == '/cookie-set':
                    extra = [('Set-Cookie', 'a=1; Path=/'), ('Set-Cookie', 'b=2; Path=/cookie')]
                elif path == '/cookie-invalid':
                    extra = [('Set-Cookie', 'invalid'), ('Set-Cookie', 'c=3; Path=/')]
                elif path == '/cookie-delete':
                    extra = [('Set-Cookie', 'a=gone; Max-Age=0; Path=/')]
                elif path in ['/cookie', '/cookie/echo', '/echo']:
                    body = ';'.join(value for name, value in headers if name == 'cookie').encode(
                        'ascii'
                    )
                elif path.startswith('/length/'):
                    body = b'x' * int(path.rsplit('/', 1)[1])
                elif path.startswith('/chunk/'):
                    body = b'y' * int(path.rsplit('/', 1)[1])
                    chunks = True
                elif path == '/eof':
                    body = b'eof-body'
                    close = True
                elif path == '/truncated':
                    body = b'abc'
                    declared = 20
                    close = True
                elif path == '/chunk-length':
                    chunks = True
                    declared = 1000
                    body = b'z'
                elif path == '/empty-204-length':
                    status = 204
                    declared = 1000
                    body = b''
                elif path == '/oversize-declared':
                    declared = 104857601
                    body = b''
                    close = True
                elif path == '/slow-head':
                    time.sleep(0.4)
                elif path == '/slow-body':
                    body = b'ok' if a.overlap_payload == 'same' else b'prior-payload'
                elif path == '/slow-redirect':
                    status = 302
                    body = (
                        b'ok'
                        if a.overlap_payload == 'same'
                        else (
                            b'following-payload'
                            if a.overlap_payload == 'distinct'
                            else b'HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n'
                        )
                    )
                    extra = [('Location', '/slow-body')]
                if path == '/slow-redirect':
                    time.sleep(0.15)
                response = f'HTTP/1.1 {status} Fixture\r\n'.encode('ascii')
                if declared is not None:
                    response += f'Content-Length: {declared}\r\n'.encode()
                elif not chunks and not close:
                    response += f'Content-Length: {len(body)}\r\n'.encode()
                if chunks:
                    response += b'Transfer-Encoding: chunked\r\n'
                if close:
                    response += b'Connection: close\r\n'
                for name, value in extra:
                    response += (name + ': ' + value + '\r\n').encode('latin1')
                self.wfile.write(response + b'\r\n')
                self.wfile.flush()
                if path in ['/slow-body', '/slow-redirect']:
                    time.sleep(0.4 if path == '/slow-body' else 0.15)
                self.wfile.write(
                    (f'{len(body):x}\r\n'.encode() + body + b'\r\n0\r\n\r\n') if chunks else body
                )
                self.wfile.flush()
                if close:
                    return
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        finally:
            closures.append(
                dict(group=self.group, connection=self.connection_id, time=time.monotonic())
            )


server = Server(('127.0.0.1', 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = 'http://127.0.0.1:' + str(server.server_address[1])
rows = []


def add(label, path, ticks=100000000, delay=0):
    rows.append(
        dict(
            label=label,
            uri=None if path is None else path if ':' in path else base + path,
            ticks=ticks,
            delayBeforeMilliseconds=delay,
        )
    )


add('null', None)
add('empty', '')
rows[-1]['uri'] = ''
add('relative', 'relative')
rows[-1]['uri'] = 'relative'
add('https', 'https://127.0.0.1/')
add('invalid', 'http://[bad]/')
for ticks in [0, -1, -10000, 9223372036854775807]:
    add('timeout-' + str(ticks), '/ok', ticks)
for status in [200, 201, 204, 299, 300, 301, 302, 303, 304, 305, 307, 308, 400, 404, 500]:
    add('status-' + str(status), '/status/' + str(status))
for status in [300, 301, 302, 303, 307, 308]:
    add('redirect-' + str(status), '/redirect/' + str(status))
for path in [
    '/no-location',
    '/https-location',
    '/bad-location',
    '/chain/9',
    '/chain/10',
    '/cookie-set',
    '/cookie/echo',
    '/echo',
    '/cookie-invalid',
    '/echo',
    '/cookie-delete',
    '/echo',
    '/length/63',
    '/length/64',
    '/length/65',
    '/chunk/63',
    '/chunk/64',
    '/chunk/65',
    '/chunk-length',
    '/empty-204-length',
    '/eof',
    '/truncated',
    '/oversize-declared',
]:
    # These follow responses rejected by the buffer limit. Let their asynchronous
    # disposal drains settle before checking deterministic physical socket reuse.
    # Deliberate cancellation-overlap cases below have no such settling delay.
    add(path, path, delay=100 if path in ['/chunk/63', '/chunk-length', '/empty-204-length'] else 0)
for path in ['/slow-head', '/slow-body', '/slow-redirect']:
    add(path, path, 4500000 if path == '/slow-redirect' else 1000000)
add('cancel-before-other-origin', '/slow-body', 1000000)
add('other-origin', base.replace('127.0.0.1', 'localhost') + '/ok')
add('return-to-pending-origin', '/slow-redirect', 4500000)
add('cancel-before-drain-deadline', '/slow-body?case=expiry', 1000000)
add('after-drain-deadline', '/ok?case=expired-resume', delay=2200)
protocol = (
    '\n'.join(
        str(row['ticks'])
        + ' '
        + ('_' if row['uri'] is None else row['uri'].encode('utf-16-be').hex() or '-')
        + ' '
        + str(row['delayBeforeMilliseconds'])
        for row in rows
    )
    + '\n'
)
env = os.environ.copy()
env.update(NO_PROXY='*', no_proxy='*')
for key in ['HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'http_proxy', 'https_proxy', 'all_proxy']:
    env.pop(key, None)


def capture(group_name, command, limit):
    global active
    active = group_name
    output = run([*command, limit], input=protocol, env=env).splitlines()
    assert len(output) == len(rows), (group_name, limit, len(rows), len(output))
    group = [record for record in records if record['group'] == group_name]
    ids = {}
    wire = []
    elapsed = None
    for record in group:
        if record['connection'] not in ids:
            ids[record['connection']] = len(ids)
        wire.append(
            dict(
                target=record['target'],
                headers=record['headers'],
                connection=ids[record['connection']],
            )
        )
    if limit in ['default', '64', '2147483647']:
        cancelled = next(record for record in group if record['target'] == '/slow-body?case=expiry')
        resumed = next(record for record in group if record['target'] == '/ok?case=expired-resume')
        ended = next(
            record
            for record in closures
            if record['group'] == group_name and record['connection'] == cancelled['connection']
        )
        elapsed = (ended['time'] - cancelled['time']) * 1000
        assert 1800 <= elapsed <= 2400 and ended['time'] < resumed['time'], (
            group_name,
            limit,
            elapsed,
        )
        by_label = {row['label']: output[i] for i, row in enumerate(rows)}
        assert by_label['other-origin'] == by_label['after-drain-deadline'] == '6f6b', by_label
        if a.overlap_payload == 'valid-status':
            assert (
                by_label['/slow-redirect'] == by_label['return-to-pending-origin'] == '-'
            ), by_label
    return dict(outputs=output, wire=wire, drainDeadlineMilliseconds=elapsed)


differences = []
observations = []
try:
    for limit in ['default', '64', '0', '-1', '2147483647', '2147483648']:
        outputs = {}
        wire = {}
        drain_deadlines = {}
        for kind, command in [('source', reference), ('native', [a.native])]:
            sample = capture(kind + '-' + limit, command, limit)
            outputs[kind] = sample['outputs']
            wire[kind] = sample['wire']
            drain_deadlines[kind] = sample['drainDeadlineMilliseconds']
        for index, row in enumerate(rows):
            if outputs['source'][index] != outputs['native'][index]:
                differences.append(
                    dict(
                        limit=limit,
                        label=row['label'],
                        source=outputs['source'][index],
                        native=outputs['native'][index],
                    )
                )
        if limit in ['default', '64', '2147483647']:
            by_label = {row['label']: outputs['source'][i] for i, row in enumerate(rows)}
            assert by_label['other-origin'] == by_label['after-drain-deadline'] == '6f6b', by_label
            if a.overlap_payload == 'valid-status':
                assert (
                    by_label['/slow-redirect'] == by_label['return-to-pending-origin'] == '-'
                ), by_label
        if wire['source'] != wire['native']:
            differences.append(
                dict(limit=limit, wireSource=wire['source'], wireNative=wire['native'])
            )
        observations.append(
            dict(limit=limit, outputs=outputs, wire=wire, drainDeadlineMilliseconds=drain_deadlines)
        )
finally:
    server.shutdown()
    server.server_close()
    thread.join()
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=not differences,
            valueParityPassed=not any('label' in difference for difference in differences),
            wireParityPassed=not any('wireSource' in difference for difference in differences),
            overlapPayload=a.overlap_payload,
            diagnosticSource=os.environ.get('OPENSIM_TEST_X509_FAULTS') == '1',
            cases=len(rows) * 6,
            requests=len(records),
            inputRows=rows,
            inputSHA256=hashlib.sha256(protocol.encode()).hexdigest(),
            differences=differences,
            observations=observations,
            timings=timings,
            scope='Actual pinned Linux runtime X509ResourceClient.DownloadAsset versus native HTTP requester composition. Complete byte/null/empty results and HTTP request/header/cookie/connection reuse observations; limit configuration, status handling, redirects, cookies, framing and deadlines. Overlap variants change previous/following payloads, including a following payload that forms a valid replacement HTTP status/header and yields an empty success instead of failure. Both value and wire differences remain failing assertions. Certificate parsing/persistence/default TLS integration remain separate work.',
        ),
        indent=2,
    )
    + '\n'
)
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)*6} X509 resource downloads / {len(records)} HTTP requests')
