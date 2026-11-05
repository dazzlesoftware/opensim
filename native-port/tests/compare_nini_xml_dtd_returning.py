"""Actual pinned DtdParser continuation after a returning adapter Throw callback.

The callback returns only its first one/two/three errors; later calls forward to
the real reader. This bounds malformed-input loops without changing parser code.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--original-comparator', type=Path, default=Path(__file__).with_name('compare_nini_xml_dtd_parser.py'))
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    reference = Path(__file__).with_name('nini_xml_dtd_returning_reference.cs').resolve()
    (w / 'reference.csproj').write_text(
        '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType>'
        '<TargetFramework>net8.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
        '<Nullable>enable</Nullable><RuntimeFrameworkVersion>8.0.31</RuntimeFrameworkVersion>'
        '<RollForward>Disable</RollForward></PropertyGroup><ItemGroup><Compile Include="'
        + str(reference) + '"/></ItemGroup></Project>')
    subprocess.run([a.dotnet, 'build', str(w / 'reference.csproj'), '-c', 'Release', '--nologo', '-v:q'], check=True)
    spec = importlib.util.spec_from_file_location('original', a.original_comparator.resolve())
    original = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original)
    rows = [row + '|-|011000001' + str(budget) + '|0'
            for row in original.cases() if row.startswith('I|') for budget in (1, 2)]
    targeted = ['1>', 'r [<!ELEMENT r (a|)>]>', 'r [<!ELEMENT r (a,b|c)>]>',
                'r [<!ELEMENT r (#PCDATA|a)>]>', 'r [<!ATTLIST r a BAD #IMPLIED>]>',
                'r [<!ATTLIST r a CDATA #FIXED x>]>', 'r [<!ENTITY e BAD>]>',
                'r [<!NOTATION n BAD>]>', 'r [<!ELEMENT r >]>', 'r [<!ELEMENT r ()>]>',
                'r [<!ELEMENT r (a,)>]>', 'r [<!ATTLIST r a (a|) "x">]>']
    rows += ['I|' + original.enc(text) + '|-|011000001' + str(budget) + '|0'
             for text in targeted for budget in (1, 2, 3)]
    data = '\n'.join(rows) + '\n'
    (w / 'inputs.txt').write_text(data, encoding='ascii')
    outputs = []
    for label, command in [('source', [a.dotnet, str(w / 'bin/Release/net8.0/reference.dll')]),
                           ('native', [str(Path(a.native).resolve()), '--probe'])]:
        result = subprocess.run(command, input=data, text=True, capture_output=True, timeout=180)
        (w / (label + '.txt')).write_text(result.stdout, encoding='utf-8')
        (w / (label + '.stderr.txt')).write_text(result.stderr, encoding='utf-8')
        assert result.returncode == 0, (label, result.returncode, result.stderr)
        outputs.append(result.stdout.splitlines())
    assert all(len(out) == len(rows) for out in outputs)
    differences = [{'index': i, 'input': rows[i], 'source': s, 'native': n}
                   for i, (s, n) in enumerate(zip(*outputs)) if s != n]
    report = {'cases': len(rows), 'differences': differences,
              'native_sha256': hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
              'reference_sha256': hashlib.sha256(reference.read_bytes()).hexdigest()}
    (w / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(len(rows), 'actual returning-callback cases,', len(differences), 'differences')
    assert not differences


if __name__ == '__main__':
    main()
