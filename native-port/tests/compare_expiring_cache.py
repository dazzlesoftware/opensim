"""Compare complete unchanged ExpiringCacheOS with a deterministic clock/timer."""

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
sources = ['native-port/tests/expiring_cache_reference.cs', 'OpenSim/Framework/ExpiringCacheOS.cs']
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
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert r.returncode == 0, r.stdout + r.stderr
flows = [
    'dA:1 dB:2 v v w v gA rA dC:3 v b c v z n dD:4 gA v z',
    'aA:1:{ms} t{now} gA kA p gA v n p rA p',
    'aA:1:{ms} aB:2:0 t{now} lA:{ms} hB:{ms} p v rA dC:3 v',
    't{now} dA:1 jB:2:{seconds} iC:3:{intseconds} v p gA gB gC v',
    'qA dA:1 v qA gA z qA',
    'v sA:{ms} n gA v z u gA kA v t{now} p c',
    'dA:1 v sB:{ms} n u v gB kB b p c',
    's_:{ms} n u v k_ d_:3 a_:4:{ms} g_ r_ h_:{ms} c',
    'dA:1 z gA dA:2 aB:3:{ms} p v kA c z n',
    'dA:1 f8 rA n f0 p c v',
    'f2 dA:1 n gA v f0 p dB:2 p c',
    'dA:1 f4 t{now} p f0 p v c',
    'f1 dA:1 aA:2:0 f0 gA v',
]
commands = []
for interval in [-2147483648, 0, 499, 500, 501, 60000, 2147483647]:
    for ms in [-2147483648, -1, 0, 1, 499, 500, 501, 300000, 2147483647]:
        for now in [
            '0',
            '499',
            '500',
            '501',
            '60000',
            '300000',
            '300001',
            '2147483647',
            '2147483648',
            '-1',
            'nan',
            'inf',
        ]:
            for seconds, intseconds in [
                ('0.001', 1),
                ('-1', -1),
                ('nan', 2147483647),
                ('inf', 2147484),
            ]:
                for flow in flows:
                    commands.append(
                        f'{interval} 0 '
                        + flow.format(ms=ms, now=now, seconds=seconds, intseconds=intseconds)
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
    scope='Complete unchanged ExpiringCacheOS with actual ReaderWriterLockSlim/Dictionary/CollectionsMarshal; controlled monotonic clock and System.Threading.Timer boundary. Exact state/expiration/timer calls, wrapping arithmetic, array identity/mutation, dictionary slot reuse, stale reads, disposal and source ref-return lock errors/partial mutation. Native real timer/concurrency integration remains separately required; whole application/macOS/CLR lifetime incomplete.',
    sourceHashes={f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert not diffs, str(len(diffs)) + ' cache differences; see results.json'
print('PASS:', len(commands), 'original expiring cache scenarios')
