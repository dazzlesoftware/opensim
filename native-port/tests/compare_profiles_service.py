"""Compare all operations of the unchanged original UserProfilesService."""

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
    'native-port/tests/profiles_service_reference.cs',
    'OpenSim/Services/UserProfilesService/UserProfilesService.cs',
    'OpenSim/Services/Interfaces/IUserProfilesService.cs',
    'OpenSim/Data/IProfilesData.cs',
    'OpenSim/Framework/UserProfiles.cs',
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
for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData', 'Nini']:
    ref = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / str(name + '.dll'))
support = w / 'Base.cs'
support.write_text(
    'namespace OpenSim.Services.ProfilesService{public class UserProfilesServiceBase{public OpenSim.Data.IProfilesData ProfilesData;public UserProfilesServiceBase(Nini.Config.IConfigSource config,string name){ProfilesData=Fixture.Data;}}}'
)
ET.SubElement(items, 'Compile', Include=str(support))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
commands = [
    f'{method} {mode} {email} {account} {flags} {seed}'
    for method in range(18)
    for mode in range(10)
    for email in range(4)
    for account in range(8)
    for flags in range(4)
    for seed in range(2)
]
outputs = {}
for name, cmd in [('reference', [a.dotnet, str(w / 'app/Reference.dll')]), ('native', [a.native])]:
    with (w / (name + '.txt')).open('w', encoding='utf-8') as output:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
            timeout=180,
        )
    assert r.returncode == 0, r.stderr
    outputs[name] = (w / (name + '.txt')).read_text(encoding='utf-8').splitlines()
    assert len(outputs[name]) == len(commands), (name, len(outputs[name]), len(commands))
diffs = [
    dict(command=c, source=x, native=y)
    for c, x, y in zip(commands, outputs['reference'], outputs['native'])
    if x != y
]
report = dict(
    passed=not diffs,
    scenarios=len(commands),
    differenceCount=len(diffs),
    differences=diffs[:12],
    applicationParityVerified=False,
    scope='Full unchanged original UserProfilesService operation bodies and constructor, original IUserProfilesService/IProfilesData/DTOs with actual SDK and Nini. Controlled storage and account plugin/base-constructor boundary; eighteen operations with provider failures/throws/ref replacement/null, missing data and null DTOs, initial/null result strings, local concrete/subclass versus remote account identity, email fallback and nested preference writes. Direct typed calls retain ref updates even on exception. Configuration-base/plugin implementation, SQLite/MySQL/PGSQL persistence, executable/viewer wiring and macOS/application parity remain incomplete.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' profile service differences; see results.json'
print('PASS:', len(commands), 'original profile service scenarios')
