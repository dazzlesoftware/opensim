"""Actual ServerUtils string XML parser, settings, and recursive reader."""

import argparse, json, re, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'OpenSim/Server/Base/ServerUtils.cs').read_text(encoding='utf-8-sig')
    parts = [
        extract(source, r'private static Dictionary<string, object> ScanXmlResponse\('),
        extract(source, r'public static Dictionary<string, object> ParseXmlResponse\(string'),
    ]
    for name in ['ParseXmlStringResponseXmlReaderSettings', 'ParseXmlResponseXmlParserContext']:
        parts.append(
            re.search(r'private static readonly [^;]+ ' + name + r' = [\s\S]+?;', source).group()
        )
    wrapper = w / 'Parser.cs'
    wrapper.write_text(
        'using System;using System.IO;using System.Xml;using System.Collections.Generic;public static partial class Reference{'
        + ''.join(parts)
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [wrapper, ROOT / 'native-port/tests/service_xml_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    path = w / 'ServiceXml.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    contents = [
        '',
        '<a/>',
        '<a></a>',
        '<a>text</a>',
        '<a> \n </a>',
        '<a><![CDATA[ \n ]]></a>',
        '<a>&lt;&amp;&#x1f600;</a>',
        '<a>x<!--c-->y</a>',
        '<a><b/></a>',
        '<a type="List"/>',
        '<a type="List"></a>',
        '<a type="List"><b>1</b><b>2</b><c/></a>',
        '<a type="list"><b/></a>',
        '<a>1</a><a>2</a>',
        '<a type="List"><b type="List"/><c>3</c></a>',
        '<a xml:space="preserve"> \n </a>',
        '<a>1\r\n2\r3</a>',
        'text<a/>tail',
        '<!--c--><a/><!---->',
        '<a type="List" type="List"/>',
        '<_x0041_>1</_x0041_>',
        '<_x0001F600_>emoji</_x0001F600_>',
        '<a>&#0;</a>',
        '<a>\0</a>',
    ]
    profiles = []
    for body in contents:
        for start, end in [
            ('<ServerResponse>', '</ServerResponse>'),
            ('<ServerResponse/>', ''),
            ('<ServerResponse></ServerResponse>', ''),
            ('<outer><ServerResponse>', '</ServerResponse></outer>'),
        ]:
            profiles.append(start + body + end)
    profiles += [
        '',
        'junk',
        '<other/>',
        '<?xml version="1.0"?><ServerResponse><Result>True</Result></ServerResponse>',
        '<ServerResponse><a>ok</a></ServerResponse><broken>',
        '<!DOCTYPE ServerResponse><ServerResponse><a/></ServerResponse>',
        '<ServerResponse><a/></ServerResponse><!DOCTYPE other>',
        '<ServerResponse><a/></ServerResponse>tail',
        '<ServerResponse><a></ServerResponse>',
    ]
    for name in [
        'a',
        'A',
        '_x0041_',
        '_X0041_',
        '_x0001F600_',
        '_xFFFFFFFF_',
        '試験',
        '😀',
        '1name',
        'a:b',
        'a:b:c',
        'a.b',
        'a-b',
        'xml:name',
    ]:
        profiles += [
            f'<ServerResponse><{name}>value</{name}></ServerResponse>',
            f'<ServerResponse xmlns:a="urn:test"><{name}>value</{name}></ServerResponse>',
        ]
    profiles += [
        '<ServerResponse><a x:y="1">v</a></ServerResponse>',
        '<ServerResponse><a>&unknown;</a></ServerResponse>',
        '<ServerResponse><a><![CDATA[&#0;]]></a></ServerResponse>',
        '<ServerResponse xmlns="urn:test"><a>v</a></ServerResponse>',
        '<ServerResponse xmlns:a="urn:test"><a:x>v</a:x></ServerResponse>',
        '<ServerResponse xmlns:a=""><a:x>v</a:x></ServerResponse>',
        '<ServerResponse><a xml:space="invalid">v</a></ServerResponse>',
        '<ServerResponse><a><![CDATA[]]></a><b/></ServerResponse>',
    ]
    for body in [
        '<a>&#32;</a>',
        '<a>&#x20;</a>',
        '<a>&#9;&#10;</a>',
        '<a> &#32; </a>',
        '<a xml:space="default"> &#32; </a>',
        '<a>]]&gt;</a>',
        '<a>]]></a>',
        '<_x00110000_>v</_x00110000_>',
        '<_x0000D800_>v</_x0000D800_>',
    ]:
        profiles.append('<ServerResponse>' + body + '</ServerResponse>')
    rows = []
    for command in [[a.dotnet, str(w / 'app/ServiceXml.dll')], [a.native]]:
        r = subprocess.run(
            command,
            input='\n'.join(x.encode().hex() for x in profiles) + '\n',
            capture_output=True,
            text=True,
            timeout=45,
        )
        if r.returncode:
            raise RuntimeError(r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(profiles)
    differences = [
        {'input': c, 'source': x, 'native': y} for c, x, y in zip(profiles, *rows) if x != y
    ]
    (w / 'service-xml-parity-results.json').write_text(
        json.dumps(
            {'passed': not differences, 'observations': len(profiles), 'differences': differences},
            indent=2,
        ),
        encoding='utf-8',
    )
    if differences:
        raise AssertionError(str(differences[:12]))
    print(f'PASS: {len(profiles)} service XML source observations')


if __name__ == '__main__':
    main()
