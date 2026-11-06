from pathlib import Path
import argparse, subprocess, socketserver, threading, socket, json, hashlib, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--nini', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
lab = Path(__file__).resolve().parent
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in ['config_xml_document_network_reference.cs']:
    ET.SubElement(items, 'Compile', Include=str(lab / f))
r = ET.SubElement(items, 'Reference', Include='Nini')
ET.SubElement(r, 'HintPath').text = str(a.nini.resolve())
cs = w / 'Reference.csproj'
ET.ElementTree(project).write(cs)
subprocess.run(
    [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v:q'], check=True, capture_output=True
)
body = "<!DOCTYPE Nini [<!ATTLIST Key Value CDATA 'Native Remote'>]><Nini><Section Name='Startup'><Key Name='Name'/><Key Name='Greeting' Value='é中'/></Section></Nini>".encode()
trace = []


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            data = b''
            while b'\r\n\r\n' not in data:
                b = self.request.recv(4096)
                if not b:
                    return
                data += b
            trace.append(data.hex())
            target = data.split(b' ')[1]
            if target == b'/start':
                self.request.sendall(
                    b'HTTP/1.1 302 Found\r\nLocation: /config\r\nSet-Cookie: pipeline=yes; Path=/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
                )
            else:
                assert b'Cookie: pipeline=yes\r\n' in data, data
                self.request.sendall(
                    b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'
                )
                for i in range(0, len(body), 3):
                    chunk = body[i : i + 3]
                    self.request.sendall(
                        (hex(len(chunk))[2:] + ';x=y\r\n').encode() + chunk + b'\r\n'
                    )
                self.request.sendall(b'0\r\nX-End: yes\r\n\r\n')
            while self.request.recv(4096):
                pass
        except (ConnectionError, OSError):
            pass


s = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=s.serve_forever, daemon=True).start()
payload = 'http://127.0.0.1:' + str(s.server_address[1]) + '/start\n'
outputs = {}
traces = {}
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    trace.clear()
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True, timeout=60)
    assert r.returncode == 0, (name, r.stderr)
    outputs[name] = r.stdout
    traces[name] = trace[:]
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
s.shutdown()
expected = (
    ''.join(f'{ord(c):04x}' for c in 'Native Remote')
    + ':'
    + ''.join(f'{ord(c):04x}' for c in 'é中')
    + '\n'
)
report = {
    'output': outputs,
    'wire': traces,
    'passed': outputs['reference'] == outputs['native'] == expected
    and traces['reference'] == traces['native'],
    'applicationParityVerified': False,
    'scope': 'Real direct HTTP redirect+cookie+chunk extension/trailer+UTF8+DTD default+actual bundled Nini.dll XmlConfigSource(url)/System.Xml versus native downloaded XML configuration source; OS proxy/TLS/auth/pooling remain incomplete.',
    'niniSHA256': hashlib.sha256(a.nini.read_bytes()).hexdigest(),
}
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert report['passed'], report
print('PASS native HTTP -> buffered XML reader -> Nini configuration; exact2-request trace')
