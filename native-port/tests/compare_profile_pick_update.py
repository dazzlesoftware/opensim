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
    'native-port/tests/profile_pick_info_reference.cs',
    'OpenSim/Framework/Util.cs',
    'OpenSim/Framework/Constants.cs',
    'native-port/tests/profile_pick_update_reference.cs',
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
    'public void PickInfoRequest(',
    'public void PickInfoUpdate(',
    'public UserProfilePick GetPick(',
    'void cacheForeignImage(',
    'public Dictionary<UUID, string> GetPicks(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;using GridRegion=PickRegion;using ILandObject=ProfileFixtureLand;partial class Original{'
    + ''.join(extract(sources[2], sig) for sig in sigs)
    + '}namespace OpenSim.Framework{'
    + extract(sources[4], 'public class GroupMembershipData')
    + '}'
)
with (w / 'Extracted.cs').open('a') as out:
    out.write(
        'namespace OpenSim.Framework{public static partial class Util{'
        + ''.join(
            extract(sources[7], sig)
            for sig in [
                'public static UUID BuildFakeParcelID(ulong regionHandle, uint x, uint y)',
                'public static bool ParseFakeParcelID(UUID parcelID, out ulong regionHandle, out uint x, out uint y)',
            ]
        )
        + '}}'
    )
import re

constant = re.search(
    r'public const int MaxProfilePicks\s*=\s*\d+;', (root / sources[8]).read_text()
).group(0)
with (w / 'Extracted.cs').open('a') as out:
    out.write(
        'namespace OpenSim.Framework{public static partial class Constants{' + constant + '}}'
    )
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    StartupObject='EditFixture',
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
    root / sources[9],
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


import struct, math

id = lambda n: f'00000000-0000-0000-0000-{n:012x}'
flows = ['U U G', 'A U Z U G', 'D U G', 'E U G', 'F U B U', 'L U C U', 'N U R U', 'A t300001 P U']
commands = []


def add(
    listmode=1,
    getmode=8,
    update=8,
    f=0,
    seams=0,
    x=512,
    y=768,
    uri='http://profile/',
    creator=id(1),
    name='name',
    pos='<10,20,30>',
    old='<1,2,3>',
    gate='http://home/',
    flow='U G',
):
    commands.append(
        f'{listmode} {getmode} {update} {f} {seams} {x} {y} {enc(uri)} {enc(creator)} {enc(name)} {enc(pos)} {enc(old)} {enc(gate)} '
        + flow
    )


updates = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 16]
mode_rows = list(
    dict.fromkeys(
        [(1, 8, u) for u in updates]
        + [(m, 8, 8) for m in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 17]]
        + [(1, m, 8) for m in [0, 1, 3, 5, 6, 7, 8, 12, 13, 16]]
    )
)
flags = [
    0,
    1,
    16,
    16 | 32,
    16 | 64,
    128,
    16 | 256,
    256,
    8192,
    1048576,
    4096,
    2048,
    1024,
    512,
    65536,
    16384,
    16777216,
    33554432,
]
for listmode, getmode, update in mode_rows:
    for f in flags:
        for flow in flows:
            for kw in [
                dict(name=None),
                dict(
                    name='name ' + chr(0x1F600),
                    creator=id(2),
                    pos='<NaN,' + chr(0x221E) + ',-0>',
                    x=4294967295,
                    y=16777217,
                    old='bad',
                    gate=None,
                ),
                dict(
                    name='raw' + chr(0xD800) + chr(0),
                    pos='<1.25,-257.5,0.1>',
                    x=16777217,
                    y=4294967295,
                    old='1,2,3',
                    gate='',
                ),
                dict(uri=' ', creator=id(0)),
            ]:
                add(listmode=listmode, getmode=getmode, update=update, f=f, flow=flow, **kw)
for seams in [
    1,
    2,
    4,
    8,
    16,
    32,
    64,
    128,
    256,
    512,
    1024,
    2048,
    4096,
    8192,
    16384,
    32768,
    32768 | 16384,
    65536,
    32768 | 65536,
    131072,
    262144,
    524288,
    1048576,
    2097152,
    1048576 | 2097152,
]:
    for creator in [id(1), id(2)]:
        for flow in ['U U', 'A U G', 'F U B U', 'D U G']:
            add(seams=seams, creator=creator, flow=flow)
for update in updates:
    for seams in [0, 1048576, 2097152, 1048576 | 2097152, 32768]:
        for flow in ['U G', 'A U G', 'D U G']:
            add(update=update, seams=seams, flow=flow, old='bad')
for sign in range(2):
    for e in range(256):
        for m in [0, 1, (1 << 23) - 1]:
            v = struct.unpack('>f', ((sign << 31) | (e << 23) | m).to_bytes(4, 'big'))[0]
            number = (
                'NaN'
                if math.isnan(v)
                else ('-' if v < 0 else '') + chr(0x221E) if math.isinf(v) else repr(v)
            )
            add(
                listmode=12,
                x=16777217,
                y=4294967295,
                pos='<' + number + ', ' + number + ', -0>',
                flow='U G',
            )
outputs = {}
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    with (w / (name + '.txt')).open('w', encoding='utf-8') as out:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            stdout=out,
            stderr=subprocess.PIPE,
            text=True,
            timeout=300,
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
    scope='Complete unchanged PickInfoUpdate plus original GetPicks/GetPick, cache, Constants limit20, DTOs/fake parcel helpers and actual SDK vectors/UUID. Controlled scene/presence/land/grid/client/RPC boundaries. Exact state/identity/partial errors/send order; ownership/god and roots,20limit, separate client/presence scenes, uint+float rounding before double, all float exponents and safe unchecked coordinates, new default parcel warning/gatekeeper, six-field existing mutation before false RPC, ignored ref replacement/null on true, cache list/details/expiry, ignored invalid-position parse producing zero, list callback mutation/clear/reentry before detail reads. Full advanced runtime, remaining simulator and macOS incomplete',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' pick update differences; see results.json'
print('PASS:', len(commands), 'original pick update flows')
