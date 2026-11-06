import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(cmd, rows=None):
    p = subprocess.run(
        cmd,
        input=None if rows is None else "\n".join(rows) + "\n",
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=120,
    )
    assert p.returncode == 0, (p.returncode, p.stderr)
    return p.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    g = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        RuntimeFrameworkVersion='8.0.31',
        RollForward='Disable',
    ).items():
        ET.SubElement(g, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(ROOT / 'native-port/tests/config_xml_stream_metadata_reference.cs'),
    )
    cs = w / 'Reference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    run([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '-v', 'quiet'])
    rows = []
    for size, read in itertools.product(
        list(range(258)) + [511, 512, 81919, 81920, 81921], [0, 1, 2, 3, 17, 255, 256, 257, 1024]
    ):
        rows.append(f'{size} S L P R{read} S L P R0 P R{read} P C S L P R0 R1 C S')
    rng = random.Random(8157)
    for _ in range(10000):
        count = rng.randrange(1025)
        commands = rng.choices(
            ['S', 'L', 'P', 'C', 'R0', 'R1', 'R2', 'R17', 'R1024'], k=rng.randrange(1, 50)
        )
        rows.append(str(count) + ' ' + ' '.join(commands))
    source = run([a.dotnet, str(w / 'app/Reference.dll')], rows)
    native = run([a.native, '--probe'], rows)
    assert len(source) == len(native) == len(rows)
    diff = [dict(row=r, source=s, native=n) for r, s, n in zip(rows, source, native) if s != n]
    for name, data in [('inputs.txt', rows), ('actual-bcl.txt', source), ('native.txt', native)]:
        (w / name).write_text('\n'.join(data) + '\n', encoding='utf-8')
    (w / 'results.json').write_text(
        json.dumps(
            dict(
                cases=len(rows),
                passed=not diff,
                differences=diff[:30],
                hashes={
                    name: hashlib.sha256((w / name).read_bytes()).hexdigest()
                    for name in ['inputs.txt', 'actual-bcl.txt', 'native.txt']
                },
            ),
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    print(len(rows), len(diff))
    print(diff[:2])
    assert not diff


if __name__ == '__main__':
    main()
