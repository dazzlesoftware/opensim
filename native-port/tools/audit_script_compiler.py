"""Compile existing interface probes; acceptance does not establish runtime parity."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    calls = json.loads((args.audit / 'script-api-method-review.json').read_text())
    rows = []
    control = subprocess.run([str(args.probe), str(args.audit / 'probe/control.lsl')],
                             capture_output=True, text=True, check=True)
    assert '\taccepted' in control.stdout, control.stdout + control.stderr
    for offset in range(0, len(calls), 50):
        batch = calls[offset:offset + 50]
        paths = [args.audit / 'probe/calls' / Path(call['probe']).name for call in batch]
        result = subprocess.run([str(args.probe), *map(str, paths)],
                                capture_output=True, text=True, check=True)
        lines = result.stdout.splitlines()
        assert len(lines) == len(batch), result.stdout + result.stderr
        for call, path, line in zip(batch, paths, lines):
            fields = line.split('\t')
            assert fields[1] in ('accepted', 'rejected'), line
            rows.append(dict(id=call['id'], status=fields[1], diagnostics=fields[2:],
                             sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report = dict(overloads=len(rows), counts=dict(collections.Counter(row['status'] for row in rows)),
                  probeSha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(), calls=rows,
                  scope='Compile-only coverage of existing interface probes; no runtime behavior claim.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['counts']))


if __name__ == '__main__':
    main()
