import argparse, json, subprocess, threading, http.server, socketserver, xml.etree.ElementTree as ET, urllib.parse
from pathlib import Path

a = argparse.ArgumentParser()
a.add_argument('--native', required=True)
a.add_argument('--work', type=Path, required=True)
a.add_argument('--download-manager', type=Path, required=True)
a = a.parse_args()
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
    root / 'native-port/tests/config_xml_transport_network_reference.cs',
    a.download_manager.resolve(),
]:
    ET.SubElement(g, 'Compile', Include=str(f))
cs = w / 'XmlActualHttp.csproj'
ET.ElementTree(project).write(cs, encoding='utf-8')
subprocess.run(
    ['dotnet', 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'],
    check=True,
    capture_output=True,
)
trace = []


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def handle(self):
        try:
            super().handle()
        except ConnectionResetError:
            pass

    def log_message(self, *args):
        pass

    def do_GET(self):
        trace.append(dict(target=self.path, headers=list(self.headers.items())))
        parts = self.path.split('/')
        status = 200
        body = bytes([0, 255, 13, 10, 65])
        headers = []
        if parts[1] == 'status':
            status = int(parts[2])
            body = b'status-body'
        if parts[1] == 'redirect':
            status = int(parts[2])
            headers.append(('Location', '/bytes'))
        if parts[1] == 'loop':
            status = 302
            headers.append(('Location', '/loop'))
        if parts[1] == 'cookie':
            status = 302
            headers.extend([('Location', '/echo'), ('Set-Cookie', 'first=one; Path=/')])
        if parts[1] == 'jar':
            case = int(parts[2])
            status = 302
            headers.append(('Location', '/echo'))
            cases = [
                ['a=one;Path=/', 'b=two;Path=/'],
                ['a=one;Secure;Path=/', 'b=two;Path=/'],
                ['a=one;Path=/other', 'b=two;Path=/'],
                ['a=one;Max-Age=0;Path=/', 'b=two;Path=/'],
                ['a=one;Path=/, $bad=two', 'b=two;Path=/'],
                ['a=one;Path=/, z=' + ('x' * 4097), 'b=two;Path=/'],
                ['a=one;Path=/', 'A=two;Path=/'],
                ['a=one;Version=1;Path=/', 'b=two;Path=/'],
                ['a=one;Port="80";Path=/', 'b=two;Path=/'],
                ['a=one;Domain=127.0.0.1;Path=/', 'b=two;Path=/'],
                ['a=one;Expires=Sat, 01 Jan 2000 00:00:00 GMT;Path=/', 'b=two;Path=/'],
                ['a=one;Path=/', 'a=removed;Max-Age=0;Path=/'],
            ]
            headers.extend(('Set-Cookie', v) for v in cases[case])
        if parts[1] == 'echo':
            body = self.headers.get('Cookie', '').encode()
        if parts[1] == 'gzip':
            body = b'\x1f\x8braw'
            headers.append(('Content-Encoding', 'gzip'))
        self.send_response(status)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if status not in [204, 304]:
            self.wfile.write(body)


s = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=s.serve_forever, daemon=True).start()
base = f'http://127.0.0.1:{s.server_port}'
paths = (
    ['/bytes', '/gzip', '/cookie', '/echo']
    + [
        '/status/' + str(n)
        for n in [
            200,
            201,
            204,
            206,
            299,
            300,
            301,
            302,
            304,
            307,
            308,
            400,
            401,
            403,
            404,
            407,
            418,
            500,
        ]
    ]
    + ['/redirect/' + str(n) for n in [300, 301, 302, 303, 304, 305, 306, 307, 308]]
    + ['/loop', '/bytes?x=a%20b#ignored']
)
paths += ['/jar/' + str(i) for i in range(12)] + ['/echo']
urls = [base + x for x in paths]
r = subprocess.run(
    ['dotnet', str(w / 'app/XmlActualHttp.dll')],
    input='\n'.join(urls) + '\n',
    capture_output=True,
    text=True,
    timeout=120,
)
assert r.returncode == 0, (r.stdout, r.stderr)
source_trace = list(trace)
trace.clear()
rows = []
for path in paths:
    parsed = urllib.parse.urlsplit(path)
    rows.append(
        str(s.server_port) + ' ' + parsed.path + ('?' + parsed.query if parsed.query else '')
    )
native = subprocess.run(
    [a.native], input='\n'.join(rows) + '\n', capture_output=True, text=True, timeout=120
)
s.shutdown()
assert native.returncode == 0, (native.stdout, native.stderr)
differences = [
    dict(path=path, source=source, native=actual)
    for path, source, actual in zip(paths, r.stdout.splitlines(), native.stdout.splitlines())
    if source != actual
]
assert len(r.stdout.splitlines()) == len(native.stdout.splitlines()) == len(paths)
data = dict(
    passed=not differences,
    rows=list(zip(paths, r.stdout.splitlines(), native.stdout.splitlines())),
    source_requests=source_trace,
    native_requests=trace,
    differences=differences,
)
(w / 'source-native-http-results.json').write_text(json.dumps(data, indent=2) + '\n')
assert not differences, differences
assert source_trace == trace, [(a, b) for a, b in zip(source_trace, trace) if a != b][:3]
print('PASS', len(paths), 'actual HTTP resources and', len(trace), 'request/header observations')
