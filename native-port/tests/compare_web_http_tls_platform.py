import argparse, json, subprocess, xml.etree.ElementTree as ET, hashlib, sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--mode', choices=['hello', 'certificate'], required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
source = (root / 'OpenSim/Framework/WebUtil.cs').read_bytes().decode('utf-8-sig')


def extract(signature):
    at = source.index(signature)
    start = source.index('{', at)
    end = start + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


methods = ''.join(
    extract(sig)
    for sig in [
        'public static void SetupHTTPClients(',
        'public static HttpClient GetNewGlobalHttpClient(',
        'public static HttpClient GetGlobalNoRedirHttpClient(',
    ]
)
(w / 'Extracted.cs').write_text(
    'using System;using System.Net;using System.Net.Http;using System.Net.Security;using System.Security.Authentication;using System.Security.Cryptography.X509Certificates;static partial class WebUtil{'
    + methods
    + '}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
    NoWarn='SYSLIB0039',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [root / 'native-port/tests/web_http_network_reference.cs', w / 'Extracted.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
reference = [a.dotnet, str(w / 'app/Reference.dll')]


def encode(s):
    return '-' if s is None else s.encode('utf-16-be', errors='surrogatepass').hex() or '_'


names = [
    None,
    '',
    'alt.fixture.test',
    'ALT.FIXTURE.TEST',
    'alt.fixture.test.',
    'alt.fixture.test..',
    ' alt.fixture.test ',
    'alt.fixture.test:443',
    'alt.fixture.test:8443',
    '127.0.0.1',
    '127.1',
    '0x7f.1',
    '0177.0.0.1',
    '127.00.0.1',
    '2130706433',
    '127.0.0.1.',
    '[::1]',
    '::1',
    '[::1]:443',
    '[fe80::1%123]',
    '[fe80::1%123]:443',
    'b\u00fccher.fixture.test',
    'xn--bcher-kva.fixture.test',
    '\u212a.fixture.test',
    '\u017f.fixture.test',
    'a_b.fixture.test',
    'bad/name',
    'bad,name',
    'a#b',
    'a?b',
    'a\nb',
    'a\r\nb',
    'a\0b',
    '\ud800',
    'a' * 63 + '.test',
    'a' * 64 + '.test',
    '.'.join(['a' * 50] * 5),
]
rows = []
labels = []
if a.mode == 'hello':
    rows = [encode(s) for s in names]
    labels = [repr(s) for s in names]
else:
    r = subprocess.run(
        reference + ['--make-name-certs', str(w / 'certificates')], capture_output=True, text=True
    )
    assert r.returncode == 0, r.stdout + r.stderr
    identities = ['ip', 'dns', 'cn']
    if sys.platform.startswith('linux'):
        identities += [
            'cn-noncanonical',
            'cn-ip-dns',
            'cn-ip-other-ip',
            'cn-ip-multiple',
            'partial-wild',
            'cn-multi-dns',
        ]
    for identity in identities:
        der = (w / 'certificates' / f'{identity}.der').read_bytes().hex()
        targets = [s for s in names if s is not None] + [
            'wild.fixture.test',
            'a.wild.fixture.test',
            'a.b.wild.fixture.test',
        ]
        if sys.platform.startswith('linux'):
            targets += [
                'abc.fixture.test',
                'first.fixture.test',
                'second.fixture.test',
                '.fixture.test',
                'a..fixture.test',
                '-a.fixture.test',
                'a-.fixture.test',
            ]
        for name in targets:
            rows.append(der + ' ' + encode(name))
            labels.append(identity + ':' + repr(name))
payload = '\n'.join(rows) + '\n'
outputs = {}
mode = '--hello-probe' if a.mode == 'hello' else '--certificate-probe'
for label, command in [('source', reference), ('native', [a.native])]:
    r = subprocess.run(command + [mode], input=payload, capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, (label, r.stdout[-1000:], r.stderr)
    outputs[label] = r.stdout.splitlines()
    assert len(outputs[label]) == len(rows), (label, len(outputs[label]), len(rows))


def hello(value):
    data = bytes.fromhex(value[2:]) if value[2:] != '-' else b''
    handshake = b''
    offset = 0
    while offset + 5 <= len(data):
        size = int.from_bytes(data[offset + 3 : offset + 5], 'big')
        record = data[offset + 5 : offset + 5 + size]
        if data[offset] == 22:
            handshake += record
        offset += 5 + size
    if not handshake or handshake[0] != 1:
        return dict(clientHello=False, sni=None)
    pos = 4 + 2 + 32
    pos += 1 + handshake[pos]
    pos += 2 + int.from_bytes(handshake[pos : pos + 2], 'big')
    pos += 1 + handshake[pos]
    names = []
    if pos + 2 <= len(handshake):
        stop = pos + 2 + int.from_bytes(handshake[pos : pos + 2], 'big')
        pos += 2
        while pos + 4 <= stop:
            kind = int.from_bytes(handshake[pos : pos + 2], 'big')
            size = int.from_bytes(handshake[pos + 2 : pos + 4], 'big')
            extension = handshake[pos + 4 : pos + 4 + size]
            pos += 4 + size
            if kind == 0:
                index = 2
                while index + 3 <= len(extension):
                    name_type = extension[index]
                    name_size = int.from_bytes(extension[index + 1 : index + 3], 'big')
                    index += 3
                    if name_type == 0:
                        names.append(extension[index : index + name_size].hex())
                    index += name_size
    return dict(clientHello=True, sni=names)


observations = []
differences = []
for i, (s, n) in enumerate(zip(outputs['source'], outputs['native'])):
    left, right = (hello(s), hello(n)) if a.mode == 'hello' else (s, n)
    row = dict(case=i, label=labels[i], source=left, native=right)
    if a.mode == 'hello':
        row.update(sourceToken=s[2:], nativeToken=n[2:])
    observations.append(row)
    if left != right:
        differences.append(row)
report = dict(
    passed=not differences,
    cases=len(rows),
    mode=a.mode,
    differences=differences,
    observations=observations,
    inputSHA256=hashlib.sha256(payload.encode()).hexdigest(),
    applicationParityVerified=False,
    scope='Source original Setup/Get factory with an in-memory ConnectCallback capture stream versus native production target-name selector and OpenSSL memory BIO: actual ClientHello SNI bytes, no network or peer certificate. Certificate mode compares the actual runtime Windows chain/name policy with native read-only Crypt32 using temporary self-signed DERs; compares name acceptance only, not trust/revocation. No certificate-store or security-setting changes.',
)
if a.mode == 'certificate' and sys.platform.startswith('linux'):
    report['scope'] = (
        'Actual pinned Unix CertificateValidation.BuildChainAndVerifyProperties versus native openssl.c-compatible certificate names. Temporary self-signed DERs isolate name acceptance: IP SAN and all-CN fallback, noncanonical/scoped/bracketed IP text, SAN precedence, multiple CNs, NO_PARTIAL_WILDCARDS and strict secondary default IDN conversion. This does not prove native chain trust, revocation or complete TLS. No certificate-store changes.'
    )
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)} offline {a.mode} policy cases')
