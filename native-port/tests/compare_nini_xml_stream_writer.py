"""Actual pinned runtime oracle for the shared native INI/XML StreamWriter."""

import argparse, json, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
tests = Path(__file__).resolve().parent
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for key, value in {
    'OutputType': 'Exe',
    'TargetFramework': 'net8.0',
    'EnableDefaultCompileItems': 'false',
    'Nullable': 'enable',
    'RuntimeFrameworkVersion': '8.0.31',
    'RollForward': 'Disable',
}.items():
    ET.SubElement(props, key).text = value
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(tests / 'nini_xml_stream_writer_reference.cs'))
file = work / 'Reference.csproj'
ET.ElementTree(project).write(file)
subprocess.run(
    [a.dotnet, 'build', str(file), '-o', str(work / 'app'), '--nologo', '-v', 'quiet'], check=True
)
results = {}
for name, script in [
    ('streams', 'compare_nini_xml_stream_writer_cases.py'),
    ('encodings', 'compare_nini_xml_stream_encodings.py'),
]:
    folder = work / name
    subprocess.run(
        [
            sys.executable,
            str(tests / script),
            '--native',
            str(Path(a.native).resolve()),
            '--dotnet',
            a.dotnet,
            '--reference',
            str(work / 'app/Reference.dll'),
            '--work',
            str(folder),
        ],
        check=True,
    )
    results[name] = json.loads((folder / 'results.json').read_text())
(work / 'results.json').write_text(
    json.dumps(
        {
            'applicationParityVerified': False,
            'gates': results,
            'scope': 'Shared synchronous INI/XML StreamWriter, intrinsic encodings and typed error metadata. Complete default XML backend, alternate encoding policies/providers and full simulator parity remain incomplete.',
        },
        indent=2,
    )
)
