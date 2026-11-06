"""Work-in-progress actual BCL synchronous XML reader differential; never a subset completion claim."""

import argparse, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path


def enc(s):
    return s.encode('utf-16-be', 'surrogatepass').hex() or '-'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--navigation', action='store_true')
    p.add_argument('--resolve', action='store_true')
    p.add_argument('--normalize', action='store_true')
    p.add_argument('--doc-limit', type=int, default=0)
    p.add_argument('--entity-limit', type=int, default=-1)
    p.add_argument('--state', action='store_true')
    p.add_argument('--summary', action='store_true')
    p.add_argument('--deep', action='store_true')
    p.add_argument('--start-attribute', action='store_true')
    p.add_argument('--prohibit', action='store_true')
    p.add_argument('--stream', action='store_true')
    p.add_argument('--chunk', type=int, default=3)
    p.add_argument('--nonseek', action='store_true')
    p.add_argument('--close-stream', action='store_true')
    p.add_argument('--load', action='store_true')
    p.add_argument('--bytes', action='store_true')
    p.add_argument('--legacy', action='store_true')
    p.add_argument('--standalone', action='store_true')
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'OutputType': 'Exe',
        'TargetFramework': 'net8.0',
        'EnableDefaultCompileItems': 'false',
        'Nullable': 'enable',
        'RuntimeFrameworkVersion': '8.0.31',
        'RollForward': 'Disable',
    }.items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(Path(__file__).with_name('nini_xml_reader_reference.cs').resolve()),
    )
    file = w / 'Reference.csproj'
    ET.ElementTree(project).write(file)
    r = subprocess.run(
        [a.dotnet, 'build', str(file), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    cases = [
        '',
        '<Nini/>',
        '<Nini></Nini>',
        '<Nini><Section Name="s"><Key Name="k" Value="v"/></Section></Nini>',
        ' \r\n<Nini/>\n',
        '<a>text &lt; &gt; &amp; &apos; &quot; &#65; &#x1f600;</a>',
        '<a><![CDATA[x\r\ny]]><!--c--><?pi data?></a>',
        '<?xml version="1.0"?><a/>',
        '<a xmlns="urn:x" xmlns:p="urn:p" p:x="v"><p:b/></a>',
        '<a xml:space="preserve"> \n <b/> </a>',
        '<!DOCTYPE a [<!ELEMENT a ANY>]><a/>',
        '<!DOCTYPE a SYSTEM "missing"><a/>',
        '<a>\ud800</a>',
        '<a>\0</a>',
        '<a>',
        '<a></b>',
        '<a/><b/>',
        'text<a/>',
        '<a x="1" x="2"/>',
        '<p:a/>',
        '<a>&unknown;</a>',
        '<a>&#0;</a>',
        '<a>&#xZZ;</a>',
        '<a x="<"/>',
        '<!--x--x--><a/>',
        '<![CDATA[x]]>',
        '<a>]]></a>',
        '<?xml version="1.1"?><a/>',
        '<?xml?><a/>',
        '<a/>trailing',
        '<a xmlns:p="urn:x" xmlns:q="urn:x" p:x="1" q:x="2"/>',
    ]
    cases.extend(
        [
            "<a x='&unknown;'/>",
            "<a xmlns='&unknown;'/>",
            "<a xml:space='&unknown;'/>",
            "<!DOCTYPE a [<!ENTITY e 'value'>]><a x='A&e;Z'/>",
        ]
    )
    seeds = cases.copy()
    for seed in seeds:
        cases.extend(seed[:i] for i in range(len(seed)))
    for code in range(128):
        c = chr(code)
        cases.extend(
            [
                '<' + c + '/>',
                '<a' + c + '/>',
                '<a>' + c + '</a>',
                "<a x='" + c + "'/>",
                '<!--' + c + '--><a/>',
                '<?pi ' + c + '?><a/>',
                '<a><![CDATA[' + c + ']]></a>',
            ]
        )
    for value in [
        '',
        '1.0',
        '1.00',
        '1.01',
        '1.0garbage',
        '1.1',
        '2.0',
        'yes',
        'no',
        'YES',
        'utf-8',
        'UTF-16',
        'bogus',
        'a b',
        '\\0',
        '\\ud800',
    ]:
        for key in ['version', 'encoding', 'standalone', 'other']:
            cases.extend(
                [
                    '<?xml ' + key + '="' + value + '"?><a/>',
                    '<?xml version="1.0" ' + key + '="' + value + '"?><a/>',
                ]
            )
    for n in [
        0,
        1,
        8,
        9,
        10,
        11,
        13,
        14,
        31,
        32,
        127,
        128,
        0xD7FF,
        0xD800,
        0xDBFF,
        0xDC00,
        0xDFFF,
        0xE000,
        0xFFFD,
        0xFFFE,
        0xFFFF,
        0x10000,
        0x10FFFF,
        0x110000,
        0x7FFFFFFF,
        0xFFFFFFFF,
        0x100000000,
    ]:
        for number in [str(n), 'x' + format(n, 'x'), 'x' + format(n, 'X')]:
            cases.extend(['<a>&#' + number + ';</a>', "<a x='&#" + number + ";'/>"])
    for prefix in ['p', 'xml', 'xmlns', '']:
        for uri in [
            '',
            'urn:x',
            'http://www.w3.org/XML/1998/namespace',
            'http://www.w3.org/2000/xmlns/',
        ]:
            declaration = ('xmlns:' + prefix if prefix else 'xmlns') + '="' + uri + '"'
            cases.extend(
                [
                    '<a ' + declaration + '/>',
                    '<' + (prefix + ':' if prefix else '') + 'a ' + declaration + '/>',
                ]
            )
    for length in [
        70,
        79,
        80,
        81,
        82,
        255,
        1023,
        2047,
        4093,
        4094,
        4095,
        4096,
        4097,
        8191,
        8192,
        8193,
        65535,
        65536,
        65537,
    ]:
        for character in [' ', 'x', '\n']:
            value = character * length
            cases.extend(
                [
                    '<a>' + value + '</a>',
                    '<a x="' + value + '"/>',
                    '<!--' + value + '--><a/>',
                    '<a><![CDATA[' + value + ']]></a>',
                    '<' + ('a' * length) + '/>',
                ]
            )
    for text in [
        'a',
        'a [<!-- ] -->]',
        'a [<?pi ] ?>]',
        'a [<!ENTITY x "v]v">]',
        'a [<!ELEMENT a ANY>]',
        'a [<![IGNORE[anything]]>]',
        'a SYSTEM "x"',
        'a PUBLIC "bad\x00id" "x"',
        'a [wrong < grammar & % whatever]',
    ]:
        cases.append('<!DOCTYPE ' + text + '><a/>')
    cases.extend(
        [
            '<a><!DOCTYPE a></a>',
            '<a><!BAD whatever></a>',
            '<a><!-x></a>',
            '<a><![CDQxxxxxx]></a>',
            '<!DOCTYPE a><!DOCTYPE a><a/>',
            '<a/> <!DOCTYPE a>',
            ' \n&#32;<a/>',
            '<a> \n&#32; </a>',
            "<a x='1'y='2'/>",
            "<?pi?x?><a/>",
            "<?xml version='1.0'   ?><a/>",
            "<?xml version='1.0'\r\n encoding='bogus' ?><a/>",
        ]
    )
    for value in ['preserve', 'default', ' preserve ', '\tpreserve\r\n', 'yes', '', 'Preserve']:
        for tail in ['><b/> \n</a>', ' broken', ' x=\"unterminated']:
            cases.append('<a xml:space=\"' + value + '\"' + tail)
    for value in ['', 'http://www.w3.org/XML/1998/namespace', 'http://www.w3.org/2000/xmlns/']:
        for tail in [' broken', ' x=\"unterminated', ' a=\"\0']:
            cases.append('<a xmlns:p=\"' + value + '\"' + tail)
    cases.extend(
        [
            '\0',
            '\0<a/>',
            '<a/>\0junk',
            '<a/> \0junk',
            ' \0<a/>',
            '<a>\0',
            '<a>text\0',
            '<a> \0',
            '<a/>\0\0',
        ]
    )
    cases.extend(
        [
            "<a x='v'><b><",
            "<a x='v'><b>",
            "<a x='v'><b><?pi ",
            "<a x='v'><b><!--",
            "<a x='v' y='w'><b><c><",
            "<a x=''/ >",
        ]
    )
    for length in [4092, 4093, 4094, 4095, 4096, 4097, 8192]:
        for character in ['x', ' ']:
            for tail in ['&unknown;', '&#x', '\0', '\ud800z']:
                cases.append('<a>' + character * length + tail + '</a>')
    for decl in [
        '<!ATTLIST a x CDATA "v">',
        '<!ATTLIST a x NMTOKENS " a  b ">',
        '<!ATTLIST a xmlns CDATA "urn:def">',
        '<!ATTLIST a xmlns:p CDATA "urn:p" p:x CDATA "v">',
        '<!ATTLIST a xml:space (default|preserve) "preserve">',
        '<!ATTLIST a x NMTOKEN #IMPLIED>',
    ]:
        for body in ['<a/>', '<a x=" a  b "/>', '<a>  <b/>  </a>', '<a x="explicit"/>']:
            cases.append('<!DOCTYPE a [' + decl + ']>' + body)
    for value in [
        '',
        'text',
        'a&amp;b',
        '<b/>',
        'a<b/>c',
        '<b>text</b>',
        '&x;',
        '&#32;',
        '\r\n',
        '<b>',
        '</a>',
    ]:
        for body in ['<a>&x;</a>', '<a>before&x;after</a>', '<a>&x;&x;</a>']:
            cases.append('<!DOCTYPE a [<!ENTITY x "' + value + '">]>' + body)
    for value in [
        '',
        'text',
        'a&amp;b',
        "a'quoted'c",
        '&x;',
        '&#32;',
        '\r\n',
        '<b/>',
        '&#60;',
        '&lt;',
    ]:
        for body in ['<a n="&x;"/>', '<a n="before&x;after"/>', '<a n="&x;&x;"/>']:
            cases.append('<!DOCTYPE a [<!ENTITY x "' + value + '">]>' + body)
    for depth in [1, 2, 4, 8]:
        definitions = '<!ENTITY e0 "text">' + ''.join(
            '<!ENTITY e' + str(j) + ' "a&e' + str(j - 1) + ';b">' for j in range(1, depth + 1)
        )
        for body in [
            '<a>&e' + str(depth) + ';</a>',
            '<a n="before&e' + str(depth) + ';after"/>',
            '<a n="&e' + str(depth) + ';" m="&e0;"/>',
        ]:
            cases.append('<!DOCTYPE a [' + definitions + ']>' + body)
    for definition in [
        '<!ENTITY x SYSTEM "urn:x">',
        '<!NOTATION n SYSTEM "urn:n"><!ENTITY x SYSTEM "urn:x" NDATA n>',
        '<!ENTITY x SYSTEM "">',
    ]:
        for body in ['<a>&x;</a>', '<a>before&x;after</a>', '<a n="&x;"/>']:
            cases.append('<!DOCTYPE a [' + definition + ']>' + body)
    if not a.quick:
        for code in range(65536):
            c = chr(code)
            cases.extend(
                ['<' + c + '/>', '<a' + c + '/>', '<a>' + c + '</a>', "<a x='" + c + "'/>"]
            )
    if a.deep:
        assert a.load and a.summary, 'Deep fixture must use iterative load/summary paths'
        cases = []
        for depth in [1, 64, 512, 4096, 16384, 65536]:
            start = "<Nini x='v'>" + '<a>' * depth
            end = '</a>' * depth + '</Nini>'
            cases.extend(
                [
                    start + 'text &#x20000;' + end,
                    start + '<leaf/>' + end,
                    start + 'text &unknown;' + end,
                    start + '\0' + end,
                    start + '</b>' + end,
                ]
            )

    if a.bytes:
        raw = []
        for value in cases:
            for codec, bom in [
                ('utf-8', b''),
                ('utf-8', bytes.fromhex('efbbbf')),
                ('utf-16-le', bytes.fromhex('fffe')),
                ('utf-16-be', bytes.fromhex('feff')),
                ('utf-32-le', bytes.fromhex('fffe0000')),
                ('utf-32-be', bytes.fromhex('0000feff')),
            ]:
                raw.append(bom + value.encode(codec, 'surrogatepass'))
        for code in range(256):
            raw.extend(
                [
                    b'<a>' + bytes([code]) + b'</a>',
                    b"<a x='" + bytes([code]) + b"'/>",
                    bytes.fromhex('efbbbf') + b'<a>' + bytes([code]) + b'</a>',
                ]
            )
        for prefix in [b'', b'\n', b'\n<a', b'<a>\n', b'<a><![CDATA[\n', b'<!--\n']:
            for bom in [b'', bytes.fromhex('efbbbf')]:
                raw.append(bom + prefix + b'\xff')
        for name in [
            'ansi_x3.4-1968',
            'ansi_x3.4-1986',
            'ascii',
            'cp367',
            'cp819',
            'csascii',
            'csisolatin1',
            'csunicode11utf7',
            'ibm367',
            'ibm819',
            'iso-10646-ucs-2',
            'iso-8859-1',
            'iso-ir-100',
            'iso-ir-6',
            'iso646-us',
            'iso8859-1',
            'iso_646.irv:1991',
            'iso_8859-1',
            'iso_8859-1:1987',
            'l1',
            'latin1',
            'ucs-2',
            'unicode',
            'unicode-1-1-utf-7',
            'unicode-1-1-utf-8',
            'unicode-2-0-utf-7',
            'unicode-2-0-utf-8',
            'unicodefffe',
            'us',
            'us-ascii',
            'utf-16',
            'utf-16be',
            'utf-16le',
            'utf-32',
            'utf-32be',
            'utf-32le',
            'utf-7',
            'utf-8',
            'x-unicode-1-1-utf-7',
            'x-unicode-1-1-utf-8',
            'x-unicode-2-0-utf-7',
            'x-unicode-2-0-utf-8',
            'ucs-4',
            'bad',
            'utf8',
        ]:
            for spelling in [name, name.upper()]:
                for codec, bom in [
                    ('utf-8', b''),
                    ('utf-8', bytes.fromhex('efbbbf')),
                    ('utf-16-le', bytes.fromhex('fffe')),
                    ('utf-16-be', bytes.fromhex('feff')),
                    ('utf-32-le', bytes.fromhex('fffe0000')),
                    ('utf-32-be', bytes.fromhex('0000feff')),
                ]:
                    raw.append(
                        bom
                        + (
                            "<?xml version='1.0' encoding='"
                            + spelling
                            + "'?><a>\u00e9\U00020000</a>"
                        ).encode(codec, 'surrogatepass')
                    )
        for scalar in [0x10000, 0x1FFFF, 0x20000, 0x2FFFF, 0x30000, 0xFFFFF, 0x10FFFF]:
            for codec, bom in [
                ('utf-8', b''),
                ('utf-16-le', bytes.fromhex('fffe')),
                ('utf-16-be', bytes.fromhex('feff')),
                ('utf-32-le', bytes.fromhex('fffe0000')),
                ('utf-32-be', bytes.fromhex('0000feff')),
            ]:
                raw.append(bom + ('<a>' + chr(scalar) + '</a>').encode(codec))
        cases = raw

    def row_encoding(value):
        return (value.hex() or '-') if a.bytes else enc(value)

    def peer(command):
        return subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8',
        )

    expected = peer(
        [a.dotnet, str(w / 'app/Reference.dll')]
        + (['--load'] if a.load else [])
        + (['--bytes'] if a.bytes else [])
        + (['--legacy'] if a.legacy else [])
        + (['--standalone'] if a.standalone else [])
        + (['--navigation'] if a.navigation else [])
        + (['--resolve'] if a.resolve else [])
        + (['--normalize'] if a.normalize else [])
        + ['--doc-limit=' + str(a.doc_limit), '--entity-limit=' + str(a.entity_limit)]
        + (['--state'] if a.state else [])
        + (['--summary'] if a.summary else [])
        + (['--start-attribute'] if a.start_attribute else [])
        + (['--prohibit'] if a.prohibit else [])
        + (['--stream'] if a.stream else [])
        + (['--nonseek'] if a.nonseek else [])
        + (['--close-stream'] if a.close_stream else [])
        + (['--chunk=' + str(a.chunk)] if a.stream else [])
    )
    actual = peer(
        [str(Path(a.native).resolve()), '--load' if a.load else '--probe']
        + (['--bytes'] if a.bytes else [])
        + (['--legacy'] if a.legacy else [])
        + (['--standalone'] if a.standalone else [])
        + (['--navigation'] if a.navigation else [])
        + (['--resolve'] if a.resolve else [])
        + (['--normalize'] if a.normalize else [])
        + ['--doc-limit=' + str(a.doc_limit), '--entity-limit=' + str(a.entity_limit)]
        + (['--state'] if a.state else [])
        + (['--summary'] if a.summary else [])
        + (['--start-attribute'] if a.start_attribute else [])
        + (['--prohibit'] if a.prohibit else [])
        + (['--stream'] if a.stream else [])
        + (['--nonseek'] if a.nonseek else [])
        + (['--close-stream'] if a.close_stream else [])
        + (['--chunk=' + str(a.chunk)] if a.stream else [])
    )
    diff = []
    difference_count = 0
    for value in cases:
        row = row_encoding(value) + '\n'
        expected.stdin.write(row)
        expected.stdin.flush()
        e = expected.stdout.readline().rstrip('\r\n')
        actual.stdin.write(row)
        actual.stdin.flush()
        n = actual.stdout.readline().rstrip('\r\n')
        if not e or not n:
            raise AssertionError(('peer closed', expected.poll(), actual.poll()))
        if e != n:
            difference_count += 1
            if len(diff) < 1000:
                diff.append({'input': row_encoding(value), 'expected': e, 'actual': n})
    for process in [expected, actual]:
        process.stdin.close()
        process.wait(timeout=30)
        assert process.returncode == 0, process.stderr.read()
    (w / 'xml-reader-results.json').write_text(
        json.dumps(
            {
                'cases': len(cases),
                'difference_count': difference_count,
                'differences': diff,
                'scope': 'In-progress reader, full closure not yet ready.',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    print(f'{len(cases)-difference_count}/{len(cases)} actual reader flows agree')
    assert not difference_count, w / 'xml-reader-results.json'


if __name__ == '__main__':
    main()
