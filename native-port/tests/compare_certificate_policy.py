import argparse, itertools, random, json, subprocess, hashlib, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
p.add_argument('--runtime', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parent
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
    Nullable='disable',
    NoWarn='CS0436;CS8632',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in sorted((root / 'certificate_policy_reference').glob('*.cs')):
    ET.SubElement(items, 'Compile', Include=str(f))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr


def der(tag, data):
    n = len(data)
    size = (
        bytes([n])
        if n < 128
        else (lambda b: bytes([128 + len(b)]) + b)(n.to_bytes((n.bit_length() + 7) // 8, 'big'))
    )
    return bytes([tag]) + size + data


def seq(*values):
    return der(48, b''.join(values))


def base128(n):
    data = [n & 127]
    n >>= 7
    while n:
        data.insert(0, (n & 127) | 128)
        n >>= 7
    return bytes(data)


def oid(value):
    arcs = [int(s) for s in value.split('.')]
    return der(6, base128(arcs[0] * 40 + arcs[1]) + b''.join(base128(n) for n in arcs[2:]))


def integer(value, tag=2):
    n = 1
    while not -(1 << (n * 8 - 1)) <= value < (1 << (n * 8 - 1)):
        n += 1
    return der(tag, value.to_bytes(n, 'big', signed=True))


def eku(*values):
    return seq(*(oid(v) for v in values))


def policy(*values, qualifier=None):
    return seq(*(seq(oid(v), qualifier or b'') for v in values))


def mapping(*pairs):
    return seq(*(seq(oid(k), oid(v)) for k, v in pairs))


def constraints(explicit=None, mapped=None):
    return seq(
        integer(explicit, 128) if explicit is not None else b'',
        integer(mapped, 129) if mapped is not None else b'',
    )


empty = (None,) * 6
server = '1.3.6.1.5.5.7.3.1'
client = '1.3.6.1.5.5.7.3.2'
cert = '1.2.3'
other = '1.2.4'
any_cert = '2.5.29.32.0'
any_eku = '2.5.29.37.0'
large = '2.' + str(2**256 + 12345) + '.' + str(2**128)
atoms = [empty]


def atom(field, data):
    value = list(empty)
    value[field] = data
    return tuple(value)


for field, encode in [(0, policy), (1, policy), (4, eku)]:
    for values in [
        [],
        [server],
        [client],
        [any_cert],
        [any_eku],
        [cert],
        [large],
        [server, client],
        [cert, other],
        [any_cert, cert],
        [cert, cert],
    ]:
        atoms.append(atom(field, encode(*values)))
for value in [-2147483648, -2, -1, 0, 1, 2, 3, 8, 2147483647]:
    atoms.extend(
        [
            atom(3, constraints(value, None)),
            atom(3, constraints(None, value)),
            atom(3, constraints(value, value)),
            atom(5, integer(value)),
        ]
    )
for pairs in [
    [],
    [(cert, other)],
    [(other, cert)],
    [(cert, other), (cert, server)],
    [(server, cert), (cert, other)],
    [(any_cert, other)],
]:
    atoms.append(atom(2, mapping(*pairs)))
atoms.extend(
    [
        (policy(server), None, None, None, b'\xff', None),
        (policy(), None, None, None, b'\xff', None),
        (policy(any_cert), None, None, None, b'\xff', None),
    ]
)
rows = []
categories = {}


def token(data):
    return '-' if data is None else data.hex() or '_'


def add(chain, mode, queries, category):
    rows.append(
        ' '.join(
            [
                str(len(chain)),
                *(token(v) for c in chain for v in c),
                mode,
                str(len(queries)),
                *(q if q is not None else '-' for q in queries),
            ]
        )
    )
    categories[category] = categories.get(category, 0) + 1


queries = [server, client, cert, other, any_cert, any_eku, large, None]
for chain in [[], *[[v] for v in atoms]]:
    for mode in ['A', 'C']:
        for q in queries:
            add(chain, mode, [q], 'single')
    for mode in ['a', 'c']:
        for qs in [[], [server, client], [cert, other], [any_cert, None]]:
            add(chain, mode, qs, 'collections')
for left, right in itertools.product(atoms, repeat=2):
    for mode, q in [('A', server), ('A', any_cert), ('C', cert), ('C', other)]:
        add([left, right], mode, [q], 'pairs')
rng = random.Random(831)
for _ in range(2200):
    chain = [rng.choice(atoms) for _ in range(rng.randrange(3, 8))]
    for mode in ['A', 'C', 'a', 'c']:
        add(
            chain,
            mode,
            [rng.choice(queries)] if mode.isupper() else rng.sample(queries, rng.randrange(4)),
            'long-chains',
        )
valid = [
    policy(cert, qualifier=seq()),
    policy(server, large),
    mapping((cert, other)),
    constraints(0, 2),
    eku(server, client),
    integer(2),
]
for field, value in enumerate(valid):
    malformed = {b'', b'\x00\x00', b'\x30\x80\x00\x00', value + b'\x00', b'\xff' * 5}
    for at in range(len(value)):
        malformed.add(value[:at])
        for byte in range(256):
            malformed.add(value[:at] + bytes([byte]) + value[at + 1 :])
    for data in sorted(malformed):
        add([atom(field, data)], 'A', [server], 'DER-mutations')
for qualifier in [
    b'\x00\x00',
    b'\x05\x00',
    b'\x05\x01\x00',
    b'\x30\x02\xff\xff',
    b'\x1f\x1f\x00',
    b'\x1f\x80\x1f\x00',
    b'\x1f' + base128(2**31 - 1) + b'\x00',
    b'\x1f' + base128(2**31) + b'\x00',
]:
    add([atom(0, policy(server, qualifier=qualifier))], 'A', [server], 'opaque-qualifiers')
for value in [-(2**63), 2**31, 2**64, 2**512]:
    for field, data in [(3, seq(integer(value, 128))), (5, integer(value))]:
        add([atom(field, data)], 'C', [cert], 'integer-overflow')
for mode in ['C', 'c']:
    add([empty, atom(1, policy(cert)), atom(5, integer(-(2**31)))], mode, [cert], 'counter-wrap')
payload = '\n'.join(rows) + '\n'
(w / 'inputs.txt').write_text(payload)
outputs = {}
for label, command in [
    ('source', [a.dotnet, str(w / 'app/Reference.dll')] + (['--runtime'] if a.runtime else [])),
    ('native', [a.native]),
]:
    r = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=180)
    assert r.returncode == 0, (label, r.stdout[-1000:], r.stderr)
    outputs[label] = r.stdout.splitlines()
    assert len(outputs[label]) == len(rows), (label, len(outputs[label]), len(rows))
differences = [
    dict(case=i, input=rows[i], source=s, native=n)
    for i, (s, n) in enumerate(zip(outputs['source'], outputs['native']))
    if s != n
]
report = dict(
    passed=not differences,
    cases=len(rows),
    categories=categories,
    differences=differences,
    inputSHA256=hashlib.sha256(payload.encode()).hexdigest(),
    outputSHA256={
        label: hashlib.sha256(('\n'.join(lines) + '\n').encode()).hexdigest()
        for label, lines in outputs.items()
    },
    applicationParityVerified=False,
    scope='Complete pinned CertificatePolicy.cs rules and runtime-generated ASN decoder bodies execute with scripted certificate PolicyData and a public AsnReader bridge. Native DER/policy outputs and outer exception type/HResult/message are compared. This is not certificate extraction, trust/path building, revocation, inner exception or simulator parity; actual Linux runtime adapter validation is separate.',
)
if a.runtime:
    report['scope'] = (
        'Actual pinned Linux runtime CertificatePolicyChain constructor/match methods, real certificate DER extension payloads and actual PAL extraction/ASN.1 decoders versus native policy/DER processor. No trust/path building, revocation, inner-exception or simulator parity claim.'
    )
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(rows)} certificate policy/DER scenarios')
