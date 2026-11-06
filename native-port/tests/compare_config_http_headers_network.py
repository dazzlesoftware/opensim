import argparse, json, time, socket, socketserver, threading, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--native', required=True)
ap.add_argument('--work', type=Path, required=True)
ap.add_argument('--download-manager', type=Path, required=True)
a = ap.parse_args()
root = Path(__file__).resolve().parents[2]
p = root / 'native-port/build/config-xml-transport-preparation'
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
).items():
    ET.SubElement(props, k).text = v
g = ET.SubElement(project, 'ItemGroup')
for f in [
    root / 'native-port/tests/config_http_headers_network_reference.cs',
    a.download_manager.resolve(),
]:
    ET.SubElement(g, 'Compile', Include=str(f))
cs = w / 'HeaderNetwork.csproj'
ET.ElementTree(project).write(cs, encoding='utf-8')
subprocess.run(
    ['dotnet', 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'],
    capture_output=True,
    check=True,
)
responses = []
labels = []


def add(label, head):
    labels.append(label)
    responses.append(head)


def response(headers=b'', status=b'HTTP/1.1 302 Found'):
    return status + b'\r\n' + headers + b'Content-Length: 0\r\nConnection: close\r\n\r\n'


for i in range(256):
    add(
        'cookie-value-' + str(i),
        response(b'Location: /echo\r\nSet-Cookie: a=x' + bytes([i]) + b'y;Path=/\r\n'),
    )
for i in range(256):
    add('location-' + str(i), response(b'Location: /x' + bytes([i]) + b'y\r\n'))
for value in [
    'c2a9',
    'c3a9',
    'e282ac',
    'f09f9982',
    'eda080',
    'f4908080',
    'c080',
    'efbbbf',
    'e180',
    'c2',
]:
    add('location-multi-' + value, response(b'Location: /' + bytes.fromhex(value) + b'\r\n'))
for value in [
    '0',
    '1',
    '2',
    '3',
    '4',
    '01',
    'bad',
    '-1',
    '+1',
    '1,1',
    '9223372036854775807',
    '9223372036854775808',
    '00000000000000000000',
]:
    add(
        'body-cl-' + value,
        response(b'Content-Length: ' + value.encode() + b'\r\n', b'HTTP/1.1 200 OK') + b'abc',
    )
for coding in [
    'gzip',
    'chunked; a=b',
    'gzip, chunked',
    'chunked, gzip',
    'chunked, bad@',
    'bad@, chunked',
]:
    add(
        'body-te-' + coding,
        response(b'Transfer-Encoding: ' + coding.encode() + b'\r\n', b'HTTP/1.1 200 OK')
        + b'3\r\nabc\r\n0\r\n\r\n',
    )
for body in [
    b'3\r\nabc\r\n0\r\n\r\n',
    b'3\nabc\n0\n\n',
    b'0\r\n\r\n',
    b'0\n\n',
    b'1',
    b'1\r\na',
    b'1\r\na\r\n',
    b'',
    b'g\r\n',
    b'1x\r\na\r\n0\r\n\r\n',
    b'1 \t;anything\r\na\r\n0\r\n\r\n',
    b'1\r\naX\r\n0\r\n\r\n',
    b'0\r\nX-Test: value\r\n\r\n',
    b'0\r\nInvalid Name: value\r\n\r\n',
    b'0\r\nX-Test: a\r\n b\r\n\r\n',
    b'fffffffffffffffff\r\n',
    b'0' * 16383 + b'\n\r\n',
    b'0' * 16384 + b'\n\r\n',
]:
    add(
        'chunk-' + body[:40].hex() + '-' + str(len(body)),
        response(b'Transfer-Encoding: chunked\r\n', b'HTTP/1.1 200 OK') + body,
    )
for value in [b'OK', b' ', b'\t', b'\xa0', b'\x85', b'x\xffy', b'x\ry']:
    add('reason-404-' + value.hex(), response(b'', b'HTTP/1.1 404 ' + value))
for i in range(256):
    byte = bytes([i])
    for label, body in [
        ('extension-prefix', b'1' + byte + b';x\r\na\r\n0\r\n\r\n'),
        ('extension-suffix', b'1;' + byte + b'\r\na\r\n0\r\n\r\n'),
        ('terminator', b'1\r\na' + byte + b'\r\n0\r\n\r\n'),
        ('trailer-name', b'0\r\nX' + byte + b'Y: value\r\n\r\n'),
        ('trailer-value', b'0\r\nX: a' + byte + b'b\r\n\r\n'),
        ('data', b'1\r\n' + byte + b'\r\n0\r\n\r\n'),
    ]:
        add(
            'chunk-byte-' + label + '-' + str(i),
            response(b'Transfer-Encoding: chunked\r\n', b'HTTP/1.1 200 OK') + body,
        )
for i in range(256):
    add('reason-404-byte-' + str(i), response(b'', b'HTTP/1.1 404 a' + bytes([i]) + b'b'))
for code in [100, 101, 102, 103, 199]:
    for header in [
        b'X: value\r\n',
        b'Bad name: value\r\n',
        b': value\r\n',
        b'X\xff: value\r\n',
        b'garbage\r\n',
    ]:
        raw = (
            b'HTTP/1.1 '
            + str(code).encode()
            + b' Info\r\n'
            + header
            + b'\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
        )
        add('info-' + str(code) + '-' + header.hex(), raw)
for size in [65400, 65500, 66000]:
    add(
        'trailer-limit-' + str(size),
        response(b'Transfer-Encoding: chunked\r\n', b'HTTP/1.1 200 OK')
        + b'0\r\nX: '
        + b'a' * size
        + b'\r\n\r\n',
    )
for first, last in [(32000, 32000), (32740, 32740), (33000, 33000)]:
    add(
        'info-budget-' + str(first) + '-' + str(last),
        b'HTTP/1.1 100 Info\r\nX: '
        + b'a' * first
        + b'\r\n\r\n'
        + response(b'X: ' + b'b' * last + b'\r\n', b'HTTP/1.1 200 OK'),
    )
for amount in [65535, 65536]:
    prefix = b'HTTP/1.1 100 Info\r\nX: '
    suffix = b'\r\n\r\n'
    first = prefix + b'a' * (amount - len(prefix) - len(suffix)) + suffix
    add('info-budget-exact-' + str(amount), first + response(b'', b'HTTP/1.1 200 OK'))
    prefix = b'Transfer-Encoding: chunked\r\nX: '
    head = response(prefix + b'\r\n', b'HTTP/1.1 200 OK')
    head = response(prefix + b'a' * (amount - len(head)) + b'\r\n', b'HTTP/1.1 200 OK')
    assert len(head) == amount
    add('trailer-limit-exact-' + str(amount), head + b'0\r\n\r\n')
for raw in [
    b'',
    b'HTTP/',
    b'HTTP/1.1 200 OK',
    b'HTTP/1.1 200 OK\r\n',
    b'HTTP/1.1 200 OK\r\nX: a',
    b'HTTP/1.1 200 OK\r\nX: a\r\n',
    b'HTTP/1.1 200 OK\r\n\r\n',
    b'HTTP/2.0 200 OK\r\n\r\n',
]:
    add('header-eof-' + raw.hex(), raw)
for size in [65450, 65500, 65535, 65536, 65537, 66000]:
    add('header-limit-' + str(size), response(b'X: ' + b'a' * size + b'\r\n', b'HTTP/1.1 200 OK'))
trace = []


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            data = b''
            while b'\r\n\r\n' not in data:
                b = self.request.recv(65536)
                if not b:
                    return
                data += b
            trace.append(data.hex())
            target = data.split(b' ')[1]
            if target.startswith(b'/case/'):
                index = int(target[6:])
                self.request.sendall(responses[index])
                label = labels[index]
                requires_eof = label.startswith(
                    ("header-eof-", "body-cl-", "chunk-byte-extension-", "chunk-byte-terminator-")
                ) or (label.startswith("chunk-") and not label.startswith("chunk-byte-"))
                if requires_eof:
                    self.request.shutdown(socket.SHUT_WR)
                self.request.settimeout(None)
                try:
                    while self.request.recv(4096):
                        pass
                except OSError:
                    pass
                return
            cookie = b''
            for h in data.split(b'\r\n')[1:]:
                if h.lower().startswith(b'cookie:'):
                    cookie = h.split(b':', 1)[1].strip()
            body = target + b'|' + cookie
            self.request.sendall(
                b'HTTP/1.1 200 OK\r\nContent-Length: '
                + str(len(body)).encode()
                + b'\r\nConnection: close\r\n\r\n'
                + body
            )
        except (ConnectionError, OSError):
            pass


s = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=s.serve_forever, daemon=True).start()
port = s.server_address[1]
paths = ['/case/' + str(i) for i in range(len(responses))]
urls = ['http://127.0.0.1:' + str(port) + path for path in paths]
source_lines = []
source_errors = []
# Large near-limit heads use fresh child processes so their provider observation
# is independent of previous unrelated HTTP pool/JIT/native socket state.
for batch in [urls[:-6]] + [[v] for v in urls[-6:]]:
    process = subprocess.run(
        ['dotnet', str(w / 'app/HeaderNetwork.dll')],
        input='\n'.join(batch) + '\n',
        text=True,
        capture_output=True,
        timeout=90,
    )
    assert process.returncode == 0, process.stderr
    source_lines.extend(process.stdout.splitlines())
    source_errors.append(process.stderr)
(w / 'source-stderr.txt').write_text(''.join(source_errors), encoding='utf-8')
source_trace = trace[:]
trace.clear()
native_lines = []
rows = [str(port) + ' ' + path for path in paths]
for batch in [rows[:-6]] + [[v] for v in rows[-6:]]:
    process = subprocess.run(
        [a.native], input='\n'.join(batch) + '\n', text=True, capture_output=True, timeout=90
    )
    assert process.returncode == 0, process.stderr
    native_lines.extend(process.stdout.splitlines())
s.shutdown()
source = type('Result', (), {'stdout': '\n'.join(source_lines)})()
native = type('Result', (), {'stdout': '\n'.join(native_lines)})()
source = source.stdout.splitlines()
native = native.stdout.splitlines()
assert len(source) == len(native) == len(responses), (len(source), len(native))
diff = [
    dict(label=l, source=s, native=n, response=r.hex())
    for l, s, n, r in zip(labels, source, native, responses)
    if s != n
]
(w / 'results.json').write_text(
    json.dumps(
        dict(
            cases=len(responses),
            passed=not diff,
            differences=diff,
            largeHeadersUseFreshChildrenAndNoFin=True,
            sourceDefaultProxy="controlled empty WebProxy",
            delivery="FIN only for EOF-dependent scenarios; otherwise peer stays open until client completion",
            source_requests=source_trace,
            native_requests=trace,
        ),
        indent=2,
    )
    + '\n',
    encoding='utf-8',
)
print(
    len(responses), len(diff), [(d['label'], d['source'][:70], d['native'][:70]) for d in diff[:10]]
)

assert not diff, [(d["label"], d["source"], d["native"]) for d in diff[:3]]
assert source_trace == trace, [
    (i, s, n) for i, (s, n) in enumerate(zip(source_trace, trace)) if s != n
][:3]
print("PASS exact source/native wire traces", len(trace))
