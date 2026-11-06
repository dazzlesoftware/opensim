"""Actual original requester on gated HTTP bodies versus native header decisions."""

import argparse, gzip, hashlib, http.server, json, queue, re, subprocess, tempfile, threading, urllib.parse, zlib, xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT, extract


def build_reference(dotnet, w):
    w = Path(w).resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/WebUtil.cs'
    method = extract(
        source.read_text(encoding='utf-8-sig'),
        r'public static string MakeRequest\(string method, string requestUrl, string obj, int timeoutsecs = -1,',
    )
    original = source.read_text(encoding='utf-8-sig')
    for pattern in [
        r'MaxAutomaticRedirections\s*=\s*10',
        r'AutomaticDecompression\s*=\s*DecompressionMethods.None',
        r'UseCookies\s*=\s*false',
        r'ConnectTimeout\s*=\s*TimeSpan.FromSeconds\(120\)',
    ]:
        assert re.search(pattern, original), pattern
    timeout = extract(
        original,
        r'public static int EstimatedReceiveTimeout\(HttpContentHeaders headers, bool keepalive\)',
    )
    wrapper = w / 'Requester.cs'
    wrapper.write_text(
        'using System;using System.Net.Http;using System.Net.Http.Headers;using System.IO;using System.Text;using System.Threading;using OpenSim.Framework.ServiceAuth;namespace OpenSim.Framework{public static class SynchronousRestFormsRequester{static log4net.ILog m_log=new log4net.Logger();'
        + method
        + '}public static partial class WebUtil{'
        + timeout
        + '}}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [wrapper, ROOT / 'native-port/tests/rest_forms_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = w / 'RestForms.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    return w / 'app/RestForms.dll', source, method


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--default-native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    reference, source, method = build_reference(a.dotnet, w)
    text = 'reply<&>試験 🦊\r\n\t'
    body = text.encode()
    cases = [
        ('no-length', False, 'OK|'),
        ('chunked', False, 'OK|'),
        ('zero', False, 'OK|'),
        ('zero-padded', False, 'OK|'),
        ('error-length', False, 'EX'),
        ('error-no-length', False, 'EX'),
        ('no-content', False, 'OK|'),
        ('normal', True, 'OK|' + body.hex()),
        ('utf16', True, 'OK|' + body.hex()),
        ('short', True, 'EX'),
    ]
    observations = []
    differences = []
    lengths = {
        'invalid': ['bad'],
        'negative': ['-1'],
        'plus': ['+' + str(len(body))],
        'overflow': [str(2**63)],
        'uint64-max': [str(2**64 - 1)],
        'empty-length': [''],
        'comma': [f'{len(body)},{len(body)}'],
        'duplicate-conflict': [str(len(body)), str(len(body) + 5)],
        'duplicate-same': [str(len(body))] * 2,
        'invalid-first': ['bad', str(len(body))],
        'invalid-last': [str(len(body)), 'bad'],
        'zero-first': ['0', str(len(body))],
        'zero-last': [str(len(body)), '0'],
        'padded-positive': ['0' + str(len(body))],
        'space-length': [' ' + str(len(body)) + ' '],
        'signed-max': [str(2**63 - 1)],
        'chunked-length': [str(len(body))],
        'chunked-invalid': ['bad'],
    }
    positive = {
        'duplicate-conflict',
        'duplicate-same',
        'invalid-last',
        'zero-last',
        'padded-positive',
        'space-length',
        'chunked-length',
    }
    cases += [
        (
            name,
            name in positive or name == 'signed-max',
            'EX' if name == 'signed-max' else 'OK|' + body.hex() if name in positive else 'OK|',
        )
        for name in lengths
    ]
    lengths['invalid-timeout'] = [str(((1 << 32) - 5002) * 1024)]
    cases.append(('invalid-timeout', False, 'EX'))
    cases += [(name, True, None) for name in ['gzip', 'deflate']]

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        state = None

        def log_message(self, *args):
            pass

        def do_POST(self):
            state = self.state
            state['request'] = (
                self.command,
                self.path,
                dict(self.headers),
                self.rfile.read(int(self.headers.get('Content-Length', '0'))).decode(),
            )
            profile = state['profile']
            data = b'\xff\xfe' + text.encode('utf-16-le') if profile == 'utf16' else body
            if profile == 'gzip':
                data = gzip.compress(body, mtime=0)
            if profile == 'deflate':
                data = zlib.compress(body)
            self.send_response(
                401 if profile.startswith('error-') else 204 if profile == 'no-content' else 200
            )
            self.send_header('Content-Type', 'application/xml')
            if profile in ['gzip', 'deflate']:
                self.send_header('Content-Encoding', profile)
            if profile in ['chunked', 'chunked-length', 'chunked-invalid']:
                self.send_header('Transfer-Encoding', 'chunked')
            if profile in lengths:
                for value in lengths[profile]:
                    self.send_header('Content-Length', value)
            elif profile == 'chunked':
                pass
            elif profile in ['zero', 'zero-padded']:
                self.send_header('Content-Length', '0000' if profile == 'zero-padded' else '0')
            elif profile not in ['no-length', 'error-no-length', 'no-content']:
                self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.flush()
            state['headers'].set()
            state['release'].wait(timeout=10)
            try:
                if profile in ['chunked', 'chunked-length', 'chunked-invalid']:
                    self.wfile.write(f'{len(data):x}\r\n'.encode() + data + b'\r\n0\r\n\r\n')
                elif profile != 'short':
                    self.wfile.write(data)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
            finally:
                self.close_connection = True
                state['done'].set()

    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            for profile, waits, expected in cases:
                rows = []
                commands = [('source', [a.dotnet, str(reference)]), ('native', [a.native])]
                if profile in ['duplicate-conflict', 'chunked-length']:
                    commands.append(('default-client', [a.default_native]))
                for name, command in commands:
                    expected_value = 'EX' if name == 'default-client' else expected
                    wait_for_body = False if name == 'default-client' else waits
                    state = dict(
                        profile=profile,
                        headers=threading.Event(),
                        release=threading.Event(),
                        done=threading.Event(),
                    )
                    Handler.state = state
                    output = queue.Queue()
                    process = subprocess.Popen(
                        command + [f'http://127.0.0.1:{server.server_port}/forms', 'METHOD=probe'],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                        encoding='utf-8',
                    )
                    reader = threading.Thread(
                        target=lambda: output.put(process.stdout.readline().strip()), daemon=True
                    )
                    reader.start()
                    early = None
                    try:
                        assert state['headers'].wait(timeout=10), (
                            profile,
                            name,
                            'no response headers',
                        )
                        if wait_for_body:
                            try:
                                early = output.get(timeout=0.2)
                            except queue.Empty:
                                pass
                            assert early is None, (
                                profile,
                                name,
                                'completed before body release',
                                early,
                            )
                            state['release'].set()
                            row = output.get(timeout=10)
                        else:
                            row = output.get(timeout=5)
                            assert not state['release'].is_set()
                            state['release'].set()
                        process.wait(timeout=10)
                        assert process.returncode == 0, (profile, name, process.stderr.read())
                        if expected_value is not None:
                            assert row == expected_value, (profile, name, row, expected_value)
                        else:
                            assert row.startswith('OK|') and row != 'OK|' + body.hex(), (
                                profile,
                                name,
                                'compressed body was decoded or rejected',
                                row,
                            )
                        if name != 'default-client':
                            rows.append(row)
                        assert state['done'].wait(timeout=5)
                        verb, path, headers, request = state['request']
                        headers = {k.lower(): v for k, v in headers.items()}
                        assert verb == 'POST' and path == '/forms' and request == 'METHOD=probe'
                        assert headers.get('content-type') == 'application/x-www-form-urlencoded'
                        if name != 'default-client':
                            assert (
                                headers.get('keep-alive') == 'timeout=30, max=10'
                                and headers.get('connection', '').lower() == 'keep-alive'
                            )
                            assert all(
                                k not in headers
                                for k in ['accept', 'accept-encoding', 'user-agent']
                            ), (profile, name, headers)
                        else:
                            assert headers.get('accept') == '*/*' and headers.get(
                                'user-agent', ''
                            ).startswith('cpp-httplib/'), (profile, name, headers)
                        observations.append(
                            dict(
                                profile=profile,
                                implementation=name,
                                completedBeforeBody=not wait_for_body,
                                result=row,
                            )
                        )
                    finally:
                        state['release'].set()
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=5)
                        reader.join(timeout=2)
                        process.stdout.close()
                        process.stderr.close()
                if rows[0] != rows[1]:
                    differences.append(dict(profile=profile, source=rows[0], native=rows[1]))
        finally:
            server.shutdown()
            worker.join(timeout=5)
    report = dict(
        passed=not differences,
        observations=len(observations),
        profiles=len(cases),
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        methodSha256=hashlib.sha256(method.encode()).hexdigest(),
        scope='Exact original SynchronousRestFormsRequester.MakeRequest POST method and actual .NET HttpClient versus native transport over gated HTTP sockets. Factory, clock/logging/auth configuration are controlled; the receive timeout calculation is extracted from source. No-length/chunked/zero replies and final HTTP errors complete before body release; positive-length UTF8/BOM reads wait and truncated bodies fail. The invalid receive timeout exits before reading. Detailed cancellation timing and exception text remain outside this fixture.',
        results=observations,
        differences=differences,
    )
    (w / 'rest-forms-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, differences
    print(f'PASS: {len(observations)} original/native gated response-lifetime observations')


if __name__ == '__main__':
    main()
