"""Original RequestInventoryFile/cache/InventoryStringBuilder with actual SDK strings/type tables."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, signature):
    start = s.index(signature)
    brace = s.index('{', start)
    i = brace + 1
    depth = 1
    while depth:
        if s.startswith('//', i):
            i = s.index('\n', i) + 1
            continue
        if s.startswith('/*', i):
            i = s.index('*/', i) + 2
            continue
        if s[i] in ['"', "'"]:
            quote = s[i]
            i += 1
            while i < len(s):
                if s[i] == '\\':
                    i += 2
                    continue
                if s[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/SceneObjectPartInventory.cs'
    s = source.read_text()
    method = extract(s, 'public void RequestInventoryFile(').replace('UUID.Random()', 'NextUuid()')
    builder = extract(s, 'public class InventoryStringBuilder')
    generated = w / 'ExtractedTaskInventoryFile.cs'
    generated.write_text(
        'using System;using System.Text;using OpenMetaverse;\npartial class TaskFileReference{\n'
        + method
        + '\n'
        + builder
        + '\n}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/task_inventory_file_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'TaskInventoryFileReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [f'{t} 10 49152 0' for t in list(range(58)) + [-1, 58, 127, 128, 257, -129]] + [
        f'10 {t} 49152 0' for t in list(range(29)) + [-1, 127, 128, 257, -129]
    ]
    commands += [
        f'{asset} {inv} {mask} {scenario}'
        for asset, inv, mask, scenario in itertools.product(
            [0, 10, 56, 57],
            [0, 7, 10, 25, 26],
            [0, 16384, 32768, 49152, 4294967295],
            [0, 1, 2, 3, 4, 8, 16, 32, 64, 128],
        )
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/TaskInventoryFileReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [
        dict(command=c, source=s[:1500], native=n[:1500])
        for c, s, n in zip(commands, *rows)
        if s != n
    ]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        requests=len(commands) * 11,
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, ROOT / 'bin/OpenMetaverse.dll', ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Complete RequestInventoryFile and complete nested InventoryStringBuilder; only UUID.Random dependency injected. Actual SDK UTF8-NUL/filename/type tables/UIntToHexString; controlled parts/permissions/task-list/Xfer/client callbacks. Eleven-step cache traces cover 0/1/3 privileges, serial0/65535/65536, empty items, reuse, generation errors with cached fallback, Unicode/NUL, group owner, SDK type bounds, callback failures and ignored AddNewFile return. Source list locking/concurrent item mutation, task item creation registry, raw ReplyTaskInventory and Xfer delivery remain separate.',
        differences=diffs[:2],
    )
    (w / 'task-inventory-file-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(
        f'PASS: {len(commands)*11} original task inventory file/cache/permission/callback observations'
    )


if __name__ == '__main__':
    main()
