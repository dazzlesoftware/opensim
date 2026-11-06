import argparse, base64, hashlib, json, os, re, stat, subprocess, uuid, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(exist_ok=True, parents=True)


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, **kwargs)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for key, value in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
    EnableDefaultCompileItems='false',
).items():
    ET.SubElement(props, key).text = value
ET.SubElement(
    ET.SubElement(project, 'ItemGroup'),
    'Compile',
    Include=str(
        Path(__file__).with_name('config_http_linux_certificate_persistence_reference.cs').resolve()
    ),
)
ET.ElementTree(project).write(w / 'Reference.csproj')
run([a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'])
reference = [a.dotnet, str(w / 'app/Reference.dll')]
run([*reference, '--prepare', str(w)])
der = (w / 'a.der').read_bytes()
thumb = hashlib.sha1(der).hexdigest().upper()
base = thumb + '.pfx'
fixtures = {
    name: (w / name).read_bytes()
    for name in [
        'a.der',
        'b.der',
        'c.der',
        'a.pfx',
        'b.pfx',
        'c.pfx',
        'keyed.pfx',
        'ac.pfx',
        'ca.pfx',
        'ac-keyed.pfx',
        'ca-keyed.pfx',
        'passworded.pfx',
    ]
}
fixtures['junk'] = b'not a certificate'
fixtures['empty'] = b''
fixtures['a.pem'] = (
    b'-----BEGIN CERTIFICATE-----\n' + base64.encodebytes(der) + b'-----END CERTIFICATE-----\n'
)
(w / 'aux.pem').write_text(
    run(
        [
            'openssl',
            'x509',
            '-inform',
            'DER',
            '-in',
            str(w / 'a.der'),
            '-trustout',
            '-addtrust',
            'clientAuth',
            '-addreject',
            'serverAuth',
            '-setalias',
            'persisted auxiliary metadata',
        ]
    )
)
known = {value: hashlib.sha256(value).hexdigest() for value in fixtures.values()}


def tlv(data):
    tag = data[0]
    n = data[1]
    offset = 2
    if n & 128:
        count = n & 127
        n = int.from_bytes(data[2 : 2 + count], 'big')
        offset += count
    assert offset + n <= len(data)
    return tag, data[offset : offset + n], data[offset + n :]


def encode(tag, data):
    n = len(data)
    size = n.to_bytes(max(1, (n.bit_length() + 7) // 8), 'big')
    return bytes([tag]) + (bytes([n]) if n < 128 else bytes([128 | len(size)]) + size) + data


def children(data):
    values = []
    while data:
        tag, value, data = tlv(data)
        values.append((tag, value))
    return values


def pack(values):
    return b''.join(encode(tag, value) for tag, value in values)


def pfx_shape(data):
    tag, body, tail = tlv(data)
    assert tag == 48 and not tail
    outer = children(body)
    content = children(outer[1][1])
    explicit = children(content[1][1])
    # explicit[0] is the authSafe OCTET STRING; parse its encoded SEQUENCE.
    auth = children(tlv(explicit[0][1])[1])
    assert len(auth) == 1
    info = children(auth[0][1])
    encrypted = children(children(info[1][1])[0][1])
    eci = children(encrypted[1][1])
    algorithm = children(eci[1][1])
    parameters = children(algorithm[1][1])
    assert parameters[0][0] == 4 and len(parameters[0][1]) == 16
    assert parameters[1] == (2, b'\x07\xd0')
    parameters[0] = (4, bytes(16))
    algorithm[1] = (48, pack(parameters))
    eci[1] = (48, pack(algorithm))
    assert eci[2][0] == 128
    eci[2] = (128, bytes(len(eci[2][1])))
    encrypted[1] = (48, pack(eci))
    info[1] = (160, encode(48, pack(encrypted)))
    auth[0] = (48, pack(info))
    explicit[0] = (4, encode(48, pack(auth)))
    content[1] = (160, pack(explicit))
    outer[1] = (48, pack(content))
    mac = children(outer[2][1])
    digest = children(mac[0][1])
    digest[1] = (4, bytes(len(digest[1][1])))
    mac[0] = (48, pack(digest))
    assert mac[1][0] == 4 and len(mac[1][1]) == 20
    mac[1] = (4, bytes(20))
    assert mac[2] == (2, b'\x07\xd0')
    outer[2] = (48, pack(mac))
    return encode(48, pack(outer)).hex()


def snapshot(root):
    output = []

    def visit(path, relative):
        info = path.lstat()
        item = dict(path=relative, mode=stat.S_IMODE(info.st_mode))
        if path.is_symlink():
            item.update(kind='link', target=os.readlink(path))
        elif path.is_dir():
            item['kind'] = 'directory'
        else:
            item['kind'] = 'file'
            try:
                data = path.read_bytes()
            except PermissionError:
                # Both child processes have exited. Inspect this owned fixture without
                # losing the original permission observation, then restore its mode.
                path.chmod(stat.S_IMODE(info.st_mode) | 0o400)
                try:
                    data = path.read_bytes()
                finally:
                    path.chmod(stat.S_IMODE(info.st_mode))
            if data in known:
                item['fixtureSHA256'] = known[data]
            else:
                result = subprocess.run(
                    ['openssl', 'pkcs12', '-in', str(path), '-passin', 'pass:', '-nokeys', '-info'],
                    capture_output=True,
                    text=True,
                )
                assert result.returncode == 0, result.stderr
                certs = re.findall(
                    r'-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----',
                    result.stdout,
                    re.S,
                )
                assert len(certs) == 1 and base64.b64decode(certs[0]) in [
                    fixtures[name] for name in ['a.der', 'b.der', 'c.der']
                ]
                assert 'No Attributes' in result.stdout
                item.update(
                    pfxShape=pfx_shape(data),
                    certificateSHA256=hashlib.sha256(base64.b64decode(certs[0])).hexdigest(),
                    cryptoInfo=result.stderr.strip().splitlines(),
                )
        output.append(item)
        if item['kind'] == 'directory':
            path.chmod(stat.S_IMODE(info.st_mode) | 0o700)
            try:
                for child in sorted(path.iterdir()):
                    visit(child, child.name if relative == '.' else relative + '/' + child.name)
            finally:
                path.chmod(stat.S_IMODE(info.st_mode))

    if root.exists() or root.is_symlink():
        visit(root, '.')
    return output


cases = [
    dict(label='new'),
    dict(label='repeat', certs=['a.der', 'a.der']),
    dict(label='aux-public', certs=['aux.pem']),
    dict(label='native-error', error=20),
    dict(label='empty-downloads', certs=[]),
    dict(label='multiple', certs=['a.der', 'c.der']),
]
for mode in [0o700, 0o755, 0o777, 0o750, 0o600, 0o500, 0o300]:
    cases.append(dict(label='directory-' + oct(mode), mode=mode))
for mask in [0o077, 0o200, 0o400, 0o600]:
    cases.append(dict(label='umask-' + oct(mask), mode=0o755, mask=mask))
for label, files in [
    ('candidate-junk', [(base, 'junk')]),
    ('candidate-der', [(base, 'a.der')]),
    ('candidate-pem', [(base, 'a.pem')]),
    ('candidate-pfx', [(base, 'a.pfx')]),
    ('candidate-keyed', [(base, 'keyed.pfx')]),
    ('candidate-equal-issuer-serial', [(base, 'b.der')]),
    ('candidate-other-serial', [(base, 'c.pfx')]),
    ('uppercase-suffix', [(thumb + '.PFX', 'a.pfx')]),
    ('candidate-suffix', [(thumb + '.arbitrary.pfx', 'junk')]),
    ('slot-one', [(base, 'junk'), (thumb + '.1.pfx', 'junk')]),
    (
        'slots-exhausted',
        [
            (thumb + '.arbitrary.pfx', 'junk'),
            *[(thumb + '.' + str(i) + '.pfx', 'junk') for i in range(1, 10)],
        ],
    ),
]:
    cases.append(dict(label=label, files=files))
cases.extend(
    [
        dict(label='base-directory', directory=base),
        dict(
            label='slot-directory',
            files=[(thumb + '.arbitrary.pfx', 'junk')],
            directory=thumb + '.1.pfx',
        ),
        dict(label='broken-base-link', link=(base, 'missing.pfx')),
        dict(label='candidate-link', files=[('target.pfx', 'a.pfx')], link=(base, 'target.pfx')),
        dict(label='store-is-file', block='ca'),
        dict(label='root-is-file', block='root'),
    ]
)
cases.append(
    dict(
        label='failed-first-continues',
        certs=['a.der', 'c.der'],
        files=[
            (thumb + '.arbitrary.pfx', 'junk'),
            *[(thumb + '.' + str(i) + '.pfx', 'junk') for i in range(1, 10)],
        ],
    )
)
cases.extend(
    [
        dict(label='collection-ac', files=[(base, 'ac.pfx')]),
        dict(label='collection-ca', files=[(base, 'ca.pfx')]),
        dict(label='suffix-trailing-dot', files=[(thumb + '.pfx.', 'a.pfx')]),
        dict(label='suffix-trailing-space', files=[(thumb + '.pfx ', 'a.pfx')]),
    ]
)
cases.extend(
    dict(label='candidate-' + name, files=[(base, name)])
    for name in ['ac-keyed.pfx', 'ca-keyed.pfx', 'passworded.pfx']
)
observations = []
differences = []
scenario_root = w / 'scenarios' / uuid.uuid4().hex
for case in cases:
    values = {}
    for kind, command in [('source', reference), ('native', [a.native])]:
        home = scenario_root / case['label'] / kind
        root = home / '.dotnet/corefx/cryptography/x509stores'
        root.parent.mkdir(parents=True, exist_ok=True)
        assert not root.exists(), f'Use a fresh work directory: {root}'
        if case.get('block') == 'root':
            root.write_bytes(fixtures['junk'])
        else:
            root.mkdir()
            ca = root / 'ca'
            if case.get('block') == 'ca':
                ca.write_bytes(fixtures['junk'])
            elif 'mode' in case or 'files' in case or 'directory' in case or 'link' in case:
                ca.mkdir()
                for name, fixture in case.get('files', []):
                    (ca / name).write_bytes(fixtures[fixture])
                if 'directory' in case:
                    (ca / case['directory']).mkdir()
                if 'link' in case:
                    (ca / case['link'][0]).symlink_to(case['link'][1])
                if 'mode' in case:
                    ca.chmod(case['mode'])
        env = os.environ.copy()
        env.update(HOME=str(home), SSL_CERT_FILE='', SSL_CERT_DIR='')
        mask = case.get('mask', 0o022)
        output = run(
            [
                *command,
                str(case.get('error', 0)),
                *[str(w / name) for name in case.get('certs', ['a.der'])],
            ],
            env=env,
            preexec_fn=lambda: os.umask(mask),
        ).strip()
        values[kind] = dict(cache=output, files=snapshot(root))
    observations.append(dict(label=case['label'], **values))
    if values['source'] != values['native']:
        differences.append(observations[-1])
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=not differences,
            cases=len(cases),
            differences=differences,
            observations=observations,
            scope='Actual pinned ChainPal.SaveIntermediateCertificates with isolated HOME versus native selected-issuer persistence. Exact cache snapshot replacement/counts, filenames, directory/file modes, failure side effects, duplicate Equals issuer/serial rules, PEM/DER/PFX candidates, suffix matching, symlinks, slots and umask. New PFX outputs verified/decrypted with OpenSSL and entire DER compared after replacing only independent random salts/ciphertext/MAC bytes by zero; all algorithm/iteration/length/structural fields retained.',
        ),
        indent=2,
    )
    + '\n'
)
assert not differences, f'{len(differences)} differences: {w}/results.json'
print(f'PASS: {len(cases)} selected issuer persistence scenarios')
