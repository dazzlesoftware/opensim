"""Original complete Initialise with actual bundled Nini: selected memory-profile timing fields."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs'
    generated = w / 'ExtractedFlotsamInitialise.cs'
    generated.write_text(
        'using System;using System.IO;using Nini.Config;using OpenSim.Framework;partial class FlotsamAssetCache{'
        + extract(source.read_text(encoding='utf-8'), 'public void Initialise(')
        + '}',
        encoding='utf-8',
    )
    files = [
        generated,
        ROOT / 'native-port/tests/flotsam_config_reference.cs',
        ROOT / 'OpenSim/Framework/ExpiringCacheOS.cs',
        ROOT / 'OpenSim/Framework/ExpiringKey.cs',
    ]
    proj = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = ET.SubElement(proj, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        NoWarn='0649;0414',
    ).items():
        ET.SubElement(pg, k).text = v
    group = ET.SubElement(proj, 'ItemGroup')
    for f in files:
        ET.SubElement(group, 'Compile', Include=str(f))
    r = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'FlotsamConfigReference.csproj'
    ET.ElementTree(proj).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'{s} {n}'
        for s, n in itertools.product(
            [
                '.',
                '0',
                '0.016',
                '0.25',
                '-1',
                '5000000',
                '+0.5',
                '-0',
                '1,234.5',
                '1,,2',
                '1e2',
                '1.e2',
                '1e9999',
                '1e-9999',
                'NaN',
                'nan',
                '-NaN',
                'Infinity',
                '+Infinity',
                '-Infinity',
                'inf',
                '1e,2',
                '0x1',
                'x',
            ],
            [
                '.',
                '0',
                '1',
                '120',
                '-1',
                '2147483647',
                '2147484',
                '-2147483648',
                '-2147484',
                '+120',
                '+2147484',
                '1,2',
                '1e2',
                '2147483648',
                '-2147483649',
                'x',
                'NaN',
            ],
        )
    ]
    rows = []
    for command in [
        [a.dotnet, str(w / 'app/FlotsamConfigReference.dll')],
        [a.native, '--config-probe'],
    ]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=100
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    sources = [source, ROOT / 'bin/Nini.dll', *files[2:]]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sources
        },
        scope='Complete unchanged FlotsamAssetCache.Initialise using actual Nini and original expiration classes; controlled console/log and inert AssetBase type. Selected FileCacheEnabled=false memory profile timing projection only. Actual native RegionAssetRuntime configuration/SQLite service/cache exposes exact post-initialise double bits and Int32 fields. Hours-to-seconds memory timeout and seconds-to-milliseconds negative timeout, defaults, signed/plus/grouped/exponent/nonfinite and malformed numeric input and unchecked multiplication overflow verified. Full initializer flags/commands/file lifecycle, configuration culture/INI syntax and other Nini conversions remain pending.',
        differences=diffs[:3],
    )
    (w / 'flotsam-config-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diffs, json.dumps(diffs[:2])
    print(f'PASS: {len(commands)} actual original Initialise/Nini timing profiles')


if __name__ == '__main__':
    main()
