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
    'native-port/tests/profile_async_reference.cs',
    'OpenSim/Framework/Constants.cs',
    'OpenSim/Framework/Util.cs',
    'OpenSim/Services/Interfaces/OpenProfileClient.cs',
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
    'public void PicksRequest(',
    'public Dictionary<UUID, string> GetPicks(',
    'struct AsyncPropsRequest',
    'private void ProcessLocalRequests(',
    'private void ProcessHGRequests(',
    'private void ProcessRequest(',
    'public void RequestAvatarProperties(',
    'public void AvatarInterestsUpdate(',
    'public void AvatarPropertiesUpdate(',
    'bool GetProfileData(',
    'bool GetUserAccountData(',
    'public virtual bool IsFriendOnline(',
    'bool GetUserProfileServerURI(',
    'ScenePresence FindPresence(',
    'void cacheForeignImage(',
]
(w / 'Extracted.cs').write_text(
    'using System;using System.Collections;using System.Collections.Generic;using System.Collections.Concurrent;using System.Threading;using System.Globalization;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;using IGroupsModule=AsyncGroups;using IUserAccountService=AsyncAccounts;using UserAccount=AsyncAccount;using UserAgentServiceConnector=AsyncAgent;using IFriendsModule=AsyncFriends;using INPC=AsyncNpc;using OpenProfileClient=AsyncLegacy;partial class Original{'
    + ''.join(extract(sources[2], sig) for sig in sigs)
    + '}namespace OpenSim.Framework{'
    + extract(sources[4], 'public class GroupMembershipData')
    + '}'
)
import re

born = re.search(
    r'public static readonly DateTime m_MrOpenSimBorn[^;]*;', (root / sources[7]).read_text()
).group(0)
with (w / 'Extracted.cs').open('a') as out:
    out.write(
        'namespace OpenSim.Framework{public static partial class Constants{'
        + born
        + '}public static partial class Util{private static readonly DateTime UnixEpoch=new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc);'
        + extract(sources[8], 'public static DateTime ToDateTime(int seconds)')
        + 'public static void FireAndForget(WaitCallback callback){AsyncHost.Schedule(()=>callback(null));}}}'
    )
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    StartupObject='AsyncHost',
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
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData', 'log4net']:
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


commands = []


def add(
    mode=8,
    f=0,
    x=0,
    aflags=39,
    created=1,
    title='',
    uri='http://profile/',
    text='text',
    kind=0,
    flow='R1 R1 R2 R3 W A R1',
):
    commands.append(
        f'{mode} {f} {x} {aflags} {created} {enc(title)} {enc(uri)} {enc(text)} {kind} ' + flow
    )


flows = [
    'R1 R1 R2 R3 W A R1',
    'R1 I1 W V1 R1 R2 A R1',
    'B D3 R1 A R2',
    'E R1 R2 U1 A P1 A',
    'N R1 C W R2 A',
    'R0 R1 W R2 A',
    'T1 R2 R3 W A R1',
    'Z0 S1 J0 F1 H G',
    'K G P1 U1 A M G',
    'R1 t300001 Q R2 W A',
]
for mode in range(13):
    for f in [
        0,
        1,
        16,
        16 | 32,
        16 | 64,
        128,
        256,
        4194304,
        512,
        1024,
        2048,
        8192,
        1048576,
        524288,
        65536,
        134217728,
    ]:
        for flow in flows:
            add(mode=mode, f=f, flow=flow)
for bit in list(range(32)) + list(range(33, 48)) + [51]:
    for flow in flows:
        for f in [0, 16, 2048]:
            add(x=1 << bit, f=f, flow=flow)
for mode in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]:
    for x in [1 << 9, (1 << 9) | (1 << 11), (1 << 9) | (1 << 12), 1 << 10]:
        for flow in ['G R1 A', 'R1 R2 W A', 'P1 A U1 A']:
            add(mode=mode, f=16, x=x, flow=flow)
for kind in range(21):
    for flow in ['H H', 'R1 R2 W A R1']:
        add(f=16, kind=kind, flow=flow)
for title in [None, '', 'title ' + chr(0x1F600), 'raw' + chr(0xD800)]:
    for uri in [None, '', ' ', 'http://profile/']:
        for aflags, created in [(0, 0), (-1, -2147483648), (2147483647, 2147483647), (0x0F27, 1)]:
            for flow in ['R1 R2 A R1', 'H G U1 A P1 A']:
                add(title=title, uri=uri, aflags=aflags, created=created, text=title, flow=flow)
for f, hold in [(0, 'L'), (16, 'Y')]:
    for x in [0, 1 << 31]:
        for flow in [
            hold + ' R1 R1 l R1 R2 A R1',
            hold + ' R1 R2 I2 l R3 W A R1',
            hold + ' R0 l R2 A R3',
        ]:
            add(f=f, x=x, flow=flow)
for x in [
    (1 << 6) | (1 << 13),
    (1 << 6) | (1 << 29),
    (1 << 9) | (1 << 10),
    1 << 28,
    (1 << 18) | (1 << 22),
    (1 << 18) | (1 << 22) | (1 << 24),
    (1 << 18) | (1 << 22) | (1 << 25),
    (1 << 18) | (1 << 22) | (1 << 26),
]:
    for f in [0, 16, 2048]:
        add(f=f, x=x, flow='R1 R2 A F1 H G')
for mode in [8, 9, 10]:
    for f in [16, 16 | 32, 16 | 64]:
        for x in [
            1 << 52,
            (1 << 52) | (1 << 9),
            (1 << 52) | (1 << 9) | (1 << 53),
            (1 << 52) | (1 << 9) | (1 << 54),
        ]:
            for text in [
                None,
                '',
                '-31',
                'legacy ' + chr(0x1F600),
                '00000000-0000-0000-0000-000000000009',
            ]:
                for flow in ['G R1 A', 'R1 R2 A R1', 'P1 A U1 A G']:
                    add(mode=mode, f=f, x=x, text=text, flow=flow)
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
    scope='Unchanged async local/HG worker, ProcessRequest, RequestAvatarProperties, interests/properties updates and account/profile/friend helpers with original cache/DTO/group model, actual SDK and controlled services/scheduler/client boundaries. State/identity/error/send order and LIFO/dedup/waiters, active and scheduling failures, system/NPC and flags/policy, account strict boxed ints/date/title, legacy ref outcomes and image ordering, composed actual OpenProfile decoder fallback including staged conversion failures. Virtual online predicate is controlled for queue/broadcast cases; base friend helper separately exercised. Logger stack frames normalized to exception message. Native profile user-agent bridge is composed through the real UserAgentServiceConnector.GetUserInfo at a controlled XML-RPC client boundary for boxed scalar/collection/date/null/missing/error cases. Actual socket/legacy OpenProfile/global user-agent factory/full advanced host integration and simulator/macOS remain pending',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' async profile differences; see results.json'
print('PASS:', len(commands), 'original async profile flows')
