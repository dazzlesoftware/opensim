"""Actual BCL observations of invariant native Flotsam date binding."""

import argparse, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_invariant_datetime_parse import Peer


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
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(Path(__file__).with_name('native_flotsam_datetime_reference.cs').resolve()),
    )
    file = w / 'Reference.csproj'
    ET.ElementTree(project).write(file)
    r = subprocess.run(
        [a.dotnet, 'build', str(file), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    values = [
        None,
        '',
        'now',
        'NOW',
        'nOw',
        'n\u00adow',
        'no\u200bw',
        'now\0',
        'n\u034fow',
        'not a date',
        '2024-01-01 trailing',
        '99999-12-31',
    ]
    for y, m, d, sep in itertools.product(
        [1, 99, 2000, 2024, 9999], [1, 2, 12], [1, 28, 29, 31], ['-', '/']
    ):
        date = f'{y:04}{sep}{m:02}{sep}{d:02}'
        for suffix in ['', ' 01:23:45', 'T12:59:59Z', 'T12:59:59.1234567+05:30']:
            values.append(date + suffix)
    source = Peer([a.dotnet, str(w / 'app/Reference.dll')])
    native = Peer([a.native, '--probe'])
    differences = []
    try:
        for value in values:
            q = '~' if value is None else value.encode().hex()
            expected = source.ask(q)
            actual = native.ask(q)
            if expected != actual:
                differences.append([value, expected, actual])
        result = dict(cases=len(values), differences=differences)
        (w / 'binding-results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result))
        return bool(differences)
    finally:
        source.close()
        native.close()


if __name__ == '__main__':
    raise SystemExit(main())
