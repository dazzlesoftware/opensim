import argparse, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    return '~' if s is None else s.encode('utf-16-be', 'surrogatepass').hex() or '-'


def run(cmd, rows=None):
    p = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        text=True,
        capture_output=True,
        encoding='utf-8',
        timeout=120,
    )
    assert p.returncode == 0, (p.returncode, p.stderr)
    return p.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--work', required=True, type=Path)
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
        Include=str(ROOT / 'native-port/tests/config_uri_object_overloads_reference.cs'),
    )
    cs = w / 'Reference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    run(['dotnet', 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'])
    values = [
        None,
        '',
        'relative',
        'http://x/',
        'HTTP://X:80/a%20b#f',
        'https://user:p@x:443/a/../b?q#f',
        'http://x/é#é%20',
        'http://:pass@x/a#base',
    ]
    rows = [m + ' ' + enc(v) for m, v in itertools.product(['N', 'S'], values)]
    source = run(['dotnet', str(w / 'app/Reference.dll')], rows)
    native = run([a.native, '--probe'], rows)
    assert len(rows) == len(source) == len(native)
    diff = [dict(row=r, source=s, native=n) for r, s, n in zip(rows, source, native) if s != n]
    (w / 'results.json').write_text(
        json.dumps(dict(cases=len(rows), passed=not diff, differences=diff), indent=2) + '\n',
        encoding='utf-8',
    )
    print(len(rows), len(diff))
    print(diff[:3])
    assert not diff


if __name__ == '__main__':
    main()
