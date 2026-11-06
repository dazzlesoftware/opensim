"""Actual pinned System.Xml internal character/name kernel differential."""

import argparse, hashlib, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path


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
    for k, v in {
        'OutputType': 'Exe',
        'TargetFramework': 'net8.0',
        'EnableDefaultCompileItems': 'false',
        'Nullable': 'enable',
        'RuntimeFrameworkVersion': '8.0.31',
        'RollForward': 'Disable',
    }.items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(Path(__file__).with_name('nini_xml_character_reference.cs').resolve()),
    )
    file = w / 'Reference.csproj'
    ET.ElementTree(project).write(file)
    r = subprocess.run(
        [a.dotnet, 'build', str(file), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    rows = ['-'] + [f'{i:04x}' for i in range(65536)]
    for i in range(65536):
        rows.extend([f'0061{i:04x}', f'{i:04x}0061', f'0061003a{i:04x}'])
    for hi in range(0xD800, 0xDC00):
        for lo in [0, 0xDBFF, 0xDC00, 0xDC01, 0xDFFF, 0xE000]:
            rows.extend([f'{hi:04x}{lo:04x}', f'0061{hi:04x}{lo:04x}003a0062'])
    rng = random.Random(2134)
    for _ in range(12000):
        rows.append(''.join(f'{rng.randrange(65536):04x}' for _ in range(rng.randrange(1, 32))))
    rows += [
        str(offset) + ' ' + value
        for value in [
            '-',
            '0061',
            '0061003a0062',
            '003a0061003a',
            '0061003a0062003a0063',
            'd800dc00',
        ]
        for offset in range(9)
    ]
    text = '\n'.join(rows) + '\n'
    expected = subprocess.run(
        [a.dotnet, str(w / 'app/Reference.dll')], input=text, capture_output=True, text=True
    )
    actual = subprocess.run(
        [str(Path(a.native).resolve()), '--probe'], input=text, capture_output=True, text=True
    )
    assert expected.returncode == 0, expected.stderr
    assert actual.returncode == 0, actual.stderr
    e = expected.stdout.splitlines()
    n = actual.stdout.splitlines()
    assert len(e) == len(rows), (len(e), len(rows))
    assert len(n) == len(rows), (len(n), len(rows))
    diff = [{'input': rows[i], 'expected': v, 'actual': n[i]} for i, v in enumerate(e) if v != n[i]]
    report = {
        'cases': len(rows),
        'differences': diff[:30],
        'scope': 'Exact actual System.Xml XmlCharType and ValidateNames UTF16 kernel, not a complete XML parser/DOM/writer.',
    }
    (w / 'xml-character-results.json').write_text(json.dumps(report, indent=2))
    assert not diff, report
    print(f'PASS: {len(rows)} actual System.Xml character/name observations')


if __name__ == '__main__':
    main()
