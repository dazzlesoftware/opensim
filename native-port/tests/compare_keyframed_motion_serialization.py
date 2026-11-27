"""Compare import against actual original KeyframeMotion BinaryFormatter output."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--cases')
p.add_argument('--dotnet')
p.add_argument('--reference', type=Path)
p.add_argument('--work', type=Path)
p.add_argument('--output', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
if not a.cases:
    assert a.dotnet and a.work, '--cases or --dotnet and --work required'
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    properties = ET.SubElement(project, 'PropertyGroup')
    for name, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false',
                            EnableUnsafeBinaryFormatterSerialization='true', NoWarn='SYSLIB0011;SYSLIB0050').items():
        ET.SubElement(properties, name).text = value
    items = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(items, 'Compile', Include=str(root / 'native-port/tests/keyframed_motion_serialization_reference.cs'))
    for name in ['OpenSim.Region.Framework', 'OpenSim.Framework', 'OpenMetaverseTypes', 'OpenMetaverse']:
        ref = ET.SubElement(items, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(root / 'bin' / (name+'.dll'))
    cs = work / 'Reference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run([a.dotnet, 'build', str(cs), '-c', 'Release', '-o', str(work / 'app')],
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout+r.stderr
    r = subprocess.run([a.dotnet, str(work / 'app/Reference.dll')], capture_output=True, timeout=60)
    assert r.returncode == 0, r.stderr
    a.cases = work / 'cases.jsonl'
    a.cases.write_bytes(r.stdout)
    a.reference = work / 'app/Reference.dll'
raw = Path(a.cases).read_bytes()
cases = [json.loads(line) for line in raw.decode('utf-8-sig').splitlines()]
inputs = [{'nrbf': c['nrbf']} for c in cases]
# The source rejects these incomplete stream headers. This is deliberately
# limited corruption coverage; it is not a claim of malformed graph parity.
inputs += [{'nrbf': ''}, {'nrbf': '00'}, {'nrbf': cases[0]['nrbf'][:32]}]
expected = [c['expected'] for c in cases] + [None] * 3
r = subprocess.run([a.native], input='\n'.join(map(json.dumps, inputs))+'\n',
                   capture_output=True, text=True, timeout=60)
assert r.returncode == 0, r.stderr
actual = [json.loads(line) for line in r.stdout.splitlines()]
assert len(actual) == len(expected), (len(actual), len(expected))
differences = [{'case': i, 'expected': e, 'actual': v}
               for i, (e, v) in enumerate(zip(expected, actual)) if e != v]
export_comparisons = 0
if a.reference:
    r = subprocess.run([a.native], input='\n'.join(json.dumps({'nrbf':c['nrbf'], 'export':True}) for c in cases)+'\n',
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, (r.returncode, r.stderr)
    exports = r.stdout
    native = subprocess.run([a.native], input=exports, capture_output=True, text=True, timeout=60)
    original = subprocess.run([a.dotnet, str(a.reference), '--decode'], input=exports,
                              capture_output=True, text=True, timeout=60)
    assert native.returncode == original.returncode == 0, (native.stderr, original.stderr)
    for direction, output in [('nativeRoundtrip', native.stdout), ('originalImport', original.stdout)]:
        values = [json.loads(line) for line in output.splitlines()]
        assert len(values) == len(cases)
        for i, (case, value) in enumerate(zip(cases, values)):
            if case['expected'] != value:
                differences.append({'case':i, 'direction':direction, 'expected':case['expected'], 'actual':value})
            export_comparisons += 1
report = {'originalPayloads': len(cases), 'shortStreamRejections': 3,
          'exportComparisons': export_comparisons,
          'sourceHashes': {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                           for name in ['OpenSim/Region/Framework/Scenes/KeyframeMotion.cs',
                                        'bin/OpenSim.Region.Framework.dll', 'bin/OpenMetaverseTypes.dll']},
          'differences': differences, 'corpusSha256': hashlib.sha256(raw).hexdigest(),
          'scope': 'Typed NRBF codec; live restoration, list mutation tracking and malformed graph parity pending'}
Path(a.output).write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({k: v for k, v in report.items() if k != 'differences'}))
assert not differences, f'{len(differences)} differences; see {a.output}'
