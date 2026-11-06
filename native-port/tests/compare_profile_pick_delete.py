"""Original picks workflow composed with actual SDK, profile/group models and cache."""

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
    'native-port/tests/profile_picks_reference.cs',
    'OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs',
    'OpenSim/Framework/UserProfiles.cs',
    'OpenSim/Framework/GroupData.cs',
    'OpenSim/Framework/ExpiringCacheOS.cs',
    'native-port/tests/profile_pick_delete_reference.cs',
]


def extract(path, sig):
    s = (root / path).read_bytes().decode('utf-8-sig')
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
    'public void PicksRequest(',
    'public void PickDelete(',
    'public Dictionary<UUID, string> GetPicks(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;partial class Original{'
    + ''.join(extract(sources[2], sig) for sig in sigs)
    + '}namespace OpenSim.Framework{'
    + extract(sources[4], 'public class GroupMembershipData')
    + '}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    StartupObject='DeleteFixture',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [
    root / sources[0],
    root / sources[1],
    root / sources[3],
    root / sources[5],
    root / sources[6],
    w / 'Extracted.cs',
]:
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


def enc(s):
    return '-' if s is None else s.encode('utf-16-le', 'surrogatepass').hex() or '.'


flows = [
    'D D Z 0',
    'A G D M R Z R',
    'A B D G D',
    'L D E D N D',
    'A t299999 D t300001 P R t600000 P G',
    'A G W D M D',
    'A m0 D m1 D C D',
    'N D C D G',
]
commands = [
    f'{mode} {flags} {enc(uri)} {enc(query)} {enc(name)} ' + flow
    for mode in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 17]
    for flags in [
        0,
        1,
        16,
        16 | 32,
        16 | 64,
        128,
        16 | 256,
        8192,
        1048576,
        16384,
        65536,
        16777216,
        33554432,
        16777216 | 33554432,
    ]
    for uri in ['http://profile/', ' ']
    for query in ['00000000-0000-0000-0000-000000000003', '00000000-0000-0000-0000-000000000000']
    for name in [None, 'raw' + chr(0xD800) + chr(0x1F600)]
    for flow in flows
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
    differences=diffs[:8],
    applicationParityVerified=False,
    scope='Unchanged PickDelete composed with original routing/GetPicks/PicksRequest, cache/models and actual SDK. Controlled client/RPC/timer boundaries. Exact method trace, query parameter, repeated caller ID reads, local/HG and blank routing, all RPC success/replacement/null/false/throw outcomes, shared picks/list removal and reply identity, missing/null/empty entries, preserved properties/classifieds/flags, expiry refresh/purge, caller mutation and synchronous GetPicks callback reentry. Full advanced scene runtime, remaining profile methods, application/macOS incomplete',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' pick deletion differences; see results.json'
print('PASS:', len(commands), 'original pick deletion flows')
