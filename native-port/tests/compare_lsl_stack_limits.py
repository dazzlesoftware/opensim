# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Measure original recursion boundaries and compare the configured native VM."""
import argparse
import json
from pathlib import Path
import subprocess


def cases():
    result = []
    for configured in (0, 16384, 32768, 65536):
        normalized = max(16384, configured)
        for local in (False, True):
            # The formula chooses interesting inputs only. Expected outcomes are
            # obtained by executing the unchanged original generated methods.
            charge = 72 if local else 68
            boundary = (normalized // 2 - 64) // charge - 1
            body = ('integer local=n;if(local<=0)return 0;return 1+recurse(local-1);'
                    if local else 'if(n<=0)return 0;return 1+recurse(n-1);')
            for initializer in (False, True):
                depths = (boundary - 1, boundary, boundary + 1) if initializer else (boundary, boundary + 1)
                for depth in depths:
                    function = 'integer recurse(integer n){' + body + '}'
                    if initializer:
                        source = ('integer result=recurse(' + str(depth) + ');' + function +
                                  'default{state_entry(){}}')
                    else:
                        source = ('integer result;' + function +
                                  'default{state_entry(){result=recurse(' + str(depth) + ');}}')
                    result.append(dict(configured=configured, normalized=normalized,
                                       local=local, initializer=initializer, depth=depth, source=source))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--original-bin', type=Path)
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.reference:
        observed = []
        for case in cases():
            run = subprocess.run([args.dotnet, str(args.reference), str(args.original_bin),
                                  str(case['normalized']), str(case['normalized'] // 2)],
                                 input=json.dumps(case['source']) + '\n',
                                 capture_output=True, text=True, check=True)
            original = json.loads(run.stdout)
            if not original['accepted']:
                raise RuntimeError('Original stack case failed compilation: ' + run.stdout)
            if original['exception']:
                if 'OutOfStackException' not in original['exception']:
                    raise RuntimeError('Original reference failed unexpectedly: ' + run.stdout)
                expected = None
            else:
                if not original['executed']:
                    raise RuntimeError('Original reference omitted execution')
                expected = [original['globals']['result']]
            observed.append(dict(**case, original=original, expected=expected))
        args.oracle.write_text(json.dumps(dict(cases=observed), indent=2) + '\n')
    if not args.native:
        return
    oracle = json.loads(args.oracle.read_text())
    comparisons = []
    for case in oracle['cases']:
        run = subprocess.run([str(args.native), '--execute', str(case['configured'])],
                             input=case['source'] + '\n', capture_output=True, text=True, check=True)
        actual = json.loads(run.stdout)
        comparisons.append(dict(configured=case['configured'], local=case['local'], initializer=case['initializer'],
                                depth=case['depth'], original=case['expected'], native=actual,
                                passed=actual == case['expected']))
    report = dict(cases=comparisons, passed=all(case['passed'] for case in comparisons))
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
