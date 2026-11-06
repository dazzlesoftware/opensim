import argparse, hashlib, json, socketserver, threading, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--download-manager', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
g = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
).items():
    ET.SubElement(g, k).text = v
g = ET.SubElement(project, 'ItemGroup')
for f in [
    ROOT / 'native-port/tests/config_http_typed_transport_reference.cs',
    a.download_manager.resolve(),
]:
    ET.SubElement(g, 'Compile', Include=str(f))
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs, encoding='utf-8')
subprocess.run(
    ['dotnet', 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'],
    check=True,
    capture_output=True,
)
trace = []
port = 0
values = [
    b'foo',
    b'http:foo',
    b'http:',
    b'http:?q',
    b'http:#f',
    b'http:/next',
    b'../echo',
    b'?q',
    b'#f',
    b'#',
    b'',
    b'http://',
    b'http://x:65536/',
    b' /echo ',
    b'/echo#own',
    b'/echo#',
    b'/x%41?y=%2F#z',
    b'/x%GG#%20',
    b'/x\xc3\x83\xc2\xa9%41#%20',
    b'/x\xc3\xa9%41#%20',
    b'/x\xe9%41#%20',
    b'/echo#%C3%A9%20',
    b'/echo#%GG%20',
    b'/echo?a%00b#%20',
    b'/echo?a b#f',
]
headers = [b'Location: ' + v + b'\r\n' for v in values]
headers += [
    b'Location: \r\nLocation: /echo\r\n',
    b'Location: http://\r\nLocation: /echo\r\n',
    b'Location: /echo\r\nLocation: /different\r\n',
    b'Location: /echo\r\nSet-Cookie: a=first;Path=/\r\n',
    b'Location: /echo\r\nSet-Cookie: a=\xe9;Path=/\r\n',
    b'Location: /echo\r\nSet-Cookie: a="x\xffy";Path=/\r\n',
]


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
                index = int(target[6:].split(b'?')[0])
                head = headers[index]
                body = b''
                status = b'302 Found'
            elif target.startswith(b'/chain-'):
                mode, n = target.decode().rsplit('/', 1)
                n = int(n)
                location = (mode + '/' + str(n + 1) + '#step').encode() if n < 50 else b'/echo'
                if mode == '/chain-invalid' and n == 50:
                    location = b'http://'
                head = b'Location: ' + location + b'\r\n'
                body = b''
                status = b'302 Found'
            else:
                cookie = b''
                for h in data.split(b'\r\n')[1:]:
                    if h.lower().startswith(b'cookie:'):
                        cookie = h.split(b':', 1)[1].strip()
                body = target + b'|' + cookie
                head = b''
                status = b'200 OK'
            self.request.sendall(
                b'HTTP/1.1 '
                + status
                + b'\r\n'
                + head
                + b'Content-Length: '
                + str(len(body)).encode()
                + b'\r\nConnection: close\r\n\r\n'
                + body
            )
            # Hold the response side until the client completes; do not inject a reset race.
            self.request.settimeout(None)
            while self.request.recv(4096):
                pass
        except (ConnectionError, OSError, ValueError):
            pass


s = Server(('127.0.0.1', 0), Handler)
port = s.server_address[1]
threading.Thread(target=s.serve_forever, daemon=True).start()
base = 'http://127.0.0.1:' + str(port)
urls = [base + '/case/' + str(i) for i in range(len(headers))] + [
    base + '/case/' + str(i) + '#base' for i in range(len(headers))
]
urls += [
    base + '/chain-ok/0#base',
    base + '/chain-invalid/0#base',
    'http://:pass@127.0.0.1:' + str(port) + '/chain-cap/0#base',
]


def run(command):
    result = subprocess.run(
        command, input='\n'.join(urls) + '\n', text=True, capture_output=True, timeout=120
    )
    assert result.returncode == 0, (result.returncode, result.stderr)
    return result


source = run(['dotnet', str(w / 'app/Reference.dll')])
source_trace = trace[:]
trace.clear()
native = run([a.native])
s.shutdown()
source_lines = source.stdout.splitlines()
native_lines = native.stdout.splitlines()
assert len(source_lines) == len(native_lines) == len(urls), (
    len(source_lines),
    len(native_lines),
    len(urls),
)
diff = [
    dict(url=u, source=x, native=y) for u, x, y in zip(urls, source_lines, native_lines) if x != y
]
for name, text in [
    ('inputs.txt', '\n'.join(urls) + '\n'),
    ('actual-bcl.txt', source.stdout),
    ('native.txt', native.stdout),
    ('source-stderr.txt', source.stderr),
    ('native-stderr.txt', native.stderr),
]:
    (w / name).write_text(text, encoding='utf-8')
result = dict(
    cases=len(urls),
    passed=not diff and source_trace == trace,
    differences=diff,
    source_requests=source_trace,
    native_requests=trace,
    hashes={
        n: hashlib.sha256((w / n).read_bytes()).hexdigest()
        for n in ['inputs.txt', 'actual-bcl.txt', 'native.txt']
    },
    provider='explicit empty WebProxy/direct HTTP; source per-resource default CookieContainer; Connection close responses held open until client completion',
)
(w / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(len(urls), len(diff), len(source_trace), len(trace))
print(diff[:3])
assert not diff
assert source_trace == trace, [
    (i, x, y) for i, (x, y) in enumerate(zip(source_trace, trace)) if x != y
][:3]
print('PASS exact wire traces')
