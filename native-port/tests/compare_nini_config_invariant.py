"""Run the identical concrete configuration flow through native C++ and actual Nini.dll."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    g = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        g,
        'Compile',
        Include=str(Path(__file__).with_name('nini_config_invariant_reference.cs').resolve()),
    )
    r = ET.SubElement(g, 'Reference', Include='Nini')
    dll = ROOT / 'bin/Nini.dll'
    ET.SubElement(r, 'HintPath').text = str(dll)
    file = w / 'Reference.csproj'
    ET.ElementTree(project).write(file)
    built = subprocess.run(
        [a.dotnet, 'build', str(file), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert not built.returncode, built.stdout + built.stderr
    observations = []
    for command in [[a.dotnet, str(w / 'app/Reference.dll')], [a.native]]:
        r = subprocess.run(command, capture_output=True, text=True)
        assert not r.returncode, r.stdout + r.stderr
        observations.append(r.stdout.strip())
    assert observations[0] == observations[1], observations
    report = dict(
        actual_nini_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(), observations=observations
    )
    (w / 'invariant-integration-results.json').write_text(json.dumps(report, indent=2))
    print('PASS: actual Nini.dll and native concrete numeric/culture/BOM/expansion/save-load flow')


if __name__ == '__main__':
    main()
