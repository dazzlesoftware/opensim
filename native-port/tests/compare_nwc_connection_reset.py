"""Compare pooled POST close/reset behavior with the bundled original XMLRPC DLL."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import xml.etree.ElementTree as ET
import xmlrpc.client

ROOT = Path(__file__).resolve().parents[2]


def build_reference(dotnet, work):
    source = (ROOT / 'OpenSim/Framework/WebUtil.cs').read_text(encoding='utf-8-sig')
    start = source.index('public static HttpClient GetNewGlobalHttpClient(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    factory = source[start:end]
    (work / 'Factory.cs').write_text('using System; using System.Net.Http; partial class Reference {\n' + factory + '\n}')
    (work / 'Reference.cs').write_text('''using System;
using System.Collections;
using System.Net;
using System.Net.Http;
using Nwc.XmlRpc;
partial class Reference {
    static SocketsHttpHandler SharedSocketsHttpHandler = new() {
        UseProxy = false, UseCookies = false, AllowAutoRedirect = true,
        MaxAutomaticRedirections = 10, AutomaticDecompression = DecompressionMethods.None,
        PooledConnectionIdleTimeout = TimeSpan.FromMilliseconds(31000),
        PooledConnectionLifetime = TimeSpan.FromSeconds(180),
        ConnectTimeout = TimeSpan.FromSeconds(120), MaxConnectionsPerServer = 32
    };
    static void Main() {
        string url;
        while ((url = Console.ReadLine()) != null) {
            try {
                using var client = GetNewGlobalHttpClient(2000);
                var reply = new XmlRpcRequest("get_region", new ArrayList { "probe" }).Send(url, client);
                Console.WriteLine(!reply.IsFault && (string)reply.Value == "ok" ? "OK" : "BAD");
            } catch (Exception error) {
                Console.WriteLine("ERROR|" + error.GetType().FullName + "|" + error.Message);
            }
        }
    }
}
''')
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    properties = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false').items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(project, 'ItemGroup')
    for name in ('Reference.cs', 'Factory.cs'):
        ET.SubElement(items, 'Compile', Include=name)
    reference = ET.SubElement(items, 'Reference', Include='XMLRPC')
    ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin/XMLRPC.dll')
    ET.ElementTree(project).write(work / 'Reference.csproj')
    result = subprocess.run([dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


class Peer(http.server.ThreadingHTTPServer):
    daemon_threads = True


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            # Peers may reset an idle connection when the test process exits.
            pass

    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        self.server.requests.append(self.client_address[1])
        index = len(self.server.requests)
        if self.server.mode == 'reset-on-second-post' and index == 2:
            linger = struct.pack('HH' if os.name == 'nt' else 'ii', 1, 0)
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
            self.close_connection = True
            self.connection.close()
            return
        body = xmlrpc.client.dumps(('ok',), methodresponse=True).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/xml')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        if self.server.mode == 'closed-before-reuse' and index == 1:
            self.connection.shutdown(socket.SHUT_WR)
            self.close_connection = True
            self.server.closed.set()


def run(command, mode, native):
    with Peer(('127.0.0.1', 0), Handler) as peer:
        peer.mode, peer.requests, peer.closed = mode, [], threading.Event()
        worker = threading.Thread(target=peer.serve_forever)
        worker.start()
        try:
            with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as process:
                replies = []
                url = f'http://127.0.0.1:{peer.server_port}/'
                encode = lambda text: text.encode('utf-16-le').hex()
                try:
                    for index in range(3):
                        line = f'0 2000 {encode("get_region")} {encode("probe")} {encode(url)}' if native else url
                        process.stdin.write(line + '\n')
                        process.stdin.flush()
                        result = process.stdout.readline().strip()
                        if native:
                            result = result.split('|', 1)[0]
                            if result.startswith('R:0:S:6f006b00'):
                                result = 'OK'
                            elif result.startswith('E:'):
                                _, kind, message = result.split(':', 2)
                                result = 'ERROR|' + kind + '|' + bytes.fromhex(message).decode('utf-16-le')
                        replies.append(result)
                        if mode == 'closed-before-reuse' and index == 0:
                            assert peer.closed.wait(2)
                            time.sleep(.02)  # Let the peer's already-issued FIN reach both runtimes.
                    process.stdin.close()
                    assert process.wait(timeout=10) == 0
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                assert len(peer.requests) == 3, (mode, replies, peer.requests)
                if mode == 'reset-on-second-post':
                    assert replies == ['OK', 'ERROR|System.Net.Http.HttpRequestException|An error occurred while sending the request.', 'OK'], replies
                    assert peer.requests[0] == peer.requests[1] and peer.requests[1] != peer.requests[2]
                else:
                    assert replies == ['OK'] * 3, replies
                    assert peer.requests[0] != peer.requests[1]
                return dict(replies=replies, requests=len(peer.requests))
        finally:
            peer.shutdown()
            worker.join()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, args.work)
    assert (hashlib.sha256((reference.parent / 'XMLRPC.dll').read_bytes()).digest() ==
            hashlib.sha256((ROOT / 'bin/XMLRPC.dll').read_bytes()).digest()), 'Original XMLRPC DLL mismatch'
    observations = []
    for mode in ('closed-before-reuse', 'reset-on-second-post'):
        original = run([args.dotnet, str(reference)], mode, False)
        native = run([args.native, '--global-client'], mode, True)
        assert original == native, (mode, original, native)
        observations.append(dict(mode=mode, original=original, native=native))
    report = dict(passed=True, observations=observations,
                  xmlrpcSha256=hashlib.sha256((ROOT / 'bin/XMLRPC.dll').read_bytes()).hexdigest(),
                  scope='Actual original XMLRPC DLL and extracted WebUtil client factory over controlled pooled POST sockets; reset after request body is sent is not retried')
    (args.work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: original/native pooled POST reuse, idle close, remote reset without replay and next-request recovery')
