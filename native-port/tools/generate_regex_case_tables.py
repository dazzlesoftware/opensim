"""Regenerate native tables from ILSpy's pinned RegexCaseEquivalences source.

Input: ilspycmd -t System.Text.RegularExpressions.RegexCaseEquivalences <dll>.
This development tool is never used by the native production executable.
"""

from pathlib import Path
import argparse, ast, hashlib, re

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--assembly', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
source = a.source.read_text(encoding='utf-8-sig')
arrays = {}
for name, kind, count in [
    ('EquivalenceCasingValues', 'char', 2336),
    ('EquivalenceFirstLevelLookup', 'ushort', 64),
    ('EquivalenceCasingMap', 'ushort', 10240),
]:
    match = re.search(name + r' => new ' + kind + r'\[(\d+)\]\s*\{(.*?)\};', source, re.S)
    assert match and int(match[1]) == count, name
    values = (
        [ord(ast.literal_eval(v)) for v in re.findall(r"'(?:\\.|[^'\\])*'", match[2])]
        if kind == 'char'
        else [int(v) for v in re.findall(r'\d+', match[2])]
    )
    assert len(values) == count and all(0 <= v <= 65535 for v in values), name
    arrays[name] = values
text = (
    '// .NET Runtime v8.0.31 RegexCaseEquivalences generated tables.\n// Copyright (c) .NET Foundation and Contributors; MIT third_party/dotnet_collation/LICENSE.\n// Extracted from System.Text.RegularExpressions.dll SHA256 '
    + hashlib.sha256(a.assembly.read_bytes()).hexdigest()
    + '.\n'
)
for name, values in arrays.items():
    text += f'static constexpr {"char16_t" if name=="EquivalenceCasingValues" else "std::uint16_t"} {name}[]={{\n'
    for i in range(0, len(values), 16):
        text += ' ' + ','.join('0x%04x' % v for v in values[i : i + 16]) + ',\n'
    text += '};\n'
a.output.write_text(text)
