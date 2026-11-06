"""Unchanged user-agent login/path/packing and base PackData; controlled CreateAgent boundary."""

from pathlib import Path
import argparse, subprocess, json, xml.etree.ElementTree as ET, hashlib, itertools

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', required=True, type=Path)
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
base = 'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs'
methods = ''.join(
    extract(agent, sig)
    for sig in [
        'public UserAgentServiceConnector(string url)',
        'private bool setServiceURL(string url)',
        'protected override string AgentPath()',
        'protected override void PackData(OSDMap args, GridRegion source, AgentCircuitData aCircuit, GridRegion destination, uint flags)',
        'public bool LoginAgentToGrid(GridRegion source, AgentCircuitData aCircuit, GridRegion gatekeeper, GridRegion destination, bool fromLogin, out string reason)',
        'public bool LoginAgentToGrid(GridRegion source, AgentCircuitData aCircuit, GridRegion gatekeeper, GridRegion destination, out string reason)',
    ]
)
(w / 'Methods.cs').write_text(
    'using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;class UserAgentServiceConnector:SimulationServiceConnector{static Log m_log=new();string m_ServerURL;GridRegion m_Gatekeeper;'
    + methods
    + 'public string Path()=>AgentPath();public void PackOnly(OSDMap args,GridRegion source,AgentCircuitData circuit,GridRegion destination,uint flags){PackData(args,source,circuit,destination,flags);}}partial class SimulationServiceConnector{protected virtual string AgentPath()=>"agent/";'
    + extract(
        base,
        'protected virtual void PackData(OSDMap args, GridRegion source, AgentCircuitData aCircuit, GridRegion destination, uint flags)',
    )
    + '}'
)
version = (root / 'OpenSim/Framework/VersionInfo.cs').read_bytes().decode('utf-8-sig')
declarations = []
for name in ['SimulationServiceVersionAcceptedMax', 'SimulationServiceVersionSupportedMax']:
    start = version.index('public readonly static float ' + name)
    end = version.index(';', start) + 1
    declarations.append(version[start:end])
(w / 'Version.cs').write_text(
    'namespace OpenSim.Framework{public static class VersionInfo{' + ''.join(declarations) + '}}'
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
paths = [
    'native-port/tests/user_agent_grid_reference.cs',
    'native-port/tests/user_agent_region_reference_stubs.cs',
    'OpenSim/Services/Interfaces/IGridService.cs',
    'OpenSim/Framework/RegionFlags.cs',
    'OpenSim/Framework/EntityTransferContext.cs',
]
for f in [*[root / f for f in paths], w / 'Methods.cs', w / 'Version.cs']:
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
rows = [f'{profile} {scenario}' for profile, scenario in itertools.product(range(512), range(8))]


def run(cmd):
    r = subprocess.run(
        cmd, input='\n'.join(rows) + '\n', capture_output=True, text=True, encoding='utf-8'
    )
    assert r.returncode == 0, r.stdout + r.stderr
    out = r.stdout.splitlines()
    assert len(out) == len(rows), (len(out), len(rows))
    return [json.loads(bytes.fromhex(x)) for x in out]


expected = run([a.dotnet, str(w / 'app/Reference.dll')])
actual = run([a.native])
for i, (e, n) in enumerate(zip(expected, actual)):
    if e != n:
        (w / 'failure.json').write_text(
            json.dumps(dict(index=i, input=rows[i], expected=e, actual=n), indent=2)
        )
        raise AssertionError(
            f'mismatch {i}: {rows[i]}\nexpected {json.dumps(e)}\nactual {json.dumps(n)}'
        )
(w / 'results.json').write_text(
    json.dumps(
        dict(
            passed=True,
            cases=len(rows),
            scope='Unchanged user-agent ctor/URL/AgentPath/LoginAgentToGrid overloads/PackData and unchanged base PackData; actual GridRegion/EntityTransferContext/SDK OSD. Explicitly controlled CreateAgent/circuit identity boundary; source gatekeeper mutation and retention, fields/defaults/flags/partial packing/reason/log exceptions. Full base CreateAgent HTTP/policy/logging parity not claimed',
            sourceHashes={
                f: hashlib.sha256((root / f).read_bytes()).hexdigest()
                for f in [agent, base, 'OpenSim/Framework/VersionInfo.cs', *paths]
            },
        ),
        indent=2,
    )
)
print(f'User-agent grid login {len(rows)} source cases passed')
