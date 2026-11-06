"""Actual XmlDocument Save(TextWriter)/WriteTo with a concrete native DOM/writer."""

import argparse, json, random, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--reference', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)


def enc(s):
    return '~' if s is None else s.encode('utf-16-be', 'surrogatepass').hex() or '-'


documents = [
    None,
    '',
    '<a/>',
    '<a></a>',
    ' <a/> ',
    '<a>text</a>',
    '<a>\r\n<b/>\n<c></c></a>',
    '<a x="v"/>',
    '<a x="&lt;&amp;&gt;&quot;&apos;&#13;&#10;"/>',
    '<a><![CDATA[a<&]]></a>',
    '<!--c--><a/><?pi after?>',
    '<?xml version="1.0"?><a/>',
    '<?xml version="1.0" encoding="utf-8" standalone="yes"?><a/>',
    '<?xml version="1.0" standalone="no"?><a/>',
    '<a xmlns="urn:a"><b xmlns=""/><p:c xmlns:p="urn:p" p:x="v"/></a>',
    '<a xml:space="preserve"> \n<b/> </a>',
    '<a xml:space="default"> \n<b/> </a>',
    '<a xml:lang="en"><b xml:lang=""/></a>',
    '<a>\ud800</a>',
    '<a>\udc00</a>',
    '<a>&unknown;</a>',
    '<a/><b/>',
    '<a>',
    '<?xml version="1.1"?><a/>',
    '<a x="1" x="2"/>',
    '<a>\0</a>',
]
rng = random.Random(124092)
for _ in range(150):
    depth = rng.randrange(1, 10)
    leaf = rng.choice(
        ['text &amp; more', '<![CDATA[x<>&]]>', '<!--ok-->', '<?pi data?>', '&#x1F600;', ' \r\n ']
    )
    xml = leaf
    for level in range(depth):
        name = rng.choice(['a', 'b', 'node', 'n'])
        attribute = rng.choice(
            ['', ' x="a &amp; b"', ' xml:lang="en"', ' xmlns="urn:x"', ' xmlns:p="urn:p" p:v="x"']
        )
        xml = f'<{name}{attribute}>' + xml + f'</{name}>'
    documents.append(xml)
documents.extend(
    [
        '<!DOCTYPE r><r/>',
        '<!DOCTYPE r []><r/>',
        '<!DOCTYPE r SYSTEM "urn:unused"><r/>',
        '<!DOCTYPE r PUBLIC "pub" "urn:unused"><r/>',
        '<!DOCTYPE r [<!ELEMENT r ANY><!ATTLIST r a CDATA "default">]><r/>',
        '<!DOCTYPE r [<!ELEMENT r ANY><!ATTLIST r a NMTOKENS "  a  b  ">]><r a=" a  b "/>',
        '<!DOCTYPE r [<!ENTITY e "value">]><r>&e;</r>',
        '<!DOCTYPE r [<!ENTITY e "<x a=\"v\"/>">]><r>&e;</r>',
        '<!DOCTYPE r [<!ENTITY e "value">]><r a="&e;"/>',
        '<!DOCTYPE r [<!ENTITY a "A"><!ENTITY b "&a;B">]><r>&b;</r>',
    ]
)
for declaration in [
    '<!ATTLIST r a CDATA "default">',
    '<!ATTLIST r a NMTOKENS "  a  b  ">',
    '<!ATTLIST r a (x|y) "x">',
    '<!ATTLIST r xml:space (default|preserve) "preserve">',
    '<!ATTLIST r xmlns CDATA "urn:default">',
    '<!ATTLIST r xmlns:p CDATA "urn:p">',
    '<!ENTITY e "">',
    '<!ENTITY e "text">',
    '<!ENTITY e "a&#13;b&#10;c">',
    '<!ENTITY e "<p:x xmlns:p=\'urn:p\' a=\'v\'/>">',
    '<!ENTITY a "A"><!ENTITY e "&a;B">',
    '<!NOTATION n SYSTEM "unused"><!ENTITY image SYSTEM "image" NDATA n>',
    '<!--comment--><?pi value?><!ENTITY e "text">',
    '<!ENTITY % p "<!ATTLIST r a CDATA \'default\'>">%p;',
]:
    for body in [
        '<r/>',
        '<r a="explicit"/>',
        '<r> &e; </r>',
        '<r a="&e;"/>',
        '<r><p:x xmlns:p="urn:p"/></r>',
    ]:
        documents.append('<!DOCTYPE r [' + declaration + ']>' + body)
for public in ['', 'pub', 'a\r\nb\tc', 'a  b']:
    documents.append('<!DOCTYPE r PUBLIC "' + public + '" "urn:unused"><r/>')

rows = []
for document in documents:
    for mode in range(5):
        for preserve in [0, 1]:
            for closed in [-1, 0, 1]:
                for newline in [None, '', '\n', '\r\n', 'X']:
                    rows.append(f'{mode} {preserve} {closed} {enc(newline)} {enc(document)}')
for document in ['empty', 'decl1', 'declbad']:
    for mode in range(5):
        for preserve in [0, 1]:
            for closed in [-1, 0, 1]:
                rows.append(f'{mode} {preserve} {closed} ~ {document}')
configs = [
    '<Nini/>',
    '<Nini></Nini>',
    '<?xml version="1.0"?><Nini/>',
    '<Nini><Section Name="s"><Key Name="k" Value="v"/></Section></Nini>',
    '<Nini><Section Name="a"/><Section Name="b"/><Section Name="c"/></Nini>',
    '<Nini><!--c--><unknown/><Section Name="s"><Key Name="k" Value="&lt;&amp;&#13;&#10;"/></Section></Nini>',
    '<Nini xmlns="urn:a"><Section Name="s"><Key Name="k" Value="v"/></Section></Nini>',
    '<wrong/>',
    '<Nini><Section/></Nini>',
    '<Nini><Section Name="s"><Key Name="k"/></Section></Nini>',
]
for value in ['', 'a', '<&>"\'\r\n', '\ud800', '\ud800\udc00', '\uffff', '\x00']:
    escaped = value.replace('&', '&amp;').replace('<', '&lt;').replace('"', '&quot;')
    configs.append(
        '<Nini><Section Name="s"><Key Name="k" Value="' + escaped + '"/></Section></Nini>'
    )
for xml in configs:
    for mode in [6, 7, 8, 9]:
        rows.append(f'{mode} 0 0 ~ {enc(xml)}')
payload = '\n'.join(rows) + '\n'


def run(command):
    r = subprocess.run(command, input=payload, text=True, capture_output=True)
    assert r.returncode == 0, r.stderr
    lines = r.stdout.splitlines()
    assert len(lines) == len(rows), (len(lines), len(rows))
    return lines


expected = run([a.dotnet, a.reference])
actual = run([a.native, '--probe'])
diff = [
    {'input': rows[i], 'expected': e, 'actual': n}
    for i, (e, n) in enumerate(zip(expected, actual))
    if e != n
]
report = {
    'cases': len(rows),
    'differences': len(diff),
    'examples': diff[:20],
    'scope': 'Concrete retained DOM Save(TextWriter), XmlWriter Save and WriteTo; StringWriter ownership/closed/null/newline behavior. File/stream encodings remain unfinished; live DOM mutation has a separate actual-provider gate. DTD Parse publication and unspecified attributes are consumed here.',
}
(a.work / 'save-results.json').write_text(json.dumps(report, indent=2))
print(len(rows), 'actual save cases;', len(diff), 'differences')
assert not diff, report
