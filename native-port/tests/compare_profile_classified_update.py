"""Original classified-detail workflow composed with actual SDK, profile/group models and cache."""

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
    'native-port/tests/profile_classifieds_reference.cs',
    'native-port/tests/profile_classified_info_reference.cs',
    'OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs',
    'OpenSim/Framework/UserProfiles.cs',
    'OpenSim/Framework/GroupData.cs',
    'OpenSim/Framework/ExpiringCacheOS.cs',
    'native-port/tests/profile_classified_update_reference.cs',
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
    'public Dictionary<UUID, string> GetPicks(',
    'public void ClassifiedsRequest(',
    'public void ClassifiedInfoRequest(',
    'public void ClassifiedInfoUpdate(',
    'public void ClassifiedDelete(',
    'void cacheForeignImage(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Linq;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;using Scene=UpdateScene;using ILandObject=UpdateLand;using IMoneyModule=UpdateMoney;partial class Original{'
    + ''.join(extract(sources[4], sig) for sig in sigs)
    + '}namespace OpenSim.Framework{'
    + extract(sources[6], 'public class GroupMembershipData')
    + '}'
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    StartupObject='UpdateFixture',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
for f in [
    root / sources[0],
    root / sources[1],
    root / sources[2],
    root / sources[3],
    root / sources[5],
    root / sources[7],
    root / sources[8],
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


flags = [
    0,
    1,
    16,
    16 | 32,
    16 | 64,
    128,
    16 | 256,
    8192,
    1048576,
    4096,
    2048,
    1024,
    4194304,
    65536,
    16384,
]
flows = ['U U', 'A U U', 'A B U C U', 'A R U M U']
commands = [
    f'{listmode} {typed} {f} {seam} {enc(uri)} {enc(name)} {category} {estate} {price} {enc(pos)} '
    + flow
    for listmode, typed in [
        (1, 1),
        (12, 1),
        (12, 0),
        (12, 5),
        (12, 7),
        (1, 6),
        (1, 0),
        (0, 1),
        (2, 1),
        (3, 1),
        (4, 1),
        (5, 1),
        (6, 1),
        (8, 1),
        (10, 1),
        (11, 1),
        (13, 1),
        (14, 1),
        (17, 1),
    ]
    for f in flags
    for seam in [0, 128, 512, 2048]
    for uri, name, category, estate, price, pos in [
        ('http://profile/', None, 1, 2, -100, '<1,2,3>'),
        (' ', 'raw' + chr(0xD800) + 'q', 4294967295, 0, 2147483647, '<NaN,-0,' + chr(0x221E) + '>'),
        ('http://profile/', 'name ' + chr(0x1F600) + '<&', 2147483648, 0, 0, '<0,0,0>'),
        (
            'http://profile/',
            'a' + chr(0) + 'b',
            1,
            4294967295,
            -2147483648,
            '<1E-40,1E30,-' + chr(0x221E) + '>',
        ),
    ]
    for flow in flows
]
commands += [
    f'{listmode} 1 0 {seam} {enc("http://profile/")} {enc("name")} 1 2 100 {enc("<1,2,3>")} A U U'
    for listmode in [1, 12]
    for seam in [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536]
]
commands += [
    f'12 {typed} 0 0 {enc("http://profile/")} {enc(name)} 1 2 {price} {enc(pos)} A U U'
    for typed in [1, 3, 5, 6]
    for name in [None, 'name ' + chr(0x1F600) + '<&', 'raw' + chr(0xD800) + 'q']
    for price in [-2147483648, 0, 2147483647]
    for pos in [
        '<NaN,-0,' + chr(0x221E) + '>',
        '<1E-40,1E30,-' + chr(0x221E) + '>',
        '<0.1,0.00001,123456789>',
        '<0,0,0>',
    ]
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
    scope='Complete unchanged ClassifiedInfoUpdate compiled with original routing, cache and DTOs and actual SDK OSD/UUID/vector formatting.Scenarios control scene/client/land/money/RPC boundaries and compare exact traces, exceptions, typed ad fields and shared cache state across repeated updates. Includes initial scene/agent/land lookup order, changing caller IDs, local/HG refusal, short-circuit existing-row enumeration, new-ad money absence/coverage/charge/failures, checked category/estate conversions, current parcel versus ignored supplied ID, null land data/presence/region info, raw UTF16 names, typed RPC ref replacement/null/failure ignored on success, cache invalidation and retained other fields/owner counters. Full advanced module/runtime and application/macOS remain pending',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' classified update differences; see results.json'
print('PASS:', len(commands), 'original classified update flows')
