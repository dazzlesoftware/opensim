"""Whole original GetAssetsHandler, actual asset/HTTP response classes and SDK."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def token(value):
    return 'N' if value is None else value.encode().hex() if value else '.'


def method(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    conversions = (ROOT / 'OpenSim/Framework/SLUtil.cs').read_text(encoding='utf-8-sig')
    (w / 'SLUtil.cs').write_text(
        conversions[: conversions.index('static readonly char[] seps')] + '\n}}\n', encoding='utf-8'
    )
    util = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    (w / 'Util.cs').write_text(
        'using System;namespace OpenSim.Framework{public static class Util{'
        + method(util, 'public static bool TryParseHttpRange(')
        + '}}',
        encoding='utf-8',
    )
    originals = [
        ROOT / 'OpenSim/Capabilities/Handlers/GetAssets/GetAssetsHandler.cs',
        ROOT / 'OpenSim/Framework/AssetBase.cs',
        ROOT / 'OpenSim/Framework/NrbfReader.cs',
        ROOT / 'OpenSim/Services/Interfaces/IAssetService.cs',
    ] + [
        ROOT / 'OpenSim/Framework/Servers/HttpServer/OSHttpServer' / f
        for f in [
            'HttpResponse.cs',
            'RequestCookie.cs',
            'ResponseCookie.cs',
            'ResponseCookies.cs',
            'IHttpResponse.cs',
        ]
    ]
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
        'AllowUnsafeBlocks': 'true',
        'StartupObject': 'GetAssetsReference',
    }.items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, 'ItemGroup')
    for path in originals + [
        w / 'SLUtil.cs',
        w / 'Util.cs',
        ROOT / 'native-port/tests/get_assets_reference.cs',
        ROOT / 'native-port/tests/http_response_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(path))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'GetAssets.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    cases = []
    uuid = '01234567-89ab-cdef-0123-456789abcdef'
    keys = [
        'texture_id',
        'sound_id',
        'callcard_id',
        'landmark_id',
        'script_id',
        'clothing_id',
        'object_id',
        'notecard_id',
        'lsltext_id',
        'lslbyte_id',
        'txtr_tga_id',
        'bodypart_id',
        'snd_wav_id',
        'img_tga_id',
        'jpeg_id',
        'animatn_id',
        'gesture_id',
        'mesh_id',
        'settings_id',
        'material_id',
    ]

    def add(
        query='/asset?texture_id=' + uuid,
        range=None,
        asset_mode=0,
        type=0,
        size=8,
        callback=0,
        start=-1,
        length=-1,
        priority=0,
        foreign=None,
        mime=None,
        flags=0,
    ):
        cases.append(
            [
                token(query),
                token(range),
                str(asset_mode),
                str(type),
                str(size),
                str(callback),
                str(start),
                str(length),
                str(priority),
                token(foreign),
                token(mime),
                str(flags),
            ]
        )

    for key, type, size in itertools.product(keys, [-1, 0, 1, 49, 57], [0, 1, 8]):
        add(query='/asset?' + key + '=' + uuid, type=type, size=size)
    for header, start, priority in itertools.product(
        [
            None,
            '',
            'bytes=0-0',
            'bytes=-2',
            'bytes=1-',
            'bytes=2-5',
            'bytes=5-2',
            'bytes=99-',
            'bytes=1-2147483647',
            'bytes=+1-+3',
            'bytes=1-0',
            'bytes=1-x',
            'bytes=1-2-3',
            'bytes=\u00a02\u00a0-\u20035\u2003',
            'bytes\u00ad=1-3',
        ],
        [-1, 0, 4],
        [0, 2, 3, 9],
    ):
        add(range=header, start=start, priority=priority)
    for asset_mode, callback, size, flags in itertools.product(
        range(4), range(7), [0, 1, 8], [0, 16]
    ):
        add(asset_mode=asset_mode, callback=callback, size=size, flags=flags)
    for query in [
        None,
        '/asset',
        '/asset?',
        '/asset?unknown=x',
        '/asset?Texture_id=' + uuid,
        '/asset?texture_id=bad&mesh_id=' + uuid,
        '/asset?texture_id=&mesh_id=' + uuid,
        '/asset?texture_id=' + uuid + '&TEXTURE_ID=' + uuid,
        '/asset?unknown=x&mesh_id=' + uuid,
    ]:
        for flags in [0, 1, 2, 4, 8, 32]:
            add(query=query, flags=flags)
    for value, foreign, mime in itertools.product(
        [
            uuid,
            uuid.upper(),
            '{' + uuid + '}',
            uuid.replace('-', ''),
            ' ' + uuid + ' ',
            uuid + 'x',
            'bad',
            '00000000-0000-0000-0000-000000000000',
        ],
        [None, '', 'https://foreign/?a=1&b=2'],
        [None, '', 'image/custom'],
    ):
        add(query='/asset?texture_id=' + value, foreign=foreign, mime=mime)
    for flags, type, size in itertools.product([64, 128, 256, 1024], [0, 1, 49], [0, 1, 8]):
        add(flags=flags, type=type, size=size)
    commands = '\n'.join(' '.join(c) for c in cases) + '\n'
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/GetAssets.dll')], [a.native]]:
        r = subprocess.run(cmd, input=commands, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases), (len(cases), list(map(len, rows)))
    differences = [{'case': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n]
    assert not any('FIXTURE_EX' in row for row in rows[0]), [
        (c, s) for c, s in zip(cases, rows[0]) if s == 'FIXTURE_EX'
    ][:2]
    (w / 'get-assets-results.json').write_text(
        json.dumps(
            {
                'passed': not differences,
                'observations': len(cases),
                'sources': {
                    str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in originals
                },
                'scope': 'Entire unchanged original asset handler with actual AssetBase, HttpResponse/cookies, IAssetService and SDK; original range method body. Controlled request facade, service callbacks and logger. Null/error/duplicate/late/worker callback outcomes, stale response fields, all query types. Arbitrary concurrency, no-callback indefinite waits, GC/runtime, exception types/stacks, invalid UTF16 and platforms remain pending.',
                'differences': differences[:8],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not differences, differences[:2]
    print(f'PASS: {len(cases)} asset handler source observations')


if __name__ == '__main__':
    main()
