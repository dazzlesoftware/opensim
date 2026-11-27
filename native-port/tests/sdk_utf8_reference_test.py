"""Compare isolated SDK backing-byte conversion against genuine object API cases."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    args = parser.parse_args()
    rows = json.loads((args.fixture / 'metadata.json').read_text())
    assert len(rows) == 123
    checks = []
    for row in rows:
        units = ''.join(f'{unit:04x}' for unit in row['input']['units'])
        for bound, field, getter in [('-', 'name_large_bytes', 'Name'), ('63', 'name_wire_bytes', None),
                                     ('127', 'description_bytes', 'Desc')]:
            # A failing Name setter never reaches its second, bounded cache constructor.
            if bound == '63' and 'NameException' in row:
                continue
            output = subprocess.check_output([str(args.probe), units, bound], text=True).strip()
            if bound == '-' and 'NameException' in row:
                assert row['NameException'].startswith('System.OverflowException:')
                assert output == 'OverflowException', (row['name'], bound, output)
            else:
                assert output.startswith('ok:'), (row['name'], bound, output)
                raw = bytes.fromhex(output[3:])
                expected = row[field] or ''
                assert raw.hex() == expected, (row['name'], bound, raw.hex(), expected)
                if getter:
                    text = raw.decode('utf-8', errors='replace').encode('utf-16-le', errors='surrogatepass')
                    actual_units = [text[i] + 256 * text[i+1] for i in range(0, len(text), 2)]
                    assert actual_units == row[getter]['units'], (row['name'], getter, actual_units)
            checks.append(dict(case=row['name'], bound=bound, passed=True))
    assert len(checks) == 361
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(passed=True, originalCases=123, directByteOrExceptionComparisons=361,
                  nameWireCacheNotReachedOnOriginalSetterExceptions=8,
                  fixtureSha256=sha(args.fixture / 'metadata.json'), probeSha256=sha(args.probe),
                  scriptSha256=sha(Path(__file__)), sdkBytesToStringUsed=False,
                  runtimeIntegrationVerified=False, fullRegionDatabaseRestartVerified=False, checks=checks)
    root = Path(__file__).resolve().parents[1]
    report['sourceSha256'] = {name: sha(root / name) for name in
                             ('include/opensim/framework/sdk_utf8.hpp', 'src/sdk_utf8.cpp',
                              'tests/sdk_utf8_probe.cpp', 'tests/sdk_utf8_reference_test.py')}
    args.results.parent.mkdir(parents=True, exist_ok=True)
    args.results.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 123 genuine API cases, 361 byte/exception checks and exact getter UTF16 units')


if __name__ == '__main__':
    main()
