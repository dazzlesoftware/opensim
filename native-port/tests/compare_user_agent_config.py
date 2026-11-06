"""Unchanged user-agent config constructor + full GridInfo and bundled Nini.

DNS results, invariant culture and log callbacks are explicit fixture policies.
"""

from pathlib import Path
import argparse, subprocess, json, hashlib
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)


def extract(file, signature):
    s = (root / file).read_bytes().decode('utf-8-sig')
    start = s.index(signature)
    i = s.index('{', start) + 1
    depth = 1
    while depth:
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


agent = 'OpenSim/Services/Connectors/Hypergrid/UserAgentServiceConnector.cs'
util = 'OpenSim/Framework/Util.cs'
grid = 'OpenSim/Framework/GridInfo.cs'
methods = extract(agent, 'public UserAgentServiceConnector(IConfigSource config)').replace(
    'public UserAgentServiceConnector(', 'public ConfigConnector('
) + extract(agent, 'private bool setServiceURL(string url)')
helpers = ''.join(
    extract(util, s)
    for s in [
        'public static T GetConfigVarFromSections<T>(IConfigSource config, string varname, string[] sections)',
        'public static T GetConfigVarFromSections<T>(IConfigSource config, string varname, string[] sections, object val)',
    ]
)
generated = w / 'Extracted.cs'
generated.write_text(
    'using System;using Nini.Config;using OpenSim.Framework;class ConfigConnector{static log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(ConfigConnector));string m_ServerURL;public string URL=>m_ServerURL;'
    + methods
    + '}namespace OpenSim.Framework{public static partial class Util{'
    + helpers
    + '}}'
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
for f in [generated, root / grid, root / 'native-port/tests/user_agent_config_reference.cs']:
    ET.SubElement(items, 'Compile', Include=str(f))
ref = ET.SubElement(items, 'Reference', Include='Nini')
ET.SubElement(ref, 'HintPath').text = str(root / 'bin/Nini.dll')
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
profiles = [
    None,
    '',
    '[Const]\nHomeURI=http://home.example\n',
    '[GatekeeperService]\nExternalName=https://GATE.example:8002\n',
    '[GridService]\nGatekeeper=GATE.example\n',
]
values = [
    '',
    ' ',
    'http://gate.example',
    'https://GATE.example:8002',
    'gate.example',
    'bad/path',
    'http://bad.invalid',
    'http://gate.example/Path?Query=Case',
    'http://gate.example:65536',
    'https://gate.example/',
    'http://gate.example:0',
    'HTTP://\u00c9XAMPLE.test',
    'https://\u03a3igma.example:8002',
]
values.extend(
    [
        'http://\U0001f600.example',
        'http://' + 'a.' * 128 + 'a',
        '::1',
        'http\U0001f600host',
        'h\u00adttpxxxhost',
        'http://host:\u00a080',
        'http://host:80\0',
    ]
)
for section in ['Const', 'Startup', 'Hypergrid']:
    for value in values:
        profiles.append(f'[{section}]\nGatekeeperURI={value}\n')
        profiles.append(f'[{section}]\nGatekeeperURI=http://gate.example\nHomeURI={value}\n')
for key, value in [
    ('SearchServerURI', 'bad/path'),
    ('SearchServerURI', 'http://bad.invalid'),
    ('SearchServerURI', 'https://search.example/Path?q=Case'),
    ('DestinationGuideURI', 'bad/path'),
    ('DestinationGuide', 'http://bad.invalid'),
    ('economy', 'bad/path'),
    ('economy', 'http://bad.invalid'),
    ('economy', 'https://economy.example/Case'),
]:
    profiles.append(
        '[Const]\nGatekeeperURI=http://gate.example\n[GridInfo]\n' + key + '=' + value + '\n'
    )
for key in ['SearchServerURI', 'DestinationGuideURI', 'economy']:
    for value in [
        'http://[fe80::1%25Eth0]:9000/a?q=1#Case',
        'https://\u00c9XAMPLE.test/Path/\u8a66\u9a13',
        'https://user:pass@host:443/a/%2E./b',
        'http://host/%41%7e%2f%FF?q=\u00e9#Case',
        'http://u@v@host/',
        ' http://host/a ',
    ]:
        profiles.append(
            '[Const]\nGatekeeperURI=http://gate.example\n[GridInfo]\n' + key + '=' + value + '\n'
        )
profiles.extend(
    [
        '[Const]\ngatekeeperuri=http://gate.example\n',
        '[const]\nGatekeeperURI=http://gate.example\n',
        '[Const]\nGatekeeperURI=bad/path\n[Startup]\nGatekeeperURI=http://second.example\n[Hypergrid]\nHomeURI=https://HOME.example:8002\n',
        '[Const]\nGatekeeperURI=http://gate.example\nGatekeeperURIAlias=one.example,https://two.example:8002,bad/path\nHomeURIAlias=three.example\n[GridInfo]\nGridName=Info\nGridNick=IN\nStunServers=stun.example:3478,bad/path\n',
    ]
)


def h(s):
    return '-' if s is None else s.encode('utf-16le', 'surrogatepass').hex() or '.'


rows = [f'{flags} {h(profile)}' for profile in profiles for flags in range(16)]
outputs = []
for cmd in [[a.dotnet, str(w / 'app/Reference.dll')], [a.native]]:
    r = subprocess.run(
        cmd,
        input='\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=120,
    )
    assert r.returncode == 0, r.stdout[-1000:] + r.stderr
    out = r.stdout.splitlines()
    assert len(out) == len(rows), (len(out), len(rows))
    outputs.append(out)
differences = [
    dict(index=i, input=row, source=s, native=n)
    for i, (row, s, n) in enumerate(zip(rows, *outputs))
    if s != n
]
report = dict(
    passed=not differences,
    cases=len(rows),
    differences=differences[:10],
    scope=__doc__,
    applicationParityVerified=False,
    sourceHashes={
        f: hashlib.sha256((root / f).read_bytes()).hexdigest()
        for f in [
            agent,
            grid,
            util,
            'bin/Nini.dll',
            'native-port/tests/user_agent_config_reference.cs',
        ]
    },
)
(w / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['passed'], f'{len(differences)} config differences; see ' + str(w / 'results.json')
print(f'PASS: {len(rows)} full user-agent config source cases')
