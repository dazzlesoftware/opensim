"""Original chat API method bodies with injected clock/world/console/sleep interfaces."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        if source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs'
    s = source.read_text()
    methods = [
        'private void CheckSayShoutTime()',
        'public void llSay(int channelID, string text)',
        'public void llOwnerSay(string msg)',
        'protected virtual void ScriptSleep(int delay)',
    ]
    body = (
        '\n'.join(extract(s, m) for m in methods)
        .replace('DateTime.UtcNow', 'Clock')
        .replace('Console.WriteLine(text)', 'TestConsole.WriteLine(text)')
    )
    generated = w / 'ExtractedLslApi.cs'
    generated.write_text(
        'using System;using OpenMetaverse;\npartial class Api {\n' + body + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/lsl_runtime_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'LslRuntimeReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    texts = [
        '',
        'hello',
        'quote" slash\\\nnext\tline',
        '雪é🌍',
        'a' * 1022 + '雪z',
        'a' * 1023 + 'x',
        '雪' * 400,
        'a\0b',
        'end\0',
    ]
    commands = [
        f'{mode} {count} {channel} {text.encode().hex() or "-"}'
        for mode in range(16)
        for count in [1, 12, 32]
        for channel in [0, 7, -2147483648]
        for text in texts
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/LslRuntimeReference.dll')], [a.native, '--api-probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (len(rows[0]), len(rows[1]))
    diff = [
        dict(command=c, source=s[:150], native=n[:150])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    files = [source, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diff,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Extracted actual CheckSayShoutTime/llSay/llOwnerSay/ScriptSleep bodies; only clock and console injection edits; actual SDK UTF8 encoder. Controlled world/listener/sleep interfaces, native source compiler plus scheduler boundary per call. Exact output bytes/order/delay trace. Parser/AST/whole YEngine frontend equivalence, full scheduler, scene chat routing and Unix execution not claimed.',
        differences=diff[:3],
    )
    (w / 'lsl-runtime-api-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diff, json.dumps(diff[:1], indent=2)
    print(f'PASS: {report["cases"]} original LSL chat/throttle/UTF8 API traces')


if __name__ == '__main__':
    main()
