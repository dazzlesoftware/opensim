"""Original advanced notes/preferences and local/HG routing methods."""

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
    'native-port/tests/profile_requests_reference.cs',
    'OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs',
    'OpenSim/Framework/UserProfiles.cs',
    'OpenSim/Framework/GroupData.cs',
]


def extract(sig, path=None):
    s = (root / (path or sources[1])).read_bytes().decode('utf-8-sig')
    start = s.index(sig)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


sigs = [
    'public void NotesRequest(',
    'public void NotesUpdate(',
    'public void UserPreferencesRequest(',
    'public void UpdateUserPreferences(',
    'bool GetUserProfileServerURI(',
    'ScenePresence FindPresence(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using ScenePresence=Presence;partial class Original{'
    + ''.join(extract(sig) for sig in sigs)
    + '}namespace OpenSim.Framework{'
    + extract('public class GroupMembershipData', sources[3])
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
for f in [root / sources[0], root / sources[2], w / 'Extracted.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / (name + '.dll'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


uris = [
    None,
    '',
    ' ',
    '\t\r\n\u2000',
    'http://profile.test/',
    'http://other/a?x=é',
    'http://x/\x00tail',
]
args = [
    None,
    '',
    'bad',
    '00000000-0000-0000-0000-000000000003',
    '11111111-1111-0000-0000-000100bba000',
    '00000000-0000-0000-0000-000000000003 ',
    '00000000-0000-0000-0000-000000000003suffix',
]
texts = [None, '', 'notes 😀<&', 'a\x00b', 'a\ud800q', '\udc00']
flags = (
    [0]
    + [1 << i for i in range(24)]
    + [
        16 | 32,
        16 | 64,
        16 | 128,
        16 | 256,
        512 | 1024,
        512 | 1,
        2 | 4 | 8,
        16 | 32 | 256,
        16 | 64 | 524288,
        1 | 2048,
        16 | 1048576,
    ]
)
commands = [
    f'{op} {mode} {f} {enc(uri)} {enc(arg)} {enc(text)}'
    for op in range(6)
    for mode in range(11)
    for f in flags
    for uri, arg, text in zip(uris, args, texts + [None])
]
commands += [
    f'{op} {mode} {f} {enc(uri)} {enc(arg)} {enc(text)}'
    for op in [0, 1]
    for mode in [0, 1, 2, 3, 4, 5, 6, 8, 9, 10]
    for f in [0, 16, 16 | 32, 512, 512 | 1024, 1048576]
    for uri in [None, 'http://profile/']
    for arg in args
    for text in texts
]
outputs = {}
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    with (w / (name + '.txt')).open('w', encoding='utf-8') as out:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            stdout=out,
            stderr=subprocess.PIPE,
            text=True,
            timeout=180,
        )
    assert r.returncode == 0, r.stderr
    outputs[name] = (w / (name + '.txt')).read_text().splitlines()
    assert len(outputs[name]) == len(commands), (name, len(outputs[name]), len(commands))
diffs = [
    dict(command=c, source=s, native=n)
    for c, s, n in zip(commands, outputs['reference'], outputs['native'])
    if s != n
]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differenceCount=len(diffs),
    differences=diffs[:12],
    applicationParityVerified=False,
    scope='Complete unchanged NotesRequest/NotesUpdate/UserPreferencesRequest/UpdateUserPreferences/GetUserProfileServerURI/FindPresence with original DTOs and actual SDK UUID. Controlled scene/presence/user-management/grid/RPC/client/log boundaries; exact local/HG owner routing, special NPC/system-agent guards, repeated AgentId evaluation, null/whitespace URI, mutable RPC object/ref outcome and errors/reply order. Native default context binds real JsonRpcRequestManager; separate live production HTTP/SQLite controller composition is also verified, while full scene/runtime/advanced module/macOS remain pending.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' request differences; see results.json'
print('PASS:', len(commands), 'original profile request scenarios')
