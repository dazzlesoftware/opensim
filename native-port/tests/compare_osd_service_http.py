"""Original full ServiceOSDRequest against real local HTTP and native live adapters."""

from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET, http.server, threading, urllib.parse, gzip, time, hashlib, sys

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--global-factory', action='store_true')
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)


def extract(file, signature):
    s = (root / file).read_bytes().decode('utf-8-sig')
    start = s.index(signature)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


web = 'OpenSim/Framework/WebUtil.cs'
util = 'OpenSim/Framework/Util.cs'
methods = ''.join(
    extract(web, sig)
    for sig in [
        'public static OSDMap ServiceOSDRequest(string url, OSDMap data, string method, int timeout, bool compressed, bool rpc, bool keepalive = false)',
        'private static OSDMap CanonicalizeResults(string response)',
        'private static OSDMap ErrorResponseMap(string msg)',
        'public static int EstimatedReceiveTimeout(HttpContentHeaders headers, bool keepalive)',
        'public static void LogOutgoingDetail(string type, int reqnum, string output)',
        'public static void LogOutgoingDetail(string context, string output)',
        'public static void LogResponseDetail(int reqnum, string input)',
    ]
)
if a.global_factory:
    methods += ''.join(
        extract(web, sig)
        for sig in [
            'public static void SetupHTTPClients(',
            'public static HttpClient GetNewGlobalHttpClient(',
        ]
    )
binary = ''.join(
    extract(util, sig)
    for sig in [
        'public static void BinaryToASCII(char[] chars)',
        'public static string BinaryToASCII(string src)',
    ]
)
(w / 'Extracted.cs').write_text(
    'using System;using System.IO;using System.IO.Compression;using System.Net.Http;using System.Net.Http.Headers;using System.Net;using System.Net.Security;using System.Security.Authentication;using System.Security.Cryptography.X509Certificates;using System.Threading;using OpenMetaverse.StructuredData;static partial class WebUtil{'
    + methods
    + '}static partial class Util{'
    + binary
    + '}'
)
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
if a.global_factory:
    ET.SubElement(props, 'DefineConstants').text = 'GLOBAL_WEB_HTTP_FACTORY'
    ET.SubElement(props, 'NoWarn').text = 'SYSLIB0039'
items = ET.SubElement(project, 'ItemGroup')
for f in [root / 'native-port/tests/osd_service_http_reference.cs', w / 'Extracted.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / (name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
lock = threading.Lock()
records = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_POST(self):
        self.route()

    def do_GET(self):
        self.route()

    def do_PUT(self):
        self.route()

    def do_DELETE(self):
        self.route()

    def do_QUERYACCESS(self):
        self.route()

    def do_HEAD(self):
        self.route()

    def route(self):
        data = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        headers = {
            k.lower(): v
            for k, v in self.headers.items()
            if k.lower()
            in [
                'host',
                'connection',
                'content-type',
                'content-length',
                'x-content-encoding',
                'opensim-request-id',
                'keep-alive',
                'expect',
                'transfer-encoding',
                'user-agent',
                'accept',
                'accept-encoding',
                'authorization',
                'cookie',
            ]
        }
        with lock:
            records.append(
                dict(method=self.command, target=self.path, headers=headers, body=data.hex())
            )
        path = urllib.parse.urlsplit(self.path).path
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        status = 200
        reason = None
        extra = {}
        body = b'{"success":true,"reason":"accepted"}'
        if path == '/echo':
            body = json.dumps(
                dict(
                    success=True,
                    method=self.command,
                    target=self.path,
                    bodySHA256=hashlib.sha256(data).hexdigest(),
                    headers=headers,
                ),
                ensure_ascii=False,
                separators=(',', ':'),
            ).encode()
        elif path.startswith('/redirect/'):
            status = int(path.rsplit('/', 1)[1])
            extra['Location'] = query.get('to', ['/echo'])[0]
            body = b''
        elif path == '/loop':
            status = 302
            extra['Location'] = '/loop'
            body = b''
        elif path == '/set-cookie':
            status = 302
            extra['Location'] = '/echo'
            extra['Set-Cookie'] = 'secret=skip; Path=/'
            body = b''
        elif path == '/false':
            body = b'false'
        elif path == '/true':
            body = b'TRUE'
        elif path == '/empty':
            body = b''
        elif path == '/bad-json':
            body = b'not json'
        elif path == '/xml':
            body = b'<llsd><map><key>success</key><boolean>true</boolean></map></llsd>'
        elif path == '/status':
            status = int(query.get('code', ['404'])[0])
            reason = query.get('reason', [None])[0]
            body = b'ignored error'
        elif path == '/gzip-response':
            body = gzip.compress(body, mtime=0)
            extra['Content-Encoding'] = 'gzip'
        elif path.startswith('/bom/'):
            kind = path.rsplit('/', 1)[1]
            bom, codec = {
                'utf8': (b'\xef\xbb\xbf', 'utf-8'),
                'utf16le': (b'\xff\xfe', 'utf-16-le'),
                'utf16be': (b'\xfe\xff', 'utf-16-be'),
                'utf32le': (b'\xff\xfe\x00\x00', 'utf-32-le'),
                'utf32be': (b'\x00\x00\xfe\xff', 'utf-32-be'),
            }[kind]
            body = bom + body.decode().encode(codec)
        if path == '/slow-header':
            time.sleep(0.35)
        try:
            self.send_response(status, reason)
            self.send_header('Content-Type', 'application/json; charset=iso-8859-1')
            if self.headers.get('Connection', '').lower() == 'close':
                self.send_header('Connection', 'close')
            for k, v in extra.items():
                self.send_header(k, v)
            if path == '/chunked':
                self.send_header('Transfer-Encoding', 'chunked')
            elif path != '/body-timeout-eof':
                self.send_header('Content-Length', str(len(body)))
            else:
                self.send_header('Connection', 'close')
                self.close_connection = True
            self.end_headers()
            if self.command == 'HEAD':
                return
            if path == '/chunked':
                for i in range(0, len(body), 3):
                    part = body[i : i + 3]
                    self.wfile.write(('%x\r\n' % len(part)).encode() + part + b'\r\n')
                    self.wfile.flush()
                self.wfile.write(b'0\r\n\r\n')
            elif path in ['/slow-body', '/body-timeout', '/body-timeout-hard', '/body-timeout-eof']:
                self.wfile.write(body[:5])
                self.wfile.flush()
                time.sleep(
                    0.35 if path == '/slow-body' else 9 if path == '/body-timeout-hard' else 6
                )
                self.wfile.write(body[5:])
            else:
                self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


server = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = f'http://127.0.0.1:{server.server_port}'
enc = lambda s: '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'
rows = []


def add(
    path,
    flags=0,
    debug=0,
    timeout=30000,
    method='POST',
    data={'name': 'é😀\r\n<&>', 'count': 47, 'nested': [True, '']},
    absolute=False,
):
    rows.append(
        ' '.join(
            map(
                str,
                [
                    flags,
                    debug,
                    timeout,
                    enc(method),
                    enc(path if absolute else base + path),
                    enc(
                        None
                        if data is None
                        else json.dumps(data, ensure_ascii=False, separators=(',', ':'))
                    ),
                ],
            )
        )
    )


for flags in range(8):
    for level in [0, 5]:
        add('/echo', flags=flags, debug=level)
for method in ['GET', 'PUT', 'DELETE', 'QUERYACCESS', 'HEAD']:
    for data in [None, {}, {'x': 7}]:
        add('/reply', method=method, data=data)
for path in [
    '/reply',
    '/true',
    '/false',
    '/empty',
    '/bad-json',
    '/xml',
    '/chunked',
    '/set-cookie',
    '/gzip-response',
    '/loop',
    '/status?code=404',
    '/status?code=500&reason=',
    '/status?code=503&reason=Custom',
    '/bom/utf8',
    '/bom/utf16le',
    '/bom/utf16be',
    '/bom/utf32le',
    '/bom/utf32be',
]:
    add(path, debug=5)
for status in [301, 302, 303, 307, 308]:
    add(f'/redirect/{status}?to=/echo', flags=1)
for url in [
    None,
    '',
    'relative/path',
    'ftp://example.test/path',
    'http://127.0.0.1:bad/path',
    'http://[invalid/',
]:
    add(url, absolute=True, debug=5)
for method in [None, '', ' ', 'GET X', '\r\n', 'é']:
    add('/reply', method=method, debug=5)
add('/echo', data={'x': 'a' * 131073})
add('/slow-header', timeout=100)
add('/slow-body', timeout=100)
add('/body-timeout')
add('/body-timeout-hard')
add('/body-timeout-eof', flags=4)
outputs = {}
captures = {}
try:
    for label, cmd in [
        ('source', [a.dotnet, str(w / 'app/Reference.dll')]),
        ('native', [a.native] + (['--global'] if a.global_factory else [])),
    ]:
        with lock:
            records.clear()
        r = subprocess.run(
            cmd,
            input='\n'.join(rows) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=180,
        )
        assert r.returncode == 0, r.stdout[-2000:] + r.stderr
        outputs[label] = r.stdout.splitlines()
        assert len(outputs[label]) == len(rows), (label, len(outputs[label]), len(rows))
        with lock:
            captures[label] = list(records)
finally:
    server.shutdown()
    server.server_close()
# A Linux synchronous receive racing SocketsHttpHandler's drain disposal can
# report either local SocketException(IOException) or a zero-byte receive that
# ContentLengthReadStream wraps as HttpIOException(ResponseEnded). Both were
# observed from the unchanged BCL implementation. Validate the complete failure
# map and both diagnostic lines; normalize only that verified race's suffix.
race_normalized = []


def comparable(row, output):
    if not sys.platform.startswith('linux') or not output.startswith('R:'):
        return output
    encoded_url = row.split(' ')[4]
    if encoded_url in ['-', '.']:
        return output
    url = bytes.fromhex(encoded_url).decode('utf-16le', 'surrogatepass')
    if url.endswith('/body-timeout-eof'):
        # Concurrent Dispose of Linux ConnectionCloseReadStream was observed both
        # as EOF after the fixture's five-byte prefix and as a local socket abort.
        # Require the exact success map or the complete failure/log pair; retain
        # raw observations below and keep every other path under exact comparison.
        result, logs = output.split('|')
        text = bytes.fromhex(result[2:]).decode('utf-16le', 'surrogatepass')
        trace = '' if logs == '.' else bytes.fromhex(logs).decode('utf-16le', 'surrogatepass')
        value = json.loads(text)
        if (
            list(value) == ['Success', 'success', '_RawResult', '_Result']
            and value.get('Success') is True
            and value.get('success') is True
            and value == {'Success': True, 'success': True, '_RawResult': '{"suc', '_Result': {}}
            and trace == ''
        ):
            return '<verified BCL Linux close-delimited disposal outcome>'
        number = rows.index(row)
        method = bytes.fromhex(row.split(' ')[3]).decode('utf-16le', 'surrogatepass')
        reason = (
            'Unable to read data from the transport connection: Software caused connection abort.'
        )
        prefix = f'[WEB UTIL]: receive timeout {number} {method} {url} (0ms); '
        expected_trace = f'[WEB UTIL]: Exception making request: {reason}\n[LOGHTTP]: request {number} {method} to {url} FAILED: {prefix}{reason}'
        if (
            list(value) == ['Success', 'Message']
            and value
            == {'Success': 'False', 'Message': 'Service request failed: ' + prefix + reason}
            and trace == expected_trace
        ):
            return '<verified BCL Linux close-delimited disposal outcome>'
        return output
    if not url.endswith('/body-timeout-hard'):
        return output
    result, logs = output.split('|')
    text = bytes.fromhex(result[2:]).decode('utf-16le', 'surrogatepass')
    trace = bytes.fromhex(logs).decode('utf-16le', 'surrogatepass')
    value = json.loads(text)
    allowed = [
        'Unable to read data from the transport connection: Software caused connection abort.',
        'The response ended prematurely, with at least '
        + str(len(b'{"success":true,"reason":"accepted"}') - 5)
        + ' additional bytes expected. (ResponseEnded)',
    ]
    for reason in allowed:
        if (
            list(value) == ['Success', 'Message']
            and value['Success'] == 'False'
            and value['Message'].endswith(reason)
        ):
            assert (
                'receive timeout ' in value['Message']
                and trace.count(reason) == 2
                and text.count(reason) == 1
            )
            token = '<verified BCL local-close read failure>'
            return (
                'R:'
                + text.replace(reason, token).encode('utf-16le', 'surrogatepass').hex()
                + '|'
                + trace.replace(reason, token).encode('utf-16le', 'surrogatepass').hex()
            )
    return output


raw_diffs = [
    dict(index=i, command=c, source=s, native=n)
    for i, (c, s, n) in enumerate(zip(rows, outputs['source'], outputs['native']))
    if s != n
]
diffs = []
for i, (c, s, n) in enumerate(zip(rows, outputs['source'], outputs['native'])):
    if comparable(c, s) != comparable(c, n):
        diffs.append(dict(index=i, command=c, source=s, native=n))
    elif s != n:
        race_normalized.append(i)
wire = []
for i in range(max(len(captures['source']), len(captures['native']))):
    s = captures['source'][i] if i < len(captures['source']) else None
    n = captures['native'][i] if i < len(captures['native']) else None
    if s != n:
        wire.append(dict(index=i, source=s, native=n))
report = dict(
    globalFactory=a.global_factory,
    passed=not diffs and not wire,
    scenarios=len(rows),
    sourceRequests=len(captures['source']),
    nativeRequests=len(captures['native']),
    differences=diffs,
    wireDifferences=wire[:12],
    wireDifferenceCount=len(wire),
    rawOutcomeDifferenceCount=len(raw_diffs),
    sourceCloseRaceNormalizedCases=race_normalized,
    rawOutcomeDifferences=raw_diffs,
    scope='Original full ServiceOSDRequest/canonical/error/detail/adaptive timeout helpers, actual SDK/BCL HttpClient/StreamReader/GZip/CTS and native live HTTP. Explicit direct/global client/log/tick boundary; elapsed fixed0, duration logs filtered; Linux hard-timeout local-close suffix admits the two verified BCL read/close race errors while full failure map/log structure stays strict and raw deltas are recorded; no connection pool/TLS/proxy/general provided-client surface claim',
    applicationParityVerified=False,
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['passed'], f'{len(diffs)} outcome and {len(wire)} wire mismatches; see ' + str(
    w / 'results.json'
)
print(f"PASS: {len(rows)} real OSD HTTP cases, {len(captures['native'])} exact wire requests")
