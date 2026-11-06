"""Unchanged original HGUuidGatherer and base queue methods, controlled service."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(cmd, rows=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-3000:], r.stderr[-3000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/UuidGatherer.cs'
    text = source.read_text(encoding='utf-8-sig')
    sigs = [
        'private static readonly HashSet<UUID> ToSkip',
        'public bool Complete',
        'public UUID? NextUuidToInspect',
        'public UuidGatherer(IAssetService assetService, IDictionary<UUID, sbyte> collector) :',
        'public UuidGatherer(IAssetService assetService, IDictionary<UUID, sbyte> collector, HashSet',
        'public bool AddGathered(',
        'public bool AddForInspection(UUID uuid)',
        'public bool GatherNext(',
        'public bool GatherAll(',
        'private void GetAssetUuids(',
        'protected virtual AssetBase GetAsset(',
    ]
    chunks = [extract(text, s) for s in sigs]
    chunks[0] += ';'
    fields = 'protected IAssetService m_assetService;protected Queue<UUID> m_assetUuidsToInspect;public IDictionary<UUID,sbyte> GatheredUuids{get;private set;}public HashSet<UUID> FailedUUIDs{get;private set;}public HashSet<UUID> UncertainAssetsUUIDs{get;private set;}public int possibleNotAssetCount{get;set;}public int ErrorCount{get;private set;}public int AssetGetCount;private bool verbose=true;'
    hg = text[text.index('public class HGUuidGatherer') :]
    hgmethods = ''.join(
        extract(hg, s)
        for s in [
            'public HGUuidGatherer(IAssetService assetService, string assetServerURL)',
            'public HGUuidGatherer(IAssetService assetService, string assetServerURL, IDictionary',
            'protected override AssetBase GetAsset(',
            'public AssetBase FetchAsset(',
        ]
    )
    generated = w / 'ExtractedHGGatherer.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using System.Text;using OpenMetaverse;partial class UuidGatherer{'
        + fields
        + ''.join(chunks)
        + '}class HGUuidGatherer:UuidGatherer{static Logger m_log=new();protected string m_assetServerURL;'
        + hgmethods
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        NoWarn='0649;0169;0414',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, ROOT / 'native-port/tests/hg_uuid_gatherer_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'HGGathererReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    managed = [a.dotnet, str(w / 'app/HGGathererReference.dll')]
    spaces = (
        [' ', '\t', '\r', '\n', '\v', '\f', '\x85', '\xa0', '\u1680']
        + [chr(n) for n in range(0x2000, 0x200B)]
        + ['\u2028', '\u2029', '\u202f', '\u205f', '\u3000']
    )
    urls = [
        None,
        '',
        'http://assets/',
        'https://user:password@assets:9000/path?x=1#part',
        'not a uri',
        '\u200b',
        '\u180e',
        '\ufeff',
        '\u001c',
        '\x00',
        'é漢😀',
        ' \t\u3000',
    ] + spaces

    def row(
        url,
        op=1,
        level=1,
        mode=0,
        point=0,
        nth=1,
        muturl=0,
        mutlevel=0,
        collector=0,
        target=1,
        depth=3,
    ):
        encoded = '-' if url is None else '.' if url == '' else url.encode().hex()
        return ' '.join(
            map(
                str,
                [encoded, op, level, mode, point, nth, muturl, mutlevel, collector, target, depth],
            )
        )

    rows = [
        row(url, op, level, mode)
        for url, op, level, mode in itertools.product(urls, [0, 1, 2, 4], [-1, 0, 1, 5], range(8))
    ]
    rows += [
        row(url, op, level, mode, point, nth)
        for url, op, level, mode, point, nth in itertools.product(
            [None, ' \t', 'http://assets/'],
            [0, 1, 2, 4],
            [-1, 1],
            [0, 1, 2, 5],
            range(1, 8),
            [1, 2, 3],
        )
    ]
    rows += [
        row(url, op, level, mode, muturl=mu, mutlevel=md, depth=depth)
        for url, op, level, mode, mu, md, depth in itertools.product(
            [None, '\u3000', 'http://assets/'],
            [0, 1, 2, 4],
            [-1, 1],
            [0, 1, 2, 5],
            range(4),
            range(4),
            [1, 3],
        )
    ]
    rows += [
        row(url, op, level, mode, collector=collector)
        for url, op, level, mode, collector in itertools.product(
            [None, ' \t', 'http://assets/'], [1, 2, 4], [0, 1], [0, 1, 2, 5], range(5)
        )
    ]
    rows += [
        row(url, 0, level, mode, target=target)
        for url, level, mode, target in itertools.product(urls, [0, 1], [0, 1, 2], [0, -1])
    ]
    # Sharing is tested with successful collectors; the existing native shared
    # state additionally permits callers to share failed/uncertain sets explicitly.
    rows += [
        row(url, 3, level, collector=collector, depth=depth)
        for url, level, collector, depth in itertools.product(urls, [0, 1], [0, 1, 4], [1, 3])
    ]
    outputs = [invoke(managed, rows), invoke([a.native, '--probe'], rows)]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    differences = [dict(command=c, source=s, native=n) for c, s, n in zip(rows, *outputs) if s != n]
    report = dict(
        passed=not differences,
        cases=len(rows),
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Unchanged HG constructors/GetAsset/FetchAsset and base queue methods. Controlled local/foreign service, inspectors, dynamic HTTP debug field and logs. Null/Unicode-whitespace versus public direct retrieval, exact URL/null/store flag and result, service/logger failures, URL/debug mutations during retrieval, recursive dispatch/cycles, prepopulated/failed/uncertain state, zero/default direct IDs, verbose suppression and successful shared collectors. Native foreign transport/store implementation, arbitrary managed concurrency/GC/invalid UTF16 and complete Hypergrid production integration remain pending.',
        differences=differences[:4],
    )
    (w / 'hg-uuid-gatherer-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:3])
    print(f'PASS: {len(rows)} original HG/base gatherer cases')


if __name__ == '__main__':
    main()
