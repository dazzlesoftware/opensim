"""Unchanged CommandConsole.Parser.Parse with BCL Regex/string whitespace behavior."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract

ROOT = Path(__file__).resolve().parents[2]


def invoke(command, rows=None):
    result = subprocess.run(
        command,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=110,
    )
    assert result.returncode == 0, (result.returncode, result.stdout[-2000:], result.stderr[-2000:])
    return result.stdout.splitlines()


def encoded(text):
    return '-' if text is None else text.encode('utf-16-le', 'surrogatepass').hex() or '.'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/Console/CommandConsole.cs'
    generated = work / 'ExtractedParser.cs'
    generated.write_text(
        'using System;using System.Collections.Generic;using System.Text.RegularExpressions;'
        + extract(source.read_text(encoding='utf-8-sig'), 'public class Parser'),
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false', NoWarn='8632'
    ).items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, 'ItemGroup')
    for file in [generated, ROOT / 'native-port/tests/console_parser_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(file))
    cs = work / 'ConsoleParserReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    invoke([args.dotnet, 'build', str(cs), '-o', str(work / 'app'), '--nologo', '-v', 'quiet'])
    managed = [args.dotnet, str(work / 'app/ConsoleParserReference.dll')]
    if args.prepare_only:
        print('Prepared unchanged original console parser oracle')
        return
    texts = [
        None,
        '',
        ' ',
        'fcache clear "memory"',
        'fcache clear "file" memory',
        'fcache clear ""',
        '"',
        '""',
        '"""',
        'a"b"c',
        'a "b c" d',
        '--key="unmatched',
        '--key= "a b" tail',
        '--key="a b"tail',
        '--key="a b""c d"',
        '--="x y"',
        '--key=="x y"',
        '--key_="x y"',
        '--key:="x y"',
        '--a=\n"x y"',
        '--é="x y"',
        '--0=""',
        '---="x y"',
        '"a\x00b"',
        'fcache\u2003clear\u00a0"memory"',
    ]
    for char in range(65536):
        value = chr(char)
        texts.extend([value, 'a' + value + 'b'])
    alphabet = ['a', ' ', '"', '-', '=', '\t']
    for size in range(7):
        texts.extend(''.join(items) for items in itertools.product(alphabet, repeat=size))
    heads = [
        '--x=',
        '--0=',
        '---=',
        '--a-b=',
        '--a_b=',
        '--=',
        '--x==',
        '--é=',
        'x--x=',
        '--X=',
        '--9-=',
        '--a= ',
        '--a=\t',
        'a --a=',
        '--a= z',
    ]
    quoted = [
        '',
        'a',
        'a b',
        ' ',
        '\t',
        'é漢😀',
        '--a=',
        'x"y',
        '\ud800',
        '\udfff',
        '\x00',
        'a\n b',
    ]
    tails = ['', 'x', ' x', '"', '"y"', ' --other=', '\u2003z']
    for head, value, tail in itertools.product(heads, quoted, tails):
        texts.append(head + '"' + value + '"' + tail)
    rng = random.Random(114938)
    units = [
        'a',
        'b',
        '-',
        '=',
        '"',
        ' ',
        '\t',
        '\r',
        '\n',
        '\x00',
        '\u0085',
        '\u00a0',
        '\u1680',
        '\u180e',
        '\u2003',
        '\u200b',
        '\u2028',
        '\u202f',
        '\u205f',
        '\u3000',
        '\ufeff',
        'é',
        '漢',
        '😀',
        '\ud800',
        '\udfff',
    ]
    for _ in range(8000):
        texts.append(''.join(rng.choice(units) for _ in range(rng.randrange(100))))
    rows = [encoded(text) for text in texts]
    outputs = [invoke(managed, rows), invoke([args.native, '--probe'], rows)]
    assert len(outputs[0]) == len(outputs[1]) == len(rows), (
        len(outputs[0]),
        len(outputs[1]),
        len(rows),
    )
    differences = [
        dict(index=i, input=row, source=original, native=native)
        for i, (row, original, native) in enumerate(zip(rows, *outputs))
        if original != native
    ]
    report = dict(
        passed=not differences,
        cases=len(rows),
        unicodeUnitCases=131072,
        exhaustiveQuoteCases=sum(6**n for n in range(7)),
        optionCombinations=len(heads) * len(quoted) * len(tails),
        randomCases=8000,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Entire unchanged original Parser class with actual BCL Regex and string.Split whitespace behavior. Raw UTF16 units including all 65536 values, unpaired surrogates and NUL are preserved. Quote segments, empty tokens, unmatched quote reconstruction, exact ASCII option regex and trailing/embedded whitespace are compared. UTF8 console boundary uses the existing replacement decoder; complete command resolution, aliases and interactive line editing remain separate pending work.',
        differences=differences[:4],
    )
    (work / 'console-parser-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not differences, json.dumps(differences[:3])
    print(f'PASS: {len(rows)} original console parser observations')


if __name__ == '__main__':
    main()
