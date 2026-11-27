# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Compare native execution against saved original YEngine-generated IL results."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def global_names(source):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z_0-9]*|[^\s]', source)
    braces = parentheses = 0
    names = []
    types = {'integer', 'float', 'string', 'key', 'list', 'vector', 'rotation'}
    for index, token in enumerate(tokens):
        if token == 'default' and braces == parentheses == 0:
            break
        if (braces == parentheses == 0 and token in types and
                index + 2 < len(tokens) and tokens[index + 2] != '('):
            names.append(tokens[index + 1])
        braces += (token == '{') - (token == '}')
        parentheses += (token == '(') - (token == ')')
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    oracle = json.loads(args.oracle.read_text())
    cases = oracle['cases']
    if any(not case['accepted'] or not case['executed'] or case['exception'] for case in cases):
        raise RuntimeError('Original execution oracle contains an unsuccessful case')
    run = subprocess.run([str(args.native), '--execute'],
                         input=''.join(case['source'] + '\n' for case in cases),
                         capture_output=True, text=True, check=True)
    results = [json.loads(line) for line in run.stdout.splitlines()]
    if len(results) != len(cases):
        raise RuntimeError('Native execution probe omitted results')
    comparisons = []
    for case, result in zip(cases, results):
        names = global_names(case['source'])
        expected = [case['globals'][name] for name in names]
        comparisons.append(dict(name=case['name'], globals=names, original=expected,
                                native=result, passed=expected == result))
    report = dict(cases=comparisons, passed=all(case['passed'] for case in comparisons))
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
