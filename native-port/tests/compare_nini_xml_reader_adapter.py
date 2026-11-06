"""Actual pinned System.Xml internal adapter differential; parser closure is separate."""

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
        Include=str(Path(__file__).with_name('nini_xml_reader_adapter_reference.cs').resolve()),
    )
    file = w / 'Reference.csproj'
    ET.ElementTree(project).write(file)
    r = subprocess.run(
        [a.dotnet, 'build', str(file), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    cases = []
    for value in [
        '',
        '0',
        '9',
        '10',
        '13',
        '32',
        '65',
        '127',
        '55296',
        '65535',
        '65536',
        '1114111',
        '1114112',
        '2147483599',
        '2147483647',
        '2147483648',
        '4294967295',
        '999999999999999',
        'x0',
        'x41',
        'x10000',
        'x110000',
        'x7fffffff',
        'x80000000',
        '-1',
        '+1',
        'x',
        'X41',
        'xZZ',
        '0z',
    ]:
        for suffix in [';', '', 'x;']:
            for prefix in ['', 'x', '\n', '\r\n']:
                cases.append((prefix + '&#' + value + suffix, 'N', len(prefix)))
    for name in [
        'lt',
        'gt',
        'amp',
        'apos',
        'quot',
        'unknown',
        'a',
        'am',
        'ap',
        'q',
        'l',
        'g',
        '',
        'AMP',
        'ltx',
        'amp\0',
    ]:
        for suffix in [';', '', 'x;']:
            for op in ['E', 'U']:
                cases.append(('&' + name + suffix, op, 0))
    for text in [
        'abc-->',
        'abc--x-->',
        '-->',
        '--',
        '-',
        '',
        '\r\n-->',
        'a\0-->',
        '\ud800-->',
        'target?>',
        'target data?>',
        'xml?>',
        'XML?>',
        'xml data?>',
        'pi\r\n data?>',
        'pi?x?>',
        'pi',
        'pi ',
    ]:
        for op in ['C', 'P']:
            for prefix in ['', '\n', '  ']:
                cases.append((prefix + text, op, len(prefix)))
    for code in range(128 if a.quick else 65536):
        c = chr(code)
        cases.extend(
            [('&#' + c + ';', 'N', 0), ('a' + c + '-->', 'C', 0), ('p ' + c + '?>', 'P', 0)]
        )
    for value in ['', 'x', '\r\n', 'text', '<a/>', '&amp;', '\ud800', '\0', 'a' * 4097]:
        for op in ['H', 'J', 'Q', 'S', 'T', 'X', 'Z', 'F', 'L', 'K']:
            cases.append((value, op, 0))
    for length in [79, 80, 81, 4093, 4094, 4095, 4096, 4097, 8191, 8192, 8193, 65535, 65536, 65537]:
        for value in ['x' * length, ('x\r\n' * (length // 3))]:
            cases.extend(
                [
                    (value + '-->', 'C', 0),
                    ('pi ' + value + '?>', 'P', 0),
                    (value + '&#65;', 'N', len(value)),
                    (value + '&amp;', 'E', len(value)),
                ]
            )
    for value in ['\n-->', '\r\n-->', 'x-->']:
        cases.append((value, 'B', 0))
    cases = list(dict.fromkeys(cases))
    native = subprocess.Popen(
        [a.native, "--probe"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        encoding='utf-8',
    )
    reference = subprocess.Popen(
        [a.dotnet, str(w / 'app/Reference.dll')],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        encoding='utf-8',
    )
    diffs = []
    try:
        for i, (text, op, pos) in enumerate(cases):
            line = enc(text) + '|' + op + '|' + str(pos) + '\n'
            for peer in [native, reference]:
                peer.stdin.write(line)
                peer.stdin.flush()
            n = native.stdout.readline().rstrip('\n')
            r = reference.stdout.readline().rstrip('\n')
            if n != r and len(diffs) < 100:
                diffs.append(
                    {
                        'case': i,
                        'text': enc(text),
                        'op': op,
                        'position': pos,
                        'native': n,
                        'reference': r,
                    }
                )
    finally:
        for peer in [native, reference]:
            peer.stdin.close()
            peer.wait()
    result = {
        'cases': len(cases),
        'recorded_differences': len(diffs),
        'differences': diffs,
        'scope': 'internal adapter only; complete XML/DTD/writer closure not claimed',
    }
    (w / 'adapter-results.json').write_text(json.dumps(result, indent=2))
    print(f'{len(cases)} adapter flows, {len(diffs)} recorded differences')
    return bool(diffs)


if __name__ == '__main__':
    raise SystemExit(main())
