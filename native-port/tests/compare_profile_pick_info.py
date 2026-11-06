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
    'public UserProfilePick GetPick(',
    'void cacheForeignImage(',
    'public Dictionary<UUID, string> GetPicks(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;using GridRegion=PickRegion;partial class Original{'
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
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    StartupObject='PickFixture',
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


import uuid, struct, math

encid = lambda n: f'00000000-0000-0000-0000-{n:012x}'
valid = str(uuid.UUID(bytes=bytes.fromhex('00010000000200000300000004000000')))
invalid = encid(5)
flows = [
    'R R G M R G',
    'G G R G C G',
    'A G M R C R',
    'D G R E G R',
    'W R S R C R',
    'R t300001 P G R',
    'G H R H',
]
flags = [
    0,
    1,
    2,
    4,
    8,
    2097152,
    16,
    16 | 32,
    16 | 64,
    128,
    16 | 256,
    256,
    32,
    4194304,
    524288,
    65536,
    268435456,
    67108864,
    134217728,
]
commands = []


def add(
    mode=1,
    f=0,
    seams=0,
    local=0,
    x=512,
    y=768,
    uri='http://profile/',
    target=encid(1),
    query=encid(3),
    name='name',
    pos='<1,2,3>',
    gate='http://foreign/',
    asset='http://assets/',
    parcel=invalid,
    flow='R G',
):
    commands.append(
        f'{mode} {f} {seams} {local} {x} {y} {enc(uri)} {enc(target)} {enc(query)} {enc(name)} {enc(pos)} {enc(gate)} {enc(asset)} {enc(parcel)} '
        + flow
    )


for mode in range(18):
    for f in flags:
        for flow in flows:
            for kw in [
                dict(),
                dict(uri=' ', name=None, pos=None, gate=None, asset=None, parcel=valid),
                dict(
                    name='raw' + chr(0xD800) + chr(0x1F600),
                    pos='<NaN,-0,' + chr(0x221E) + '>',
                    parcel=valid,
                ),
                dict(target=None, query='bad', pos='bad'),
            ]:
                add(mode=mode, f=f, flow=flow, **kw)
for seams in [0, 1, 2, 4, 8, 16, 32, 64, 256, 512, 768]:
    for local in [-1, 0, 1]:
        for mode in [1, 3, 8, 14, 15]:
            for x, y in [(512, 768), (-512, 256), (-2147483648, 2147483647)]:
                for parcel in [valid, invalid, encid(0)]:
                    for flow in ['G G R G', 'R G M R']:
                        add(
                            mode=mode,
                            seams=seams,
                            local=local,
                            x=x,
                            y=y,
                            parcel=parcel,
                            pos='<4294967297.9, -257.9, -0>',
                            flow=flow,
                        )
for sign in range(2):
    for exponent in range(2048):
        for mantissa in [0, (1 << 52) - 1]:
            bits = (sign << 63) | (exponent << 52) | mantissa
            v = struct.unpack('>d', bits.to_bytes(8, 'big'))[0]
            numeric = (
                'NaN'
                if math.isnan(v)
                else ('-' if v < 0 else '') + chr(0x221E) if math.isinf(v) else repr(v)
            )
            add(pos='<' + numeric + ', ' + numeric + ', -0>', flow='R G', x=-512, y=2147483647)
for asset in [None, '', ' ']:
    for parcel in [valid, invalid]:
        for flow in ['R G', 'G G R G']:
            add(asset=asset, parcel=parcel, flow=flow)
for target, query in [
    (None, None),
    ('bad', None),
    (encid(1), None),
    (encid(1), 'bad'),
    (encid(0), encid(0)),
]:
    for f in [0, 1, 2, 4, 8, 2097152]:
        add(target=target, query=query, f=f, flow='R G')
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
    scope='Complete unchanged PickInfoRequest/GetPick, routing/cache/DTOs, original fake parcel methods and actual SDK UUID/Vector3d. Controlled scene/grid/client/RPC/timer boundaries; exact state/identity/errors and send/lookup/image order, cached null/invalid/empty entries, fresh GetPick no cache insert, returned-ID keys,300sec expiry, source-specific valid-vs-invalid parcel image caching, int32+uint32 promotion and unchecked double coordinate conversions through every exponent/sign, raw UTF16 fields and malformed sender/args, object-ref mutation/replacement/null/casts and reentry. Full advanced runtime, remaining simulator and macOS incomplete',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' pick detail differences; see results.json'
print('PASS:', len(commands), 'original pick detail flows')
