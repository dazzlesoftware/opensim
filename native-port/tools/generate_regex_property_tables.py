"""Convert pinned ILSpy RegexCharClass category/property constants into UTF16 data."""

from pathlib import Path
import argparse, re, hashlib

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
source = a.source.read_text(encoding='utf-8-sig')


def decode(value):
    escapes = {
        '0': '\0',
        'a': '\a',
        'b': '\b',
        'f': '\f',
        'n': '\n',
        'r': '\r',
        't': '\t',
        'v': '\v',
        '\\': '\\',
        '"': '"',
        "'": "'",
    }
    return re.sub(
        r'\\(u[0-9a-fA-F]{4}|[0abfnrtv\\"\'])',
        lambda m: chr(int(m[1][1:], 16)) if m[1].startswith('u') else escapes[m[1]],
        value,
    )


literal = r'"((?:\\.|[^"\\])*)"'
categories = source[
    source.index('s_definedCategories =') : source.index(
        'private static readonly string[][] s_propTable'
    )
]
properties = source[
    source.index('s_propTable =') : source.index('private List<(char First, char Last)> _rangelist')
]
groups = [
    (
        'regex_defined_categories',
        re.findall(r'\{\s*' + literal + r'\s*,\s*' + literal + r'\s*\}', categories),
    ),
    (
        'regex_property_ranges',
        re.findall(
            r'new string\[2\]\s*\{\s*' + literal + r'\s*,\s*' + literal + r'\s*\}', properties
        ),
    ),
]
assert len(groups[0][1]) == 38 and len(groups[1][1]) == 112
text = (
    '// .NET8.0.31 RegexCharClass constants; MIT third_party/dotnet_collation/LICENSE.\n// Decompiled source SHA256 '
    + hashlib.sha256(a.source.read_bytes()).hexdigest()
    + '.\nstruct RegexNamedClassData{std::u16string_view name,value;};\n'
)
for name, entries in groups:
    for i, (key, value) in enumerate(entries):
        text += (
            f'static constexpr char16_t {name}_{i}[]={{'
            + ','.join('0x%04x' % ord(c) for c in decode(value))
            + '};\n'
        )
    text += f'static constexpr RegexNamedClassData {name}[]={{\n'
    for i, (key, value) in enumerate(entries):
        text += (
            '{u"'
            + decode(key)
            + '",std::u16string_view('
            + name
            + '_'
            + str(i)
            + ','
            + str(len(decode(value)))
            + ')},\n'
        )
    text += '};\n'
a.output.write_text(text)
