"""Full original profile handlers with original interface, DTOs and SDK."""

from pathlib import Path
import argparse, subprocess, json, hashlib, xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
sources = [
    'native-port/tests/profile_handlers_reference.cs',
    'OpenSim/Server/Handlers/Profiles/UserProfilesHandlers.cs',
    'OpenSim/Services/Interfaces/IUserProfilesService.cs',
    'OpenSim/Framework/UserProfiles.cs',
    'OpenSim/Framework/Servers/HttpServer/JsonRpcResponse.cs',
]
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
for f in sources:
    ET.SubElement(items, 'Compile', Include=str(root / f))
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / str(name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
ident = '01234567-89ab-cdef-0123-456789abcdef'
params = [
    {},
    {'__null': True},
    *(
        {'params': v}
        for v in [
            None,
            False,
            1,
            'bad',
            [],
            {},
            {
                'creatorId': ident,
                'classifiedId': ident,
                'avatarId': ident,
                'PickId': ident,
                'CreatorId': ident,
                'pickId': ident,
                'Flags': 0,
                'Name': 'incoming',
            },
            {
                'creatorId': 'invalid',
                'classifiedId': 'invalid',
                'avatarId': 'invalid',
                'PickId': 'invalid',
                'CreatorId': 'invalid',
                'pickId': 'invalid',
            },
            {
                'creatorId': ident + 'suffix',
                'classifiedId': ' ' + ident + 'tail',
                'avatarId': ident.replace('-', '') + 'tail',
            },
            {'PickId': ident},
            {'PickId': ident, 'CreatorId': ident, 'Name': 'ignored', 'Enabled': True},
            {'Flags': 1},
            {'Flags': 2},
            {'Flags': 64},
            {'Flags': 255},
            {'Flags': -1},
            {'UserId': ident, 'Notes': None, 'EMail': 'incoming', 'DataVal': 'incoming'},
        ]
    ),
]
# Constructor-specific UTF-16 low-byte SIMD behavior, scalar compact parsing and ASCII prefixes.
for value in [
    ' ' + ident,
    '\t' + ident,
    ident.replace('-', ''),
    ident.upper(),
    ident[:-1],
    ident + '\x00',
    ''.join(chr(ord(c) + 256) if c != '-' else c for c in ident),
]:
    params.append({'params': dict(creatorId=value, classifiedId=value, avatarId=value)})
commands = [
    f'{method} {mode} {seed} '
    + json.dumps(param, ensure_ascii=True, separators=(',', ':')).encode().hex()
    for method in range(18)
    for mode in range(8)
    for seed in range(5)
    for param in params
]
# Isolate constructor/TryParse differences, including UTF-16 low bytes and signed SIMD byte arithmetic.
for compact in [False, True]:
    base = ident.replace('-', '') if compact else ident
    for position in [0, 1, 16, 17, 24, 25]:
        for high in [0, 256, 65280]:
            for low in range(256):
                text = base[:position] + chr(high + low) + base[position + 1 :]
                raw = json.dumps(dict(text=text), ensure_ascii=True).encode().hex()
                for method in [18, 19]:
                    commands.append(f'{method} 0 0 ' + raw)
(w / 'commands.txt').write_text('\n'.join(commands) + '\n', encoding='utf-8')
rows = []
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    r = subprocess.run(
        cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=180
    )
    assert r.returncode == 0, r.stderr
    rows.append(r.stdout.splitlines())
    (w / (name + '.txt')).write_text(r.stdout, encoding='utf-8')
    assert len(rows[-1]) == len(commands), (name, len(rows[-1]), len(commands), r.stdout[-500:])
diffs = [dict(command=c, source=x, native=y) for c, x, y in zip(commands, *rows) if x != y]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differenceCount=len(diffs),
    differences=diffs[:12],
    applicationParityVerified=False,
    scope='Unchanged full JsonRpcProfileHandlers and IUserProfilesService with real SDK and original DTOs. Eighteen registered native delegates retain service lifetime. Controlled provider success/failure/throw/ref replacement/null mutation and typed/null/wrong parameter inputs, prior/null response/error state, exact provider input, error, result, exception and diagnostic text. Production provider, configuration, viewer integration, full simulator and macOS execution incomplete.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert not diffs, str(len(diffs)) + ' profile handler differences; see results.json'
print('PASS:', len(commands), 'original profile handler scenarios')
