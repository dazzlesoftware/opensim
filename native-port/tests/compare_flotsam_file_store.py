"""Execute original path, queued writes, read/error boundaries and actual file interoperability."""

import argparse, hashlib, itertools, json, subprocess, tempfile, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def call(cmd, lines):
    r = subprocess.run(
        cmd,
        input='\n'.join(lines) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, r.stderr
    rows = r.stdout.splitlines()
    assert len(rows) == len(lines), (len(rows), len(lines), r.stdout, r.stderr)
    return rows


def build(a, w, real):
    folder = w / ('real' if real else 'control')
    folder.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs'
    text = source.read_text(encoding='utf-8')
    generated = folder / 'ExtractedFileStore.cs'
    prefix = 'using System;using System.IO;using System.Text;using System.Threading;\nusing Path=HarnessPath;\n'
    prefix += '#if REAL_FILES\nusing File=System.IO.File;using Directory=System.IO.Directory;using FileStream=System.IO.FileStream;using BinaryFormatter=System.Runtime.Serialization.Formatters.Binary.BinaryFormatter;using AssetBase=OpenSim.Framework.AssetBase;\n#else\nusing File=FakeFile;using Directory=FakeDirectory;using FileStream=FakeStream;using BinaryFormatter=FakeFormatter;\n#endif\n'
    signatures = [
        'private string GetFileName(',
        'private void UpdateFileCache(',
        'private void ProcessWrites(',
        'private static void WriteFileCache(',
        'private AssetBase GetFromFileCache(',
        'private bool CheckFromFileCache(',
    ]
    generated.write_text(
        prefix
        + 'partial class FlotsamAssetCache{'
        + ''.join(extract(text, s) for s in signatures)
        + '}',
        encoding='utf-8',
    )
    proj = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = ET.SubElement(proj, 'PropertyGroup')
    props = dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        StartupObject='FileProgram',
        NoWarn='0649;SYSLIB0011',
    )
    if real:
        props.update(
            AllowUnsafeBlocks='true',
            EnableUnsafeBinaryFormatterSerialization='true',
            DefineConstants='REAL_FILES',
            AssemblyName='OpenSim.Framework',
            AssemblyVersion='0.9.3.1',
        )
    for k, v in props.items():
        ET.SubElement(pg, k).text = v
    group = ET.SubElement(proj, 'ItemGroup')
    files = [generated, ROOT / 'native-port/tests/flotsam_file_store_reference.cs']
    if real:
        slutil = ROOT / 'OpenSim/Framework/SLUtil.cs'
        s = slutil.read_text(encoding='utf-8-sig')
        conversion = folder / 'SLUtil.cs'
        conversion.write_text(
            s[: s.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
        )
        files += [
            ROOT / 'OpenSim/Framework/AssetBase.cs',
            ROOT / 'OpenSim/Framework/NrbfReader.cs',
            ROOT / 'native-port/tests/asset_nrbf_reference.cs',
            conversion,
        ]
        ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    for f in files:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = folder / 'Reference.csproj'
    ET.ElementTree(proj).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(folder / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    return [a.dotnet, str(folder / 'app' / ('OpenSim.Framework.dll' if real else 'Reference.dll'))]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    managed = build(a, w, False)
    native = [a.native, '--probe']
    commands = [f'run 0 {m} 0 0 0' for m in range(512)]
    commands += [
        f'run 0 {m} {point} {kind} 0'
        for m, point, kind in itertools.product(
            [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 15, 16, 18, 23, 31, 32, 64, 128, 256, 258, 272],
            range(1, 18),
            range(6),
        )
    ]
    commands += [
        f'run 1 {m} {point} {kind} 0'
        for m, point, kind in itertools.product([0, 1, 64, 512, 1024, 1536], range(18), range(6))
    ]
    commands += [
        f'run 2 {m} {point} {kind} {short}'
        for m, point, kind, short in itertools.product([0, 1, 64], [0, 1], range(6), range(2))
    ]
    commands += [f'run 3 {m} 0 0 0' for m in [0, 32, 64, 128]] + [
        f'run {op} {mode} 0 0 1' for op, mode in itertools.product(range(4), [0, 32])
    ]
    ids = [
        '',
        'x',
        'abcdef',
        'abcdefghijklmnop',
        'a/b\\c:d?e*f|g<h>i"j',
        'a\x00b\x01cdefghijkl',
        '..abcdefghijklmnop',
        '😀😀😀😀😀😀',
        'a😀b𐐷cdefghijkl',
        'é漢字abcdefghijklmnop',
        'CONabcdefghijklmnop',
        '  abcdefghijklmnop',
    ]
    commands += [
        f'path {tiers} {length} ' + s.encode().hex()
        for tiers, length, s in itertools.product(
            [-2, 0, 1, 2, 3, 4, 2147483647], [-1, 0, 1, 2, 3, 4, 5, 2147483647], ids
        )
    ]
    rows = [call(managed, commands), call(native, commands)]
    diffs = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(commands, *rows))
        if s != n
    ]
    assert not diffs, json.dumps(diffs[:3])
    real = build(a, w, True)
    nio = [a.native, '--interop']
    observations = []
    # Keep actual interoperability files as evidence, but never reuse a previous
    # run's final replacement as the next run's initial keep-existing fixture.
    files = w / 'files'
    files.mkdir(exist_ok=True)
    run_files = Path(tempfile.mkdtemp(prefix='run-', dir=files))
    for profile in [0, 1, 2, 6, 8, 10, 25, 36, 512, 777, 1023]:
        key = 'abcdef12-3456-4789-8abc-' + str(profile).zfill(12)
        idhex = key.encode().hex()
        dirs = [run_files / ('managed-' + str(profile)), run_files / ('native-' + str(profile))]
        for d in dirs:
            d.mkdir(parents=True, exist_ok=True)
        mh, nh = [str(d).encode().hex() for d in dirs]
        blob = call(real, [f'emit {profile}'])[0]
        expected = call(real, ['decode ' + blob])[0]
        assert call(real, [f'put {mh} {idhex} {profile} 0']) == ['OK']
        read = call(nio, [f'get {mh} {idhex}'])[0]
        assert call(real, ['decode ' + read]) == [expected]
        assert call(nio, [f'put {nh} {idhex} {blob} 0']) == ['OK']
        assert call(real, [f'get {nh} {idhex}']) == [expected]
        variant = (profile + 1) % 1024
        next_blob = call(real, [f'emit {variant}'])[0]
        assert call(nio, [f'put {nh} {idhex} {next_blob} 0']) == ['OK']
        assert call(real, [f'get {nh} {idhex}']) == [expected]
        assert call(real, [f'put {mh} {idhex} {variant} 0']) == ['OK']
        read = call(nio, [f'get {mh} {idhex}'])[0]
        assert call(real, ['decode ' + read]) == [expected]
        assert call(nio, [f'put {nh} {idhex} {next_blob} 1']) == ['OK']
        assert call(real, [f'get {nh} {idhex}']) == call(real, ['decode ' + next_blob])
        assert call(real, [f'put {mh} {idhex} {variant} 1']) == ['OK']
        read = call(nio, [f'get {mh} {idhex}'])[0]
        assert call(real, ['decode ' + read]) == call(real, ['decode ' + next_blob])
        observations.append(dict(profile=profile, passed=True))
    sources = [
        ROOT / 'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs',
        ROOT / 'OpenSim/Framework/AssetBase.cs',
        ROOT / 'OpenSim/Framework/NrbfReader.cs',
        ROOT / 'bin/OpenMetaverseTypes.dll',
    ]
    report = dict(
        passed=True,
        cases=len(commands),
        actualFileProfiles=len(observations),
        fileRunDirectory=str(run_files),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources
        },
        scope='Complete unchanged GetFileName/UpdateFileCache/ProcessWrites/WriteFileCache/GetFromFileCache/CheckFromFileCache. Controlled queue/filesystem/formatter/log/access tracker for all write/read failure categories, late disposal outcomes/counters, duplicate/enqueue-loss state, existing/replace/race behavior, UTF16 tier/sanitization paths. Actual original methods plus real BinaryFormatter/filesystem exchange native cache files both directions and preserve keep/replace rules. Native actual executor and OS file I/O separately exercised. Full module/cache composition/access-time tracker/scanning/cleanup/commands, all malformed BinaryFormatter binding, permissions/filesystem edge cases, concurrent schedules and Linux/macOS execution remain pending.',
        fileObservations=observations,
    )
    (w / 'flotsam-file-store-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    print(
        f'PASS: {len(commands)} original file transaction/path traces and {len(observations)} actual bidirectional file profiles'
    )


if __name__ == '__main__':
    main()
