"""Actual BCL DateTime local/FileTime conversion closure, including raw kind bits."""

import argparse, json, os, random, time
from pathlib import Path
from compare_invariant_datetime_context import compile_oracle, queries, MAX
from compare_invariant_datetime_parse import Peer


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = Peer(compile_oracle(a.dotnet, work))
    native = Peer([a.native, '--probe'])
    cases = 0
    errors = []
    try:
        metadata = source.ask('META')
        source.send('ZONE LOCAL')
        header = source.read()
        rules = []
        while (line := source.read()) != 'END':
            rules.append(list(map(int, line.split())))
        base = int(header.split()[1])
        points = {int(q.split()[1]) for q in queries(base, rules, False) if q.startswith('U ')}
        points.update([0, 1, 504911232000000000 - 1, 504911232000000000, MAX - 1, MAX])
        rng = random.Random(983011)
        points.update(rng.randrange(MAX + 1) for _ in range(3000))
        qs = []
        for t in sorted(points):
            for kind in range(4):
                for command in ['TOLOCAL', 'TOFILE']:
                    qs.append(f'{command} {t|(kind<<62)}')
        files = {
            -9223372036854775808,
            -1,
            0,
            1,
            2650467743999999999,
            2650467744000000000,
            9223372036854775807,
        }
        files.update(t - 504911232000000000 for t in points if t >= 504911232000000000)
        qs.extend(f'FROMFILE {n}' for n in sorted(files))
        for q in qs:
            expected = source.ask(q)
            actual = native.ask(q)
            cases += 1
            if expected != actual:
                errors.append([q, expected, actual])
                if len(errors) >= 20:
                    break
        report = dict(metadata=metadata, cases=cases, planned=len(qs), differences=errors)
        (work / 'conversion-results.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
        return bool(errors)
    finally:
        source.close()
        native.close()


if __name__ == '__main__':
    raise SystemExit(main())
