"""Complete original AnimationSet with actual SDK byte and dictionary semantics."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_task_inventory_update import extract

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/AnimationSet.cs'
    util = ROOT / 'OpenSim/Framework/Util.cs'
    generated = w / 'Permissions.cs'
    generated.write_text(
        'namespace OpenSim.Framework{'
        + extract(util.read_text(encoding='utf-8'), 'public enum PermissionMask')
        + '}',
        encoding='utf-8',
    )
    files = [
        source,
        generated,
        ROOT / 'OpenSim/Framework/InventoryNodeBase.cs',
        ROOT / 'OpenSim/Framework/InventoryItemBase.cs',
        ROOT / 'OpenSim/Framework/CustomTypes.cs',
        ROOT / 'native-port/tests/animation_set_reference.cs',
    ]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ig = ET.SubElement(project, 'ItemGroup')
    for f in files:
        ET.SubElement(ig, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(ig, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'AnimationSetReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    modes = set(range(128))
    for extra in [
        128,
        256,
        512,
        1024,
        2048,
        4096,
        8192,
        16384,
        32768,
        65536,
        512 | 32768,
        1024 | 65536,
        131072,
        262144,
        524288,
    ]:
        modes.update(extra | n for n in [0, 1, 2, 4, 8, 16, 32, 64, 12, 24, 40, 56, 127])
    commands = [
        f'{mode} {seed} {failure}'
        for mode, seed, failure in itertools.product(sorted(modes), range(18), range(5))
    ]
    outputs = []
    for cmd in [[a.dotnet, str(w / 'app/AnimationSetReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=90,
        )
        assert r.returncode == 0, r.stderr
        outputs.append(r.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *outputs) if s != n]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [source, util, ROOT / 'bin/OpenMetaverseTypes.dll']
        },
        scope='Entire unchanged original AnimationSet class compiled with full original item classes and actual SDK. Constructor ASCII decoding and all four Console lines/throws, null input, ignored serialized members, unchanged AnimationCount, all getters/setters/null keys/null names/zero deletion, dictionary removed-slot reuse, literal serialization header, UTF16 low bytes including Unicode/NUL, mutable shared ZeroCountBytesReply alias, validation masks/short-circuit/null delegate/exceptions and removal/insertion/update during enumeration. Existing native permission helpers separately retain their original-source coverage. Arbitrary invalid CLR UTF16, OSUTF8Cached allocation/cache lifetime and concurrent callers remain unproven.',
        differences=diffs[:2],
    )
    (w / 'animation-set-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(commands)} original animation set observations')


if __name__ == '__main__':
    main()
