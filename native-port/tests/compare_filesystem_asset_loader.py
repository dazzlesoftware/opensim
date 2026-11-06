"""Original filesystem asset loader with actual Nini, SDK and real filesystem IO."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def token(value):
    return 'N' if value is None else value.encode().hex() if value else '.'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    original = ROOT / 'OpenSim/Framework/AssetLoader/Filesystem/AssetLoaderFileSystem.cs'
    text = original.read_text(encoding='utf-8-sig').replace('UUID.Random()', 'LoaderHost.Random()')
    (w / 'Loader.cs').write_text(text, encoding='utf-8')
    s = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        s[: s.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'AllowUnsafeBlocks': 'true',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        w / 'Loader.cs',
        w / 'SLUtil.cs',
        ROOT / 'OpenSim/Framework/AssetBase.cs',
        ROOT / 'OpenSim/Framework/NrbfReader.cs',
        ROOT / 'OpenSim/Framework/IAssetLoader.cs',
        ROOT / 'native-port/tests/filesystem_asset_loader_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'Nini']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'FilesystemLoader.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    (w / 'data.bin').write_bytes(bytes(range(256)))
    (w / 'empty.bin').write_bytes(b'')
    (w / '世界😀.bin').write_bytes(b'asset')
    (w / 'directory').mkdir(exist_ok=True)

    def config(rows):
        root = ET.Element('Nini')
        for i, fields in enumerate(rows):
            section = ET.SubElement(root, 'Section', Name=str(i))
            for name, value in fields.items():
                ET.SubElement(section, 'Key', Name=name, Value=value)
        return ET.tostring(root, encoding='unicode')

    specs = [
        {},
        {
            'assetID': '00000000-0000-0000-0000-000000000008',
            'name': 'Asset',
            'fileName': 'data.bin',
            'assetType': '49',
        },
        {'name': '世界😀', 'fileName': '世界😀.bin', 'assetType': '-129'},
        {'fileName': 'empty.bin'},
        {'fileName': 'missing.bin'},
        {'fileName': 'directory'},
        {'assetID': 'bad'},
        {'assetType': 'bad'},
        {'assetType': '2147483648'},
        {'assetType': '256'},
        {'assetType': ' +7 '},
        {'assetType': '7\0'},
        {'fileName': str(w / 'data.bin')},
        {'fileName': '../' + w.name + '/data.bin'},
    ]
    xmls = [config([row]) for row in specs] + [
        config([specs[1], specs[0], specs[2]]),
        '<Nini><Section Name="x"><Key Name="name" Value="first"/></Section><Section Name="x"><Key Name="name" Value="second"/></Section></Nini>',
        '<Nini><Section Name="x"><Key Name="name" Value="first"/><Key Name="name" Value="second"/></Section></Nini>',
        '<Nini><Section/></Nini>',
        '<Nini><Section Name="x"><Key Name="name"/></Section></Nini>',
        '<Other/>',
        '<Nini><Section>',
        '<Nini/><Nini/>',
        '',
        '<Nini><Other/><Section Name="x"><other/></Section></Nini>',
    ]
    controls = []
    for i, xml in enumerate(xmls):
        (w / f'set-{i}.xml').write_text(xml, encoding='utf-8')
        name = f'control-{i}.xml'
        (w / name).write_text(config([{'file': f'set-{i}.xml'}]), encoding='utf-8')
        controls.append(name)
    (w / 'multiple.xml').write_text(
        config(
            [
                {'file': 'set-1.xml'},
                {'file': 'set-20.xml'},
                {'file': 'missing.xml'},
                {'file': 'set-0.xml'},
            ]
        ),
        encoding='utf-8',
    )
    (w / 'no-file.xml').write_text(config([{}]), encoding='utf-8')
    (w / 'broken.xml').write_text('<Nini>', encoding='utf-8')
    (w / 'empty.xml').write_text('<Nini/>', encoding='utf-8')
    paths = controls + [
        'multiple.xml',
        'no-file.xml',
        'broken.xml',
        'empty.xml',
        'missing.xml',
        'directory',
        '',
        None,
        str(ROOT / 'bin/assets/AssetSets.xml'),
    ]
    cases = [f'{token(path)} {mode} -' for path, mode in itertools.product(paths, range(4))]
    cases += [
        f'{token(controls[0])} 0 {token(failure)}'
        for failure in ['random', 'info:[ASSETS]: Instantiated: []']
    ]
    rows = []
    for command in [
        [a.dotnet, str(w / 'app/FilesystemLoader.dll')],
        [str(Path(a.native).resolve())],
    ]:
        r = subprocess.run(
            command,
            input='\n'.join(cases) + '\n',
            cwd=w,
            capture_output=True,
            text=True,
            timeout=180,
        )
        assert r.returncode == 0, r.stderr[-2000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    shipped_case = f'{token(str(ROOT/"bin/assets/AssetSets.xml"))} 0 -'
    shipped = rows[0][cases.index(shipped_case)].split('|')
    assert shipped[0] == 'OK'
    shipped_count = len([a for a in shipped[1].split(';') if a])
    assert shipped_count > 0
    differences = [
        {'case': c, 'source': s[:3000], 'native': n[:3000]}
        for c, s, n in zip(cases, *rows)
        if s != n
    ]
    (w / 'filesystem-asset-loader-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'shippedAssetsMatched': shipped_count,
                'sourceSha256': hashlib.sha256(original.read_bytes()).hexdigest(),
                'scope': 'Whole original loader with only UUID.Random projected to controlled RNG; actual Nini, SDK and filesystem IO, shipped asset control/sets/data, callbacks/reentry/null and malformed XML/config cases. XML exception message text normalized. General Nini XML/DTD/encoding/path/IO-race/GC/platform semantics remain pending.',
                'differences': differences[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:1]
    print(f'PASS: {len(cases)} filesystem asset loader source observations')


if __name__ == '__main__':
    main()
