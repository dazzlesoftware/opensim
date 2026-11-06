#!/usr/bin/env python3
"""Actual pinned synchronous System.Xml.DtdParser differential gate (raw UTF16)."""

import argparse, hashlib, json, pathlib, random, shutil, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def enc(s):
    b = s.encode('utf-16-le', 'surrogatepass')
    return ''.join(f'{int.from_bytes(b[i:i+2],"little"):04x}' for i in range(0, len(b), 2)) or '-'


def cases():
    seeds = [
        'root []>',
        'root>',
        'root [<!ELEMENT root EMPTY>]>',
        'root [<!ELEMENT root ANY>]>',
        'root [<!ELEMENT root (a,b?,(c|d)*,e+)>]>',
        'root [<!ELEMENT root (#PCDATA|a|b)*>]>',
        'root [<!ELEMENT root (#PCDATA)>]>',
        'root [<!ATTLIST root a CDATA "value">]>',
        'root [<!ATTLIST root a ID #REQUIRED b IDREFS " a  b ">]>',
        'root [<!ENTITY x "text"><!ATTLIST root a CDATA "&x;">]>',
        'root [<!ENTITY % x "<!ELEMENT root EMPTY>">%x;]>',
        'root [<!NOTATION n SYSTEM "urn:n"><!ENTITY x SYSTEM "urn:x" NDATA n>]>',
        'root [<!--comment--><?pi body?>]>',
        'root PUBLIC "a" "urn:test">',
        'root SYSTEM "urn:test">',
    ]
    for s in seeds:
        yield 'I|' + enc(s)
    for s in seeds:
        for i in range(len(s) + 1):
            yield 'I|' + enc(s[:i])
            for c in [
                '\0',
                '\n',
                '\r',
                '\t',
                ' ',
                '<',
                '>',
                '[',
                ']',
                '(',
                ')',
                '%',
                '&',
                ';',
                ':',
                '!',
                '?',
                '"',
                "'",
                'a',
                '\ud800',
                '\udfff',
                '\u0085',
                '\uffff',
            ]:
                yield 'I|' + enc(s[:i] + c + s[i + 1 :])
    for typ in [
        'CDATA',
        'ID',
        'IDREF',
        'IDREFS',
        'ENTITY',
        'ENTITIES',
        'NMTOKEN',
        'NMTOKENS',
        'NOTATION (a|b)',
        '(a|b)',
    ]:
        for val in [
            '',
            'a',
            'a:b',
            'xml:lang',
            'xmlns:a',
            'p:a',
            'a:b:c',
            ':a',
            'a:',
            'a b',
            '1abc',
            ' a  b ',
            '\t a\n b\r ',
            '\u00a0a\u00a0',
            '&amp;',
            '&#x9;',
            '&#xA;',
            '&#xD;',
            '\ud800',
            '\U00010000',
            '\u037f',
        ]:
            for default in [
                '"' + val + '"',
                "'" + val + "'",
                '#FIXED "' + val + '"',
                '#REQUIRED',
                '#IMPLIED',
            ]:
                yield 'I|' + enc('root [<!ATTLIST root n ' + typ + ' ' + default + '>]>')
    for name in [
        'root',
        'a:b',
        'a:b:c',
        ':a',
        'a:',
        '',
        '1',
        '\ud800',
        '\U00010000',
        '\uffff',
        '\u037f',
    ]:
        for subset in [
            '',
            '<!ELEMENT root EMPTY>',
            '<!ENTITY x "a">',
            '<!ATTLIST root x CDATA "v">',
            '<!--c-->',
            '<!ELEMENT root (a|b)>',
        ]:
            yield 'F|' + enc(subset) + '|' + enc(name)


def traced_cases():
    seeds = [
        'root []>',
        'root [<!ELEMENT root EMPTY>]>',
        'root [<!ATTLIST root a ID "bad value" b ID "two">]>',
        'root [<!ELEMENT root EMPTY><!ATTLIST root n NOTATION (a|a) "x">]>',
        'root [<!ATTLIST root a (x|x) "y" xml:space CDATA "bad">]>',
        'root [<!ATTLIST root xml:space (bad) "bad">]>',
        'root [<!ATTLIST root xml:space (default|preserve) "default">]>',
        'root [<!ELEMENT root (#PCDATA|a|a)*>]>',
        'root [<!ELEMENT root EMPTY><!ELEMENT root ANY>]>',
        'root [<!NOTATION n SYSTEM "u"><!NOTATION n SYSTEM "v">]>',
        'root [<!ENTITY % x "<!ELEMENT root EMPTY>">%x;]>',
        'root [<!ENTITY x "a&amp;b"><!ATTLIST root a CDATA "&x;&#x20;">]>',
        'root [<!--c--><?pi body?>]>',
        'root PUBLIC "id" "urn:test">',
        'root [<!ENTITY % x "a"> %missing;]>',
        'root [<!ENTITY % x "<!--">%x;]>',
        'root [<!ATTLIST root n NOTATION (n|m|n) "p">]>',
        'root [<!ATTLIST root n CDATA "a\r\nb\rc\n">]>',
    ]
    for bits in range(32):
        flags = f'{bits:05b}'
        for seed in seeds:
            for fault in range(13):
                yield 'I|' + enc(seed) + '|-|' + flags + '|' + str(fault)


def broad_cases():
    for cp in range(65536):
        c = chr(cp)
        yield 'I|' + enc(c + ' []>')
        yield 'F|-|' + enc(c)
        yield 'I|' + enc('root [<!ATTLIST root n ID "' + c + '">]>')
        yield 'I|' + enc('root [<!ATTLIST root n NMTOKEN "' + c + '">]>')
    for length in [4090, 4091, 4092, 4093, 4094, 4095, 4096, 4097, 8190, 8191, 8192, 16384]:
        for value in ['a' * length, 'a:' + 'x' * length, 'x' * length + ':a']:
            yield 'I|' + enc(value + ' []>') + '|-|01111|0'
        for value in [
            'x' * length,
            ('a\r\nb\nc\r') * (length // 7),
            ('&amp;&#32;' * (length // 10)),
        ]:
            for decl in [
                '<!ATTLIST root n CDATA "' + value + '">',
                '<!--' + value + '-->',
                '<?pi ' + value + '?>',
                '<!ENTITY x "' + value + '"><!ATTLIST root n CDATA "&x;">',
            ]:
                yield 'I|' + enc('root [' + decl + ']>') + '|-|01111|0'
        yield 'I|' + enc('root [<!ELEMENT root (' + '(' * length + 'a' + ')' * length + ')>]>')
    rng = random.Random(31008)
    for i in range(10000):
        atoms = [
            'a',
            'a?',
            'a*',
            'a+',
            'b',
            'p:name',
            '(a|b)',
            '(a,b?)',
            '(#PCDATA)',
            'EMPTY',
            'ANY',
        ]
        model = rng.choice(atoms)
        for j in range(rng.randrange(5)):
            model = (
                '('
                + model
                + rng.choice([',', '|'])
                + rng.choice(atoms)
                + ')'
                + rng.choice(['', '?', '*', '+'])
            )
        yield 'IB|' + enc('root [<!ELEMENT root ' + model + '>]>') + '|-|01111|0'


def external_cases():
    subsets = [
        '',
        '<!ELEMENT root EMPTY>',
        '<![INCLUDE[<!ELEMENT root EMPTY>]]>',
        '<![IGNORE[garbage <![nest]]> more ]]>',
        '<![INCLUDE[<![IGNORE[bad]]><!ATTLIST root a CDATA "x">]]>',
        '<![INCLUDE[',
        '<![IGNORE[',
        '<![IGNORE[\r\nx\r\n]]>',
        '<!ATTLIST root n NOTATION (a|b|a) "a"><!NOTATION a SYSTEM "u"><!ATTLIST root m NOTATION (c|d|c) "c">',
        '<!ENTITY % p "<!ELEMENT root EMPTY>">%p;',
        '<!ENTITY % p "a">%missing;',
        '<!--c--><?pi x?>',
    ]
    for subset in subsets:
        for flags in ['01100', '01110', '01111', '11111', '00111']:
            for fault in range(18):
                yield 'I|' + enc('root SYSTEM "urn:dtd">') + '|-|' + flags + '|' + str(
                    fault
                ) + '|' + enc(subset)
    for text in [
        'text',
        '<!ELEMENT root EMPTY>',
        '<!--comment-->',
        '<![INCLUDE[<!ELEMENT root EMPTY>]]>',
        '&x;',
        '%p;',
        '\r\n',
    ]:
        for declaration, reference in [
            ('<!ENTITY % p SYSTEM "urn:entity">', '%p;'),
            ('<!ENTITY x SYSTEM "urn:entity">', '<!ATTLIST root a CDATA "&x;">'),
        ]:
            for flags in ['01100', '01111']:
                for fault in range(20):
                    yield 'I|' + enc(
                        'root [' + declaration + reference + ']>'
                    ) + '|-|' + flags + '|' + str(fault) + '|' + enc(text)


def mutation_cases():
    for flags in ['011001', '001001', '111111', '0110001']:
        for value in [
            'root [\n<!ELEMENT root EMPTY>]>',
            'root [<!ATTLIST root n CDATA "a\nb\rc\r\nd">]>',
            'root [<!ENTITY x "a&amp;b"><!ATTLIST root n CDATA "&x;&#32;">]>',
            'root [<!--comment--><?pi body?>]>',
            'root [<!ATTLIST root x ID "a" x IDREFS "b c" y (a|b) "b">]>',
            'root [<!ATTLIST a x CDATA "a"><!ATTLIST b x CDATA "b"><!ELEMENT a EMPTY><!ATTLIST c x CDATA "c">]>',
        ]:
            yield 'I|' + enc(value) + '|-|' + flags + '|0'


def free_parameter_cases():
    def value(v):
        return '~' if v is None else enc(v)

    for name in [None, '', 'root', 'p:root', 'a:b:c', '1bad', '\ud800']:
        for base in [None, '', 'urn:base', 'relative', 'http://example.test/root', '\0']:
            for pub in [None, '', 'public id', '#', '\t', '\u0085', '\ud800', '\U00010000']:
                for sys in [
                    None,
                    '',
                    'urn:system',
                    'relative#fragment',
                    '\0',
                    '\ud800',
                    '\U00010000',
                ]:
                    for subset in [None, '', '<!ELEMENT root EMPTY>']:
                        yield '|'.join(
                            [
                                'G',
                                value(subset),
                                value(name),
                                '01111',
                                '0',
                                '~',
                                value(base),
                                value(pub),
                                value(sys),
                            ]
                        )


def xml_fault_cases():
    name = 'a' * 5000
    text = 'root [<!ENTITY ' + name + ' "value"><!ATTLIST root n CDATA "&' + name + ';">]>'
    for failure in range(151):
        yield 'I|' + enc(text) + '|-|01100001|' + str(failure)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--native', required=True)
    ap.add_argument('--dotnet', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--quick', action='store_true')
    a = ap.parse_args()
    w = pathlib.Path(a.work).resolve()
    w.mkdir(parents=True, exist_ok=True)
    ref = w / 'reference'
    ref.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'tests/nini_xml_dtd_parser_reference.cs', ref / 'Program.cs')
    (ref / 'reference.csproj').write_text(
        '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><Nullable>enable</Nullable></PropertyGroup></Project>',
        encoding='utf-8',
    )
    subprocess.run(
        [a.dotnet, 'build', str(ref), '-c', 'Release', '--nologo', '-v', 'quiet'], check=True
    )
    metadata = json.loads(
        subprocess.check_output(
            [a.dotnet, str(ref / 'bin/Release/net8.0/reference.dll'), '--metadata'], text=True
        )
    )
    assert metadata['runtime'] == '8.0.31', metadata
    (w / 'runtime-metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    inputs = list(
        dict.fromkeys(
            list(cases())
            + list(traced_cases())
            + ([] if a.quick else list(broad_cases()))
            + list(external_cases())
            + list(mutation_cases())
            + list(free_parameter_cases())
            + list(xml_fault_cases())
        )
    )
    data = '\n'.join(inputs) + '\n'
    (w / 'inputs.txt').write_text(data, encoding='ascii')
    out = []
    for tag, cmd in [
        ('source', [a.dotnet, str(ref / 'bin/Release/net8.0/reference.dll')]),
        ('native', [a.native, '--probe']),
    ]:
        run = subprocess.run(cmd, input=data, text=True, capture_output=True, timeout=120)
        (w / (tag + '.txt')).write_text(run.stdout, encoding='utf-8')
        (w / (tag + '.stderr.txt')).write_text(run.stderr, encoding='utf-8')
        assert run.returncode == 0, (tag, run.returncode, run.stderr)
        out.append(run.stdout.splitlines())
    assert len(out[0]) == len(out[1]) == len(inputs), [len(x) for x in out]
    faults = []
    seen = set(inputs)
    for case, source in zip(inputs, out[0]):
        parts = case.split('|')
        if (
            len(parts) < 5
            or parts[0] != 'I'
            or parts[4] != '0'
            or len(parts[1]) > 4096
            or '|TRACE:' not in source
        ):
            continue
        trace = source.split('|TRACE:')[1].split('|STATE:')[0]
        count = (
            0
            if trace == '-'
            else len(bytes.fromhex(trace).decode('utf-16-be', 'surrogatepass').split(';'))
        )
        for failure in range(1, count + 2):
            changed = parts.copy()
            changed[4] = str(failure)
            row = '|'.join(changed)
            if row not in seen:
                seen.add(row)
                faults.append(row)
    if faults:
        faultdata = '\n'.join(faults) + '\n'
        (w / 'fault-inputs.txt').write_text(faultdata, encoding='ascii')
        for index, (tag, cmd) in enumerate(
            [
                ('source', [a.dotnet, str(ref / 'bin/Release/net8.0/reference.dll')]),
                ('native', [a.native, '--probe']),
            ]
        ):
            run = subprocess.run(cmd, input=faultdata, text=True, capture_output=True, timeout=120)
            assert run.returncode == 0, (tag, run.stderr)
            (w / (tag + '-faults.txt')).write_text(run.stdout, encoding='utf-8')
            extra = run.stdout.splitlines()
            assert len(extra) == len(faults)
            out[index] += extra
        inputs += faults
    errors = [
        {'index': i, 'input': inputs[i], 'source': s, 'native': n}
        for i, (s, n) in enumerate(zip(*out))
        if s != n
    ]
    (w / 'differences.json').write_text(json.dumps(errors, indent=2), encoding='utf-8')
    result = {
        'cases': len(inputs),
        'fault_cases': len(faults),
        'differences': len(errors),
        'runtime': metadata,
        'native_sha256': hashlib.sha256(pathlib.Path(a.native).read_bytes()).hexdigest(),
        'reference_sha256': hashlib.sha256(
            (ROOT / 'tests/nini_xml_dtd_parser_reference.cs').read_bytes()
        ).hexdigest(),
    }
    (w / 'nini-xml-dtd-parser-results.json').write_text(
        json.dumps(result, indent=2), encoding='utf-8'
    )
    print(f'{len(inputs)} source cases, {len(errors)} differences')
    assert not errors, [
        (e["index"], hashlib.sha256(e["input"].encode()).hexdigest()) for e in errors[:3]
    ]


if __name__ == '__main__':
    main()
