"""Compile the complete original response class; control random UUIDs and epoch clock.
This fixture establishes default/public property/conversion/alias behavior.
The full account/destination constructor is exercised with instrumented library, region and timezone providers.
"""

import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference, ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    base, _ = reference(a.dotnet, w / 'base')
    project = ET.parse(base)
    ET.SubElement(project.getroot().find('PropertyGroup'), 'StartupObject').text = 'ResponseProgram'
    group = project.getroot().find('ItemGroup')
    # Extend only this generated fixture's logger with the complete response class's calls.
    for item in group:
        if (
            item.tag == 'Compile'
            and Path(item.get('Include')).name == 'appearance_provisioner_reference.cs'
        ):
            text = Path(item.get('Include')).read_text(encoding='utf-8')
            text = text.replace(
                'void Debug(object o);',
                'void Debug(object o);void DebugFormat(string f,params object[] a);void WarnFormat(string f,params object[] a);',
            ).replace(
                'public void Debug(object o){}',
                'public void Debug(object o){}public void DebugFormat(string f,params object[] a){}public void WarnFormat(string f,params object[] a){}',
            )
            stub = w / 'Provisioner.cs'
            stub.write_text(text, encoding='utf-8')
            item.set('Include', str(stub))
    util = w / 'base/Util.cs'
    source = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    uui = extract(
        source, r'public static bool ParseUniversalUserIdentifier\(string value, out UUID uuid\)'
    )
    util.write_text(
        'using OpenMetaverse;namespace OpenSim.Framework{public static class Util{public static int UnixTimeSinceEpoch()=>0;public static uint RegionToWorldLoc(uint n)=>n<<8;public static string Md5Hash(string s)=>System.Convert.ToHexString(System.Security.Cryptography.MD5.HashData(System.Text.Encoding.UTF8.GetBytes(s))).ToLowerInvariant();'
        + uui
        + '}}',
        encoding='utf-8',
    )
    caps = w / 'CapsUtil.cs'
    caps.write_text(
        'namespace OpenSim.Framework {public static class CapsUtil {'
        + extract(
            (ROOT / 'OpenSim/Framework/CapsUtil.cs').read_text(encoding='utf-8-sig'),
            r'public static string GetCapsSeedPath\(',
        )
        + '}}',
        encoding='utf-8',
    )
    ET.SubElement(group, 'Compile', Include=str(caps))
    constants = w / 'Constants.cs'
    original = (ROOT / 'OpenSim/Framework/Constants.cs').read_text(encoding='utf-8-sig')
    declarations = [
        re.search(
            r'public (?:const|static readonly) [^;\n]+ ' + name + r' = [^;]+;', original
        ).group()
        for name in ['MaxAgentGroups', 'm_MrOpenSimIDString', 'm_MrOpenSimID']
    ]
    constants.write_text(
        'using OpenMetaverse;namespace OpenSim.Framework{public class Constants {'
        + ''.join(declarations)
        + '}}',
        encoding='utf-8',
    )
    ET.SubElement(group, 'Compile', Include=str(constants))
    path_source = 'OpenSim/Services/LLLoginService/LLLoginResponse.cs'
    source = (
        (ROOT / path_source)
        .read_text(encoding='utf-8-sig')
        .replace('UUID.Random()', 'ResponseFixture.NextId()')
        .replace(
            '(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds', 'ResponseFixture.Seconds'
        )
    )
    source = (
        source.replace(
            'TimeZoneInfo.Local.IsDaylightSavingTime(DateTime.Now)', 'ResponseFixture.LocalDst()'
        )
        .replace('TimeZoneInfo.FindSystemTimeZoneById(tzName)', 'ResponseFixture.FindZone(tzName)')
        .replace(
            'dstTimeZone.IsDaylightSavingTime(DateTime.Now)', 'ResponseFixture.IsDst(dstTimeZone)'
        )
    )
    full = w / 'Response.cs'
    full.write_text(source, encoding='utf-8')
    for f in [
        full,
        ROOT / 'native-port/tests/login_response_reference.cs',
        ROOT / 'native-port/tests/login_response_assembly_reference.cs',
        ROOT / 'OpenSim/Framework/AgentCircuitData.cs',
        ROOT / 'OpenSim/Framework/Culture.cs',
        ROOT / 'OpenSim/Services/Interfaces/ILoginService.cs',
        ROOT / 'OpenSim/Services/Interfaces/IFriendsService.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    # Inventory item creation time is unrelated to response fields and remains instrumented.
    item = w / 'base/InventoryItemBase.cs'
    item.write_text(
        item.read_text(encoding='utf-8').replace('Fixture.Clock()', '0'), encoding='utf-8'
    )
    path = w / 'LoginResponse.csproj'
    project.write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)

    def t(s):
        return '-' if s is None else s.encode().hex() or '.'

    commands = [
        'new',
        'llsd',
        'xml',
        'xml',
        'held 0',
        'list InventoryLibRoot 1',
        'llsd',
        'ui ' + t('custom') + ' ' + t('value'),
        'xml',
        'held 0',
        'mut-ui 0 ' + t('after') + ' ' + t('live'),
        'held 1',
        'llsd',
        'mut-inventory 0',
        'held 2',
        'xml',
        'llsd',
    ]
    strings = [
        'Login',
        'DST',
        'StipendSinceLogin',
        'Gendered',
        'EverLoggedIn',
        'Firstname',
        'Lastname',
        'AgentAccess',
        'AgentAccessMax',
        'StartLocation',
        'LookAt',
        'SeedCapability',
        'ErrorReason',
        'ErrorMessage',
        'SimAddress',
        'Home',
        'MapTileURL',
        'ProfileURL',
        'OpenIDURL',
        'SearchURL',
        'Message',
        'Currency',
        'ClassifiedFee',
        'DestinationsURL',
        'AvatarsURL',
        'SunTexture',
        'CloudTexture',
        'MoonTexture',
    ]
    for field in strings:
        for value in [None, '', 'custom <&> 試験']:
            commands += [
                'new',
                'list InventoryLibRoot 1',
                'set ' + field + ' ' + t(value),
                'xml',
                'llsd',
            ]
    for field in [
        'InventoryRoot',
        'InventorySkeleton',
        'InventoryLibrary',
        'InventoryLibraryOwner',
        'InventoryLibRoot',
        'ActiveGestures',
    ]:
        for mode in range(5):
            commands += ['new', 'list InventoryLibRoot 1', f'list {field} {mode}', 'xml', 'llsd']
    for field in ['SimPort', 'SimHttpPort', 'RegionX', 'RegionY']:
        for n in [0, 2147483647, 2147483648, 4294967295]:
            commands += ['new', 'list InventoryLibRoot 1', f'num {field} {n}', 'xml', 'llsd']
    for field in ['CircuitCode', 'MaxAgentGroups']:
        for n in [-2147483648, -1, 0, 2147483647]:
            commands += ['new', 'list InventoryLibRoot 1', f'num {field} {n}', 'xml', 'llsd']
    for seconds in [
        0.0,
        -1.9,
        1700000000.9,
        2147483647.0,
        2147483647.9,
        2147483648.0,
        -2147483648.0,
        -2147483648.1,
        float('nan'),
        float('inf'),
    ]:
        commands += [
            'new',
            'list InventoryLibRoot 1',
            'clock ' + struct.pack('>d', seconds).hex(),
            'xml',
            'llsd',
        ]
    commands += [
        'clock ' + struct.pack('>d', 1700000000.0).hex(),
        'new',
        'list InventoryLibRoot 1',
        'buddy ' + t('id') + ' -1 2147483647',
        'buddy - 3 4',
        'xml',
        'llsd',
        'buddy-null',
        'xml',
        'xml',
        'llsd',
        'buddy-reset',
        'xml',
        'held 0',
        'category 99 -',
        'ui ' + t('allow_first_life') + ' ' + t('overridden'),
        'xml',
        'held 0',
        'mut-global 0 ' + t('mutated'),
        'xml',
        'llsd',
        'new',
        'xml',
        'list InventoryLibRoot 1',
        'llsd',
    ]
    commands += [f'construct {profile} {variant}' for profile in range(45) for variant in range(4)]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/LoginResponse.dll')], [a.native]]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=45,
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-500:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [
        {'index': i, 'input': c, 'source': x, 'native': y}
        for i, (c, x, y) in enumerate(zip(commands, *rows))
        if x != y
    ]
    (w / 'login-response-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'constructorProfiles': 180,
                'sourceHashes': {
                    s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                    for s in [
                        path_source,
                        'OpenSim/Framework/Constants.cs',
                        'OpenSim/Framework/CapsUtil.cs',
                        'OpenSim/Services/Interfaces/ILoginService.cs',
                        'OpenSim/Services/Interfaces/IFriendsService.cs',
                    ]
                },
                'differences': diffs[:3],
            },
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:1], indent=2)[:8000])
    print(f'PASS: {len(commands)} login response source observations')


if __name__ == '__main__':
    main()
