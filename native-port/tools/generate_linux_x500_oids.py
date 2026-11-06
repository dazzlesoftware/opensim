"""Generate runtime8.0.31 Linux OidLookup labels from its pinned source/decompile.

Source: src/libraries/System.Security.Cryptography/src/System/Security/Cryptography/OidLookup.cs
in dotnet/runtime tag v8.0.31. Includes primary labels and compatibility labels;
aliases used only for reverse name lookup are intentionally excluded.
"""

import argparse, re
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
s = a.source.read_text(encoding='utf-8-sig')
entries = re.findall(r'AddEntry\("([0-9.]+)",\s*"([^"]+)"', s)
compat = re.findall(r'\{\s*"([0-9.]+)",\s*"([^"]+)"\s*\}', s)
assert len(entries) == 103 and len(compat) == 11, (len(entries), len(compat))
values = dict(entries)
for key, value in compat:
    values.setdefault(key, value)
a.output.write_text(
    '// Generated from runtime8.0.31 OidLookup.cs; .NET Foundation MIT license.\n'
    + ''.join('{"' + key + '","' + value + '"},\n' for key, value in values.items()),
    encoding='utf-8',
)
