"""Turn the pinned reference's --export-category UInt16 category dump into native runs."""

from pathlib import Path
import argparse, hashlib

p = argparse.ArgumentParser()
p.add_argument('--data', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
data = a.data.read_bytes()
assert len(data) == 65536 and all(v < 30 for v in data)
runs = [(i, value) for i, value in enumerate(data) if i == 0 or value != data[i - 1]]
text = (
    '// .NET Runtime8.0.31 char.GetUnicodeCategory UInt16 domain.\n// Copyright (c) .NET Foundation and Contributors; MIT third_party/dotnet_collation/LICENSE.\n// Category dump SHA256 '
    + hashlib.sha256(data).hexdigest()
    + '.\n'
)
for name, kind, values in [
    ('regex_category_starts', 'std::uint16_t', [i for i, v in runs]),
    ('regex_category_values', 'std::uint8_t', [v for i, v in runs]),
]:
    text += f'static constexpr {kind} {name}[]={{\n'
    for i in range(0, len(values), 16):
        text += ' ' + ','.join(str(v) for v in values[i : i + 16]) + ',\n'
    text += '};\n'
a.output.write_text(text)
