# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Compare declaration acceptance with the unchanged managed YEngine compiler."""
import argparse
import json
from pathlib import Path
import subprocess

CASES = [
    'integer fact(integer n){if(n<=1)return 1;return n*fact(n-1);} default{state_entry(){integer n=fact(5);}}',
    'integer outer(integer n){return inner(n);} integer inner(integer n){return n+1;} default{state_entry(){integer n=outer(1);}}',
    'helper(integer n){llSleep(0.1);} default{state_entry(){helper(1);}}',
    'integer missing(){integer n=1;} default{state_entry(){}}',
    'integer f(integer n){return n;} default{state_entry(){integer n=f(1.5);}}',
    'float f(integer n){return n;} default{state_entry(){float n=f(1);}}',
    'integer same(){return 1;} integer same(){return 2;} default{state_entry(){}}',
    'integer same; integer same(){return 1;} default{state_entry(){}}',
    'integer f(integer n,integer n){return n;} default{state_entry(){}}',
    'integer f(integer n){return n;} default{state_entry(){f();}}',
    'integer f(integer n){return n;} default{state_entry(){f(1,2);}}',
    'integer f(integer n){return n;} default{state_entry(){f(<1,2,3>);}}',
    'integer f(){return "wrong";} default{state_entry(){}}',
    'integer f(){return;} default{state_entry(){}}',
    'f(){return 1;} default{state_entry(){}}',
    'f(){} default{state_entry(){integer n=f();}}',
    'integer f(){{integer local=1;}return local;} default{state_entry(){}}',
    'integer f(integer n){if(n)return 1;else return 2;} default{state_entry(){integer n=f(1);}}',
    'integer f(integer n){if(n)return 1;} default{state_entry(){}}',
    'string f(key id){return id;} default{state_entry(){string s=f(NULL_KEY);}}',
    'key f(string id){return id;} default{state_entry(){key k=f("");}}',
    'integer f(integer n){integer x=n;{integer x=2;}return x;} default{state_entry(){integer n=f(1);}}',
    'integer same=3; integer same(){return 1;} default{state_entry(){integer n=same+same();}}',
    'integer f(integer n){return n;} integer f(float n){return (integer)n;} default{state_entry(){integer n=f(1)+f(1.5);}}',
    'integer f(integer n){return n;} float f(integer n){return n;} default{state_entry(){}}',
    'void f(){} default{state_entry(){f();}}',
    'integer odd(integer n){if(n==0)return 0;return even(n-1);} integer even(integer n){if(n==0)return 1;return odd(n-1);} default{state_entry(){integer n=even(6);}}',
    'integer f(){return 3;} integer n=f(); default{state_entry(){}}',
    'integer n=f(); integer f(){return 3;} default{state_entry(){}}',
    'integer f(integer n){while(TRUE){if(n)return 1;}} default{state_entry(){}}',
]


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
        run = subprocess.run([args.dotnet, str(args.reference), str(args.original_bin)],
                             input=''.join(json.dumps(case) + '\n' for case in CASES),
                             text=True, capture_output=True, check=True)
        results = [json.loads(line) for line in run.stdout.splitlines()]
        if len(results) != len(CASES) or any(item['exception'] for item in results):
            raise RuntimeError('Original compiler oracle failed: ' + run.stdout + run.stderr)
        args.oracle.write_text(json.dumps(dict(cases=CASES, original=results), indent=2) + '\n')
    oracle = json.loads(args.oracle.read_text())
    if not args.native:
        return
    run = subprocess.run([str(args.native)], input='\n'.join(oracle['cases']) + '\n',
                         text=True, capture_output=True, check=True)
    native = [line == '1' for line in run.stdout.splitlines()]
    expected = [item['accepted'] for item in oracle['original']]
    mismatches = [index for index, pair in enumerate(zip(native, expected)) if pair[0] != pair[1]]
    report = dict(cases=len(expected), native=native, expected=expected, mismatches=mismatches,
                  passed=len(native) == len(expected) and not mismatches)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
