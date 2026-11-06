"""Composed unchanged grid login/CreateAgent/circuit packing/ServiceOSDRequest over real HTTP.

Global direct client, logging and clocks are explicit fixture boundaries. This
does not establish production installation, pooling, TLS or macOS behavior.
"""

from pathlib import Path
import argparse, hashlib, http.server, json, subprocess, threading
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
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


base, paths = reference(a.dotnet, w / 'base')
project = ET.parse(base)
props = project.getroot().find('PropertyGroup')
for k, v in dict(
    StartupObject='GridNetworkReference', RuntimeFrameworkVersion='8.0.31', RollForward='Disable'
).items():
    ET.SubElement(props, k).text = v
group = project.getroot().find('ItemGroup')
util = w / 'base/Util.cs'
util.write_text(
    util.read_text().replace('public static class Util', 'public static partial class Util')
)
agent = 'OpenSim/Services/Connectors/Hypergrid/UserAgentServiceConnector.cs'
sim = 'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs'
web = 'OpenSim/Framework/WebUtil.cs'
us = ''.join(
    extract(agent, s)
    for s in [
        'public UserAgentServiceConnector(string url)',
        'private bool setServiceURL(string url)',
        'protected override string AgentPath()',
        'protected override void PackData(OSDMap args, GridRegion source, AgentCircuitData aCircuit, GridRegion destination, uint flags)',
        'public bool LoginAgentToGrid(GridRegion source, AgentCircuitData aCircuit, GridRegion gatekeeper, GridRegion destination, bool fromLogin, out string reason)',
    ]
).replace('public UserAgentServiceConnector(', 'public GridNetworkUserAgent(')
ss = ''.join(
    extract(sim, s)
    for s in [
        'protected virtual string AgentPath()',
        'protected virtual void PackData(OSDMap args, GridRegion source, AgentCircuitData aCircuit, GridRegion destination, uint flags)',
        'public bool CreateAgent(GridRegion source, GridRegion destination, AgentCircuitData aCircuit, uint flags, EntityTransferContext ctx, out string reason)',
    ]
)
methods = w / 'GridMethods.cs'
methods.write_text(
    'using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public class GridNetworkSimulation{GridNetworkLogger m_log=new();'
    + ss
    + '}public class GridNetworkUserAgent:GridNetworkSimulation{GridNetworkLogger m_log=new();string m_ServerURL;GridRegion m_Gatekeeper;'
    + us
    + '}'
)
ws = ''.join(
    extract(web, s)
    for s in [
        'public static OSDMap PostToService(string url, OSDMap data, int timeout, bool rpc)',
        'public static OSDMap PostToServiceCompressed(string url, OSDMap data, int timeout)',
        'public static OSDMap ServiceOSDRequest(string url, OSDMap data, string method, int timeout, bool compressed, bool rpc, bool keepalive = false)',
        'private static OSDMap CanonicalizeResults(string response)',
        'private static OSDMap ErrorResponseMap(string msg)',
        'public static int EstimatedReceiveTimeout(HttpContentHeaders headers, bool keepalive)',
        'public static void LogOutgoingDetail(string type, int reqnum, string output)',
        'public static void LogOutgoingDetail(string context, string output)',
        'public static void LogResponseDetail(int reqnum, string input)',
    ]
)
binary = ''.join(
    extract('OpenSim/Framework/Util.cs', s)
    for s in [
        'public static void BinaryToASCII(char[] chars)',
        'public static string BinaryToASCII(string src)',
    ]
)
http_methods = w / 'HttpMethods.cs'
http_methods.write_text(
    'using Util=GridHttpUtil;using System;using System.IO;using System.IO.Compression;using System.Net.Http;using System.Net.Http.Headers;using System.Threading;using OpenMetaverse.StructuredData;static partial class WebUtil{'
    + ws
    + '}static partial class GridHttpUtil{'
    + binary
    + '}'
)
files = [
    'native-port/tests/user_agent_grid_http_reference.cs',
    'native-port/tests/osd_service_http_reference.cs',
    'native-port/tests/user_agent_region_reference_stubs.cs',
    'OpenSim/Services/Interfaces/IGridService.cs',
    'OpenSim/Framework/RegionFlags.cs',
    'OpenSim/Framework/AgentCircuitData.cs',
    'OpenSim/Framework/Culture.cs',
]
http_fixture = w / 'HttpFixture.cs'
http_fixture.write_text(
    (root / 'native-port/tests/osd_service_http_reference.cs')
    .read_text()
    .replace('static partial class Util', 'static partial class GridHttpUtil')
)
for f in [
    methods,
    http_methods,
    http_fixture,
    *[root / f for f in files if f != 'native-port/tests/osd_service_http_reference.cs'],
]:
    ET.SubElement(group, 'Compile', Include=str(f))
path = w / 'GridHttp.csproj'
project.write(path, encoding='utf-8', xml_declaration=True)
r = subprocess.run(
    [a.dotnet, 'build', str(path), '-c', 'Release', '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr

records = []
lock = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        selected = {
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
                'accept',
                'accept-encoding',
                'cookie',
                'authorization',
                'user-agent',
            ]
        }
        with lock:
            records.append(
                dict(method=self.command, target=self.path, headers=selected, body=body.hex())
            )
        mode = self.path.split('/')[1]
        compressed = self.headers.get('X-Content-Encoding') == 'gzip'
        status = 200
        reply = b'{"success":true,"reason":"created"}'
        if mode == 'denied':
            reply = b'{"success":false,"reason":"denied"}'
        elif mode == 'fallback' and compressed:
            status = 415
            reply = b'unsupported compression'
        elif mode == 'raw':
            reply = b'true'
        elif mode == 'malformed':
            reply = b'not json'
        elif mode == 'error':
            status = 500
            reply = b'ignored'
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(reply)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(reply)
        self.close_connection = True


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


server = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
rows = [
    f'{profile} http://127.0.0.1:{server.server_port}/{mode}/CASE/'
    for mode in ['success', 'denied', 'fallback', 'raw', 'malformed', 'error']
    for profile in range(72)
]
outputs = {}
captures = {}
try:
    for label, cmd in [('source', [a.dotnet, str(w / 'app/GridHttp.dll')]), ('native', [a.native])]:
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
        outputs[label] = [json.loads(s) for s in r.stdout.splitlines()]
        assert len(outputs[label]) == len(rows), (label, len(outputs[label]), len(rows))
        with lock:
            captures[label] = list(records)
finally:
    server.shutdown()
    server.server_close()
differences = [
    dict(index=i, input=row, source=s, native=n)
    for i, (row, s, n) in enumerate(zip(rows, outputs['source'], outputs['native']))
    if s != n
]
wire = []
for i in range(max(len(captures['source']), len(captures['native']))):
    s = captures['source'][i] if i < len(captures['source']) else None
    n = captures['native'][i] if i < len(captures['native']) else None
    if s != n:
        wire.append(dict(index=i, source=s, native=n))
report = dict(
    passed=not differences and not wire,
    cases=len(rows),
    sourceRequests=len(captures['source']),
    nativeRequests=len(captures['native']),
    differences=differences[:12],
    wireDifferenceCount=len(wire),
    wireDifferences=wire[:2],
    sourceHashes={
        f: hashlib.sha256((root / f).read_bytes()).hexdigest()
        for f in sorted(set([agent, sim, web, *files, *paths]))
    },
    scope=__doc__,
    applicationParityVerified=False,
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['passed'], f'{len(differences)} outcomes/{len(wire)} wire differences; see ' + str(
    w / 'results.json'
)
print(f'PASS: {len(rows)} composed grid login cases; {len(captures["native"])} exact requests')
