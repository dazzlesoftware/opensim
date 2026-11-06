"""Unchanged full WebUtil.ServiceOSDRequest with real SDK JSON/gzip and explicit HTTP/reader/timer seams."""

from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET, itertools, hashlib

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
source = 'OpenSim/Framework/WebUtil.cs'
s = (root / source).read_bytes().decode('utf-8-sig')


def extract(signature):
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


method = extract(
    'public static OSDMap ServiceOSDRequest(string url, OSDMap data, string method, int timeout, bool compressed, bool rpc, bool keepalive = false)'
)
canonical = extract('private static OSDMap CanonicalizeResults(string response)').replace(
    ' CanonicalizeResults(', ' OriginalCanonicalizeResults(', 1
)
methods = (
    method
    + canonical
    + extract('private static OSDMap ErrorResponseMap(string msg)')
    + extract(
        'public static int EstimatedReceiveTimeout(HttpContentHeaders headers, bool keepalive)'
    )
)
(w / 'Extracted.cs').write_text(
    'using System;using System.IO;using System.IO.Compression;using System.Net.Http;using System.Net.Http.Headers;using OpenMetaverse.StructuredData;using HttpClient=Client;using HttpRequestMessage=Request;using HttpResponseMessage=Response;using Stream=FakeStream;using StreamReader=Reader;using CancellationTokenSource=Timer;using OSDParser=Serializer;static partial class WebUtil{'
    + methods
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
items = ET.SubElement(project, 'ItemGroup')
for f in [root / 'native-port/tests/osd_service_requester_reference.cs', w / 'Extracted.cs']:
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
rows = []


def add(level=0, num=0, ticks=0, flags=0, mode=0, length='-', fail='none', code='-'):
    rows.append(' '.join(map(str, [level, num, ticks, flags, mode, length, fail, code])))


for level, flags, mode, ticks, length in itertools.product(
    [0, 3, 4, 5],
    range(16),
    [0, 1, 2, 4, 8, 256, 512, 768, 1024, 1280, 1536],
    [0, 3001],
    ['-', 1024, 2199023255552],
):
    add(level=level, flags=flags, mode=mode, ticks=ticks, length=length)
for length in [
    0,
    1,
    1023,
    1024,
    56320000,
    56321024,
    2147483647 * 1024,
    2147483648 * 1024,
    4294962296 * 1024,
    4294962295 * 1024,
    9223372036854775807,
]:
    add(level=5, length=length)
for num in [-2147483648, -1, 2147483647]:
    add(level=5, num=num)
for level, flags, mode, ticks, length in itertools.product(
    [0, 5], [0, 1, 2, 8, 14], [0, 1, 2, 4, 8], [0, 3001], ['-', 1024]
):
    for fail in [
        'number-get',
        'number-set',
        'level',
        'tick',
        'factory',
        'request',
        'serialize-text',
        'serialize-bytes',
        'detail-out',
        'utf8',
        'send',
        'ensure',
        'stream',
        'reader',
        'length',
        'timer',
        'register',
        'read',
        'timer-close',
        'reader-close',
        'stream-close',
        'canonical',
        'detail-response',
        'request-close',
        'response-close',
        'client-close',
        'elapsed',
        'info',
        'debug',
        'debug-final',
        'debug-catch',
        'debug-failed',
    ]:
        for http in [0, 16]:
            add(
                level=level,
                flags=flags | http,
                mode=mode,
                ticks=ticks,
                length=length,
                fail=fail,
                code=503 if http else '-',
            )
for fail in [
    'read+timer-close',
    'read+reader-close',
    'ensure+request-close',
    'read+response-close',
    'read+client-close',
    'stream-close+timer-close',
]:
    for level, mode in itertools.product([0, 5], [0, 2]):
        add(level=level, mode=mode, fail=fail)
source_cmd = [a.dotnet, str(w / 'app/Reference.dll')]


def run(cmd):
    r = subprocess.run(
        cmd, input='\n'.join(rows) + '\n', capture_output=True, text=True, encoding='utf-8'
    )
    assert r.returncode == 0, r.stdout + r.stderr
    out = r.stdout.splitlines()
    assert len(out) == len(rows), (len(out), len(rows))
    return out


expected = run(source_cmd)
actual = run([a.native])
for i, (e, n) in enumerate(zip(expected, actual)):
    if e != n:
        (w / 'failure.json').write_text(
            json.dumps(dict(index=i, input=rows[i], expected=e, actual=n), indent=2)
        )
        raise AssertionError(f'mismatch {i}: {rows[i]}\nexpected {e[:4000]}\nactual {n[:4000]}')
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=True,
            cases=len(rows),
            scope='Entire unchanged ServiceOSDRequest and source canonical/error/adaptive-timeout helpers; real SDK JSON/gzip and BCL request/header construction, controlled client/reader/cancellation/logger/counter/clock/codec failure seams. Exact typed result JSON, serialized request bytes/headers, scope disposal and exception/log ordering; no real HTTP/stream-reader/global pooling policy claim',
            sourceSHA256=hashlib.sha256((root / source).read_bytes()).hexdigest(),
        ),
        indent=2,
    )
)
print(f'ServiceOSDRequest {len(rows)} source scenarios passed')
