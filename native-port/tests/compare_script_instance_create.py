"""Unchanged original CreateScriptInstance flow with real SDK script byte decoding."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, sig):
    start = s.index(sig)
    i = s.index('{', start) + 1
    d = 1
    while d:
        if s[i] == '{':
            d += 1
        elif s[i] == '}':
            d -= 1
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
    model = ROOT / 'OpenSim/Framework/TaskInventoryItem.cs'
    gen = w / 'ExtractedInstanceCreate.cs'
    gen.write_text(
        'using System;using System.Collections;using OpenMetaverse;using OpenSim.Framework;\npartial class Registry{'
        + extract(
            source.read_text(),
            'public bool CreateScriptInstance(TaskInventoryItem item, int startParam, bool postOnRez, string engine, int stateSource)',
        )
        + extract(source.read_text(), 'private void StoreScriptError(')
        + '}\n'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in [gen, model, ROOT / 'native-port/tests/script_instance_create_reference.cs']:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'InstanceCreateReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = list(range(256)) + [
        256,
        256 | 64,
        512,
        512 | 1,
        512 | 2,
        1024,
        1024 | 64,
        1024 | 16,
        2048,
        8192,
        8192 | 128,
        16384,
        16384 | 4,
        16384 | 64,
    ]
    commands = [' '.join(map(str, c)) for c in itertools.product(modes, [0, 1, 2, 3], range(15))]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/InstanceCreateReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    files = [source, model, ROOT / 'bin/OpenMetaverse.dll', ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete unchanged original SceneObjectPartInventory.CreateScriptInstance(TaskInventoryItem,...) and StoreScriptError, original full task model and actual SDK Utils.BytesToString. Controlled permissions/config/registry/asset/compiler/error collection/global counter/full update and saved XML restoration boundary. Null input/stored/asset data, missing membership/assets, identical vs distinct live items, trusted binary/source types, permission clearing/restored OldItemIDs, stopped/failed-compile count, invalid/trailing-NUL decoding and all dependency exceptions. Real CanRunScript, saved XML restoration and engine compilation must be provided by host.',
        differences=diffs[:1],
    )
    (w / 'script-instance-create-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original script instance create observations')


if __name__ == '__main__':
    main()
