"""Exact source node values, literal analyses and bounded graph length behavior."""

from pathlib import Path
import argparse, hashlib, json, random, subprocess, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
lab = Path(__file__).resolve().parent
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for k, v in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, k).text = v
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(lab / 'dotnet_regex_node_reference.cs'))
ET.ElementTree(project).write(w / 'Reference.csproj')
r = subprocess.run(
    [a.dotnet, 'build', str(w / 'Reference.csproj'), '-o', str(w / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(w / 'build.txt').write_text(r.stdout + r.stderr)
assert r.returncode == 0, r.stdout + r.stderr


def enc(s):
    return '-' if s is None else s.encode('utf-16-be', 'surrogatepass').hex() or '.'


def row(*ops):
    return ';'.join(ops)


cases = []
for code in range(65536):
    cases.append(row(f'C 0 {code} 1 . 0', 'Q 0'))
for kind in range(256):
    for options in [0, 1, 64, 1024, -1]:
        for ctor in ['z', 'c 65', 's ' + enc('ab'), 's -', 'm -2', 'q 2 2147483647']:
            cases.append(row(f'N 0 {kind} {options} {ctor}', 'Q 0', 'A 0', 'L 0 5', 'T 0', 'Q 0'))
for code in [0, 65, 73, 97, 105, 0x130, 0x131, 0x212A, 0xFFFF]:
    for name in ['', 'en-US', 'tr-TR', 'az-Latn-AZ']:
        for state in [0, 1, 2, 3, 99]:
            cases.append(row(f'C 0 {code} 9 {enc(name)} {state}', 'Q 0', 'A 0'))
for left, right in [(65, 97), (75, 107), (73, 105), (0x130, 105), (0xFFFF, 0xFFFF)]:
    cases.append(row(f'C 0 {left} 1 . 0', f'C 1 {right} 1 . 0', 'I 0 1'))
for kind in range(256):
    for shape in [0, 1, 2]:
        command = 'H 2 0' if shape == 0 else 'H 2 1 0' if shape == 1 else 'H 2 2 0 1'
        cases.append(
            row(
                'N 0 9 0 c 65',
                'N 1 12 0 s ' + enc('abcd'),
                f'N 2 {kind} 0 q 2 7',
                command,
                'F 0 Parent 2',
                'F 1 Parent 2',
                'Q 2',
                'A 2',
                'J 2 -1',
                'J 2 2147483647',
                'R 2',
                'Q 2',
            )
        )
for kind in [3, 4, 5, 6, 7, 8, 43, 44, 45, 26, 27, 24, 25, 28, 29, 32, 33, 34]:
    for minimum in [-2147483648, -1, 0, 1, 2, 63, 64, 65, 2147483647]:
        for maximum in [-1, 0, 1, 2, 2147483646, 2147483647]:
            cases.append(
                row(
                    'N 0 12 0 s ' + enc('ab'),
                    f'N 1 {kind} 0 q {minimum} {maximum}',
                    'H 1 1 0',
                    'A 1',
                    'T 1',
                    'Q 1',
                    'A 1',
                )
            )


def cls(bounds=(), categories=(), neg=0):
    return (
        chr(neg)
        + chr(len(bounds))
        + chr(len(categories))
        + ''.join(chr(c & 65535) for c in [*bounds, *categories])
    )


sets = [
    None,
    '',
    cls(),
    cls([0]),
    cls([65, 66]),
    cls([65, 66], neg=1),
    cls([65535]),
    cls([65535, 0]),
    cls([65, 91]),
    cls([65, 68, 97, 100]),
    cls(categories=[100]),
    cls(categories=[100, -100]),
    cls(categories=[-100, 100]),
    cls(categories=[9, -9]),
    cls(categories=[0, 2, 4, 5, 3, 1, 6, 9, 19, 0, 0, -2, -4, -5, -3, -1, -6, -9, -19, 0]),
]
for kind in [5, 8, 11, 45]:
    for s in sets:
        cases.append(
            row(
                f'N 0 {kind} 0 s {enc(s)}',
                'F 0 M 2',
                'F 0 N 7',
                'Q 0',
                'L 0 0',
                'L 0 1',
                'L 0 5',
                'L 0 128',
                'Z 0',
                'Q 0',
                'A 0',
            )
        )
cases.append(
    row(
        'N 0 11 0 s ' + enc(cls(categories=[100, -100])),
        'N 1 11 0 s ' + enc(cls(categories=[9, -9])),
        'Z 0',
        'Z 1',
        'I 0 1',
    )
)
for depth in [1, 38, 39, 40, 41, 42]:
    ops = ['N 0 9 0 c 65']
    for i in range(1, depth):
        ops.extend([f'N {i} 28 0 z', f'H {i} 1 {i-1}'])
    cases.append(row(*ops, f'Q {depth-1}', f'A {depth-1}', f'L {depth-1} 5'))
for options in [0, 64, 1024]:
    cases.append(
        row(f'N 0 25 {options} z', 'N 1 9 0 c 65', 'N 2 9 0 c 66', 'H 0 2 1 2', 'R 0', 'Q 0')
    )
for kind in [24, 25, 26, 28, 29, 32, 33, 34]:
    cases.append(row('N 0 13 0 z', f'N 1 {kind} 0 q 0 2147483647', 'H 1 2 0 -', 'A 1'))
# Stack-boundary integration remains an explicit host dependency.
cases.append(row('N 0 9 0 s .', 'N 1 12 0 s .', 'I 0 1'))
foundation_flows = len(cases)
for kind in range(256):
    for shape in [0, 1, 2]:
        command = 'H 2 0' if shape == 0 else 'H 2 1 0' if shape == 1 else 'H 2 2 0 1'
        cases.append(
            row(
                'N 0 9 0 c 65',
                'N 1 12 0 s ' + enc('ab'),
                f'N 2 {kind} 1 z',
                command,
                'F 0 Parent 2',
                'F 1 Parent 2',
                'U 99 2 r',
                'Q 99',
                'Q 2',
            )
        )
for depth in [1, 2, 3, 8, 16]:
    ops = ['N 0 9 0 c 65']
    for i in range(1, depth + 1):
        ops.extend([f'N {i} 29 1 z', f'H {i} 1 {i-1}', f'F {i-1} Parent {i}'])
    cases.append(row(*ops, f'U 99 {depth} g', 'Q 99', f'Q {depth}'))
for kind in [29, 9, 26, 12]:
    cases.append(row(f'N 0 {kind} 1 z', 'U 99 0 g', 'Q 0'))
bounds = [
    (0, 0),
    (0, 1),
    (1, 1),
    (1, 2),
    (2, 3),
    (2, 4),
    (2, 2147483647),
    (2147483647, 2147483647),
    (-1, 4),
    (-2147483648, 0),
]
for outer_kind in [26, 27]:
    for inner_kind in [26, 27, 3, 4, 5, 6, 7, 8, 43, 44, 45, 9, 10, 11, 12, 29]:
        for m, n in bounds:
            for child_m, child_n in bounds:
                cases.append(
                    row(
                        'N 0 9 0 c 65',
                        f'N 1 {inner_kind} 64 q {child_m} {child_n}',
                        'H 1 1 0',
                        'F 0 Parent 1',
                        f'N 2 {outer_kind} 1 q {m} {n}',
                        'H 2 1 1',
                        'F 1 Parent 2',
                        'U 99 2 l',
                        'Q 99',
                        'Q 0',
                        'Q 1',
                        'Q 2',
                    )
                )
for kind in [26, 27]:
    for m, n in bounds:
        cases.append(row(f'N 0 {kind} -1 q {m} {n}', 'U 99 0 l', 'Q 99', 'Q 0'))
loop_reduction_flows = len(cases) - foundation_flows


def setup_leaf(key, kind, options=0, text='ab', char=65, m=1, n=3):
    return [f'N {key} {kind} {options} q {m} {n}', f'F {key} Ch {char}', f'F {key} Str {enc(text)}']


def concat_case(first, second, mode, root_options=0, ids='0 1', extra=()):
    return row(
        *first,
        *second,
        'N 2 25 ' + str(root_options) + ' z',
        'H 2 2 ' + ids,
        'F 0 Parent 2',
        'F 1 Parent 2',
        *extra,
        ('U 99 2 c' if mode == 'c' else 'V 2 ' + mode),
        'Q 2',
        'Q 0',
        'Q 1',
        'I 0 1',
        *(['Q 99'] if mode == 'c' else []),
    )


for left in [9, 12, 23, 16]:
    for right in [9, 12, 23, 16]:
        for opts in [0, 1, 8, 64, 65, -1]:
            for right_opts in [opts, opts ^ 1, opts ^ 8, opts ^ 64]:
                cases.append(
                    concat_case(
                        setup_leaf(0, left, opts, 'A'), setup_leaf(1, right, right_opts, 'b'), 's'
                    )
                )
for left_text in [None, '', 'a', '\ud800']:
    for b in [None, '', 'b', '\udfff']:
        for rtl in [0, 64]:
            cases.append(
                concat_case(setup_leaf(0, 12, rtl, left_text), setup_leaf(1, 12, rtl, b), 's')
            )
for ids in ['0 0', '0 1 0', '0 1 1 0']:
    for kind in [9, 12, 3, 6, 43, 10, 11]:
        for mode in ['s', 'l', 'c']:
            cases.append(
                concat_case(
                    setup_leaf(0, kind, text='a'), setup_leaf(1, 23, text=None), mode, ids=ids
                )
            )
# Flattening preserves mutable parents and the source compaction/merge order.
for root_options in [0, 64]:
    for nested_options in [0, 1, 64, 65]:
        for child_shape in [0, 1, 2]:
            for mode in ['s', 'c']:
                cases.append(
                    row(
                        *setup_leaf(0, 9, char=65),
                        *setup_leaf(1, 12, text='bc'),
                        f'N 2 25 {nested_options} z',
                        'H 2 '
                        + str(child_shape)
                        + (' 0' if child_shape == 1 else ' 0 1' if child_shape == 2 else ''),
                        'N 3 9 0 c 68',
                        f'N 4 25 {root_options} z',
                        'H 4 2 2 3',
                        'F 2 Parent 4',
                        'F 3 Parent 4',
                        'F 0 Parent 2',
                        'F 1 Parent 2',
                        ('V 4 s' if mode == 's' else 'U 99 4 c'),
                        'Q 4',
                        'Q 2',
                        'Q 0',
                        'Q 1',
                        'Q 3',
                    )
                )
for kind in [25, 9]:
    for shape in [0, 1, 2]:
        for mode in ['s', 'l', 'c']:
            cases.append(
                row(
                    'N 0 9 0 c 65',
                    f'N 1 {kind} 0 z',
                    'H 1 ' + str(shape) + (' 0' if shape else ''),
                    ('U 99 1 c' if mode == 'c' else 'V 1 ' + mode),
                    'Q 1',
                )
            )
loop_kinds = [3, 4, 5, 6, 7, 8, 43, 44, 45, 9, 10, 11, 12, 23, 16]
count_cases = [
    (0, 0, 0, 0),
    (0, 3, 1, 2),
    (1, 3, 0, 2147483647),
    (1, 3, 1, 3),
    (2147483646, 2147483646, 1, 1),
    (2147483647, 2147483647, 0, 0),
    (-1, 3, 1, 2),
    (-2147483648, -2147483648, -2147483648, -2147483648),
]
for left in loop_kinds:
    for right in loop_kinds:
        for m, n, m2, n2 in count_cases:
            for equal in [False, True]:
                cases.append(
                    concat_case(
                        setup_leaf(0, left, text='aaab', char=65, m=m, n=n),
                        setup_leaf(
                            1,
                            right,
                            text='aaab' if equal else 'bbbc',
                            char=65 if equal else 66,
                            m=m2,
                            n=n2,
                        ),
                        'l',
                    )
                )
for kind in [3, 6, 43]:
    for text in [None, '', 'A', 'AAA', 'AAAB', 'AAABB', 'BAA', '\0\0A']:
        for opts in [0, 64]:
            cases.append(
                concat_case(setup_leaf(0, kind, opts, char=65), setup_leaf(1, 12, opts, text), 'l')
            )
for left_text in [None, '', 'a']:
    for b in [None, '', 'a', 'b']:
        cases.append(concat_case(setup_leaf(0, 5, text=left_text), setup_leaf(1, 11, text=b), 'l'))
for last in [9, 12, 23, 16]:
    cases.append(
        row(
            *setup_leaf(0, 3, char=65),
            *setup_leaf(1, 9, char=65),
            *setup_leaf(2, last, text='AAA'),
            *setup_leaf(3, 9, char=66),
            'N 4 25 0 z',
            'H 4 2 0 1 2 3',
            'U 99 4 c',
            'Q 99',
            'Q 4',
            'Q 0',
            'Q 1',
            'Q 2',
            'Q 3',
        )
    )
concatenation_flows = len(cases) - foundation_flows - loop_reduction_flows
# Actual CanBeMadeAtomic: all byte kinds, option equality, nullable counts,
# lazy eligibility, malformed sets and serialized boundary special cases.
word = '\0\0\n\0\2\4\5\3\1\6\t\x13\0'
nonword = '\0\0\n\0\ufffe\ufffc\ufffb\ufffd\uffff\ufffa\ufff7\uffed\0'
sets = [
    None,
    '',
    '\0\2\0AB',
    '\0\2\0BC',
    word,
    nonword,
    '\0\0\1\t',
    '\0\0\1\ufff7',
    '\0\n\0' + '0:A[_`a{\u0130\u0131',
    '\1\n\0' + '0:A[_`a{\u0130\u0131',
    '\0\2\0' + '0:',
]
for kind in range(256):
    for following in range(256):
        cases.append(
            row(
                *setup_leaf(0, kind, text=sets[2], char=65),
                *setup_leaf(1, following, text=sets[3], char=66),
                'W 0 1 1 1',
            )
        )
for kind in [3, 4, 5, 6, 7, 8, 43]:
    for following in [3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 20, 21, 41, 42, 43, 44, 45]:
        for minimum in [-1, 0, 1]:
            for iterate, lazy in [(0, 0), (0, 1), (1, 0), (1, 1)]:
                for text in sets:
                    cases.append(
                        row(
                            *setup_leaf(0, kind, text=text, char=65, m=minimum),
                            *setup_leaf(1, following, text=text, char=65, m=minimum),
                            f'W 0 1 {iterate} {lazy}',
                        )
                    )
for options in [0, 1, 64, 1024, -1]:
    for next_options in [0, 1, 64, 1024, -1]:
        for wrapper in [24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34]:
            for count in [0, 1, 2, 3]:
                cases.append(
                    row(
                        'N 0 3 ' + str(options) + ' c 65',
                        'N 1 9 ' + str(next_options) + ' c 66',
                        f'N 2 {wrapper} {next_options} q 1 3',
                        'H 2 2' + ' 1' * count,
                        'W 0 2 1 1',
                    )
                )
# Parent traversal: climb through pass-through wrappers, continue at the next
# concatenation child, stop at unsupported parent kinds or missing membership.
for parent_kind in range(256):
    for parent_shape in [0, 1, 2]:
        for position in ['1 2', '2 1', '2']:
            cases.append(
                row(
                    'N 0 3 0 c 65',
                    'F 0 M 1',
                    'N 1 3 0 c 66',
                    'F 1 M 0',
                    'N 2 9 0 c 65',
                    f'N 3 {parent_kind} 0 z',
                    'H 3 '
                    + str(parent_shape)
                    + (' 2' if parent_shape == 1 else ' ' + position if parent_shape == 2 else ''),
                    'F 1 Parent 3',
                    'W 0 1 1 0',
                )
            )
for kind in [3, 4, 5, 6, 7, 8]:
    for following in [5, 8, 9, 11, 12, 45]:
        for first in sets:
            for second in sets:
                cases.append(
                    row(
                        *setup_leaf(0, kind, text=first),
                        *setup_leaf(1, following, text=second),
                        'W 0 1 1 1',
                    )
                )
atomic_decision_flows = len(cases) - foundation_flows - loop_reduction_flows - concatenation_flows
for kind in range(256):
    for opts in [0, 64, 1024, -1]:
        for shape in [0, 1, 2]:
            cases.append(
                row(
                    *setup_leaf(0, kind, opts, text=sets[2], char=65),
                    'N 1 9 0 c 66',
                    'H 0 ' + str(shape) + (' 1' if shape else ''),
                    'E 0 a',
                    'Q 0',
                    'Q 1',
                    'E 0 f',
                )
            )
for kind in [3, 4, 5, 6, 7, 8, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34]:
    for last in [3, 4, 5, 6, 7, 8, 9, 11, 12, 21, 23]:
        for minimum in [-1, 0, 1, 2]:
            for opts in [0, 64]:
                cases.append(
                    row(
                        *setup_leaf(0, last, opts, sets[2], 65, minimum, 3),
                        *setup_leaf(1, 9, opts, char=66),
                        f'N 2 {kind} {opts} q 1 3',
                        'H 2 2 0 1',
                        'F 0 Parent 2',
                        'F 1 Parent 2',
                        'N 3 9 0 c 67',
                        f'N 4 25 {opts} z',
                        'H 4 2 2 3',
                        'F 2 Parent 4',
                        'F 3 Parent 4',
                        'E 4 a',
                        'Q 4',
                        'Q 2',
                        'Q 0',
                        'Q 1',
                        'Q 3',
                        'E 2 f',
                    )
                )
# Nested capture/loop expression traversal with real parent edges.
for captures in range(5):
    for loop_kind in [26, 27]:
        for last in [3, 4, 5, 6, 7, 8]:
            ops = [
                *setup_leaf(0, last, text=sets[2], char=65),
                'N 1 9 0 c 66',
                'N 2 25 0 z',
                'H 2 2 1 0',
                'F 0 Parent 2',
                'F 1 Parent 2',
            ]
            current = 2
            for depth in range(captures):
                key = 3 + depth
                ops += [f'N {key} 28 0 z', f'H {key} 1 {current}', f'F {current} Parent {key}']
                current = key
            cases.append(
                row(
                    *ops,
                    f'N 20 {loop_kind} 0 q 1 3',
                    f'H 20 1 {current}',
                    f'F {current} Parent 20',
                    'N 21 9 0 c 67',
                    'N 22 25 0 z',
                    'H 22 2 20 21',
                    'F 20 Parent 22',
                    'F 21 Parent 22',
                    'E 20 f',
                    'E 22 a',
                    'Q 0',
                    'Q 2',
                    'Q 20',
                    'Q 22',
                )
            )
auto_atomic_flows = (
    len(cases)
    - foundation_flows
    - loop_reduction_flows
    - concatenation_flows
    - atomic_decision_flows
)
for left in range(256):
    for right in [9, 11, 22, 23, 24, 25]:
        for mode in ['s', 'r']:
            cases.append(
                row(
                    *setup_leaf(0, left, text=sets[2]),
                    *setup_leaf(1, right, text=sets[3]),
                    'N 2 24 0 z',
                    'H 2 2 0 1',
                    'F 0 Parent 2',
                    'F 1 Parent 2',
                    'O 2 ' + mode,
                    'Q 2',
                    'Q 0',
                    'Q 1',
                )
            )
for left_kind in [9, 11]:
    for right_kind in [9, 11]:
        for first in sets:
            for second in sets:
                for left_options, right_options in [
                    (0, 0),
                    (1, 1),
                    (0, 1),
                    (64, 64),
                    (64, 0),
                    (8, 0),
                    (-1, -1),
                ]:
                    cases.append(
                        row(
                            *setup_leaf(0, left_kind, left_options, first, 65),
                            *setup_leaf(1, right_kind, right_options, second, 66),
                            'N 2 24 0 z',
                            'H 2 2 0 1',
                            'O 2 s',
                            'Q 2',
                            'Q 0',
                            'Q 1',
                            'I 0 1',
                        )
                    )
for shape in [0, 1, 2]:
    for opts in [0, 1, 64, 65, -1]:
        for ids in ['0 1', '0 0', '1 0 1']:
            cases.append(
                row(
                    'N 0 9 0 c 65',
                    'N 1 9 0 c 66',
                    f'N 2 24 {opts} z',
                    'H 2 ' + str(shape) + (' 0' if shape == 1 else ' ' + ids if shape == 2 else ''),
                    'N 3 24 0 z',
                    'H 3 2 0 2 1',
                    'F 0 Parent 2',
                    'F 1 Parent 2',
                    'F 2 Parent 3',
                    'O 3 s',
                    'Q 3',
                    'Q 2',
                    'Q 0',
                    'Q 1',
                )
            )
for kind in [24, 25, 9]:
    for shape in [0, 1, 2]:
        for mode in ['s', 'r']:
            cases.append(
                row(
                    'N 0 9 0 c 65',
                    f'N 1 {kind} 0 z',
                    'H 1 ' + str(shape) + (' 0' if shape else ''),
                    'O 1 ' + mode,
                    'Q 1',
                )
            )
for sequence in ['22 22', '23 23', '22 23 22 23', '9 22 9 23 23 9', '23 9 23 22 9']:
    ops = []
    ids = []
    for i, kind in enumerate(sequence.split()):
        ops += [f'N {i} {kind} 0 c 65']
        ids.append(str(i))
    cases.append(row(*ops, 'N 20 24 0 z', 'H 20 2 ' + ' '.join(ids), 'O 20 r', 'Q 20'))
alternation_helper_flows = (
    len(cases)
    - foundation_flows
    - loop_reduction_flows
    - concatenation_flows
    - atomic_decision_flows
    - auto_atomic_flows
)
for kind in range(256):
    for options in [0, 1, 64, 1024, -1]:
        for mode in ['r', 'a', 't', 'l', 'e', 'b', 'z']:
            for shape in [0, 1, 2]:
                cases.append(
                    row(
                        *setup_leaf(0, kind, options, sets[2]),
                        'N 1 9 0 c 65',
                        'H 0 ' + str(shape) + (' 1' if shape else ''),
                        'F 1 Parent 0',
                        'Y 0 ' + mode,
                        'D 0',
                    )
                )
for kind in range(256):
    for shape in [0, 1, 2]:
        for operation, index in [
            ('a', 0),
            ('i', -1),
            ('i', 0),
            ('i', 1),
            ('i', 2),
            ('r', -1),
            ('r', 0),
            ('r', 1),
            ('r', 2),
            ('r', 2147483647),
        ]:
            cases.append(
                row(
                    'N 0 25 0 z',
                    'N 1 9 0 c 65',
                    *setup_leaf(2, kind, 1, sets[2]),
                    'H 0 ' + str(shape) + (' 1' if shape else ''),
                    f'P 0 {operation} 2 {index}',
                    'D 0',
                )
            )
for operation in ['a', 'i', 'r']:
    cases.append(row('N 0 25 0 z', f'P 0 {operation} -1 0', 'D 0'))
# Prefix extraction followed by re-adoption and atomic wrapping: no child cycles.
for words in [
    ('apple', 'apply'),
    ('ab', 'ac', 'ad'),
    ('ab', 'b', 'ac', 'ad'),
    ('a', 'abc', 'abd'),
    ('abc', 'abc'),
    ('abc', 'abd', 'xyz', 'xyq'),
    ('', 'a'),
    ('a', ''),
]:
    for options in [0, 1, 64, 65, 1024]:
        for atomic in [False, True]:
            ops = []
            ids = []
            for i, word in enumerate(words):
                ops += [f'N {i} 12 {options} s ' + enc(word)]
                ids.append(str(i))
            ops += [f'N 20 24 {options} z', 'H 20 2 ' + ' '.join(ids)]
            for i in ids:
                ops += [f'F {i} Parent 20']
            if atomic:
                ops += [f'N 21 32 {options} z', 'H 21 1 20', 'F 20 Parent 21']
            cases.append(row(*ops, ('Y 21 r' if atomic else 'Y 20 r'), 'D 20'))
for first_kind in [3, 4, 5, 6, 7, 8, 9, 10, 11, 43, 44, 45]:
    for minimum, maximum in [(0, 0), (1, 1), (2, 2), (1, 3)]:
        for options in [0, 64]:
            for atomic in [False, True]:
                ops = []
                for branch in range(3):
                    prefix = branch * 3
                    tail = prefix + 1
                    concat = prefix + 2
                    ops += [
                        *setup_leaf(prefix, first_kind, options, sets[2], 65, minimum, maximum),
                        f'N {tail} 9 {options} c ' + str(66 + branch),
                        f'N {concat} 25 {options} z',
                        f'H {concat} 2 {prefix} {tail}',
                        f'F {prefix} Parent {concat}',
                        f'F {tail} Parent {concat}',
                    ]
                ops += [
                    f'N 20 24 {options} z',
                    'H 20 2 2 5 8',
                    'F 2 Parent 20',
                    'F 5 Parent 20',
                    'F 8 Parent 20',
                ]
                if atomic:
                    ops += [f'N 21 32 {options} z', 'H 21 1 20', 'F 20 Parent 21']
                cases.append(row(*ops, ('Y 21 r' if atomic else 'Y 20 r'), 'D 20'))
for wrapper in [24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34]:
    for leaf in [3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 22, 23, 43, 44, 45]:
        for count in [0, 1, 2, 3, 4]:
            for options in [0, 1, 64, 1024]:
                ops = []
                for i in range(count):
                    ops += setup_leaf(i, leaf, options, sets[2], 65 + i, 0 if i % 2 else 1, 3)
                ops += [
                    f'N 20 {wrapper} {options} q 1 3',
                    'H 20 2' + ' '.join([''] + [str(i) for i in range(count)] if count else []),
                ]
                for i in range(count):
                    ops += [f'F {i} Parent 20']
                cases.append(row(*ops, 'Y 20 r', 'D 20'))
# Parser-style bottom-up construction through the real adoption/reduce path.
rng = random.Random(784029)
for trial in range(1800):
    ops = []
    serial = [0]

    def make(depth):
        key = serial[0]
        serial[0] += 1
        kind = rng.choice(
            [3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 22, 23]
            if depth == 0
            else [9, 11, 12, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34]
        )
        options = rng.choice([0, 0, 0, 64, 1024])
        minimum, maximum = rng.choice([(0, 1), (0, 3), (1, 1), (1, 3), (2, 5)])
        ops.extend(
            setup_leaf(
                key,
                kind,
                options,
                (
                    rng.choice(sets[2:4])
                    if kind in [5, 8, 11]
                    else rng.choice(['a', 'ab', 'ac', 'b', 'ba'])
                ),
                rng.choice([65, 66, 67]),
                minimum,
                maximum,
            )
        )
        children = (
            0
            if kind < 24
            else (
                rng.randint(2, 4)
                if kind in [24, 25]
                else rng.randint(1, 2) if kind == 33 else 3 if kind == 34 else 1
            )
        )
        for unused in range(children):
            child = make(depth - 1)
            ops.append(f'P {key} a {child} 0')
        return key

    root = make(3)
    cases.append(row(*ops, f'Y {root} r', f'D {root}'))
integrated_reduction_flows = (
    len(cases)
    - foundation_flows
    - loop_reduction_flows
    - concatenation_flows
    - atomic_decision_flows
    - auto_atomic_flows
    - alternation_helper_flows
)
for kind in range(256):
    for lazy in [0, 1]:
        for minimum, maximum in [
            (-1, -1),
            (0, 0),
            (1, 1),
            (2, 2),
            (64, 64),
            (65, 65),
            (0, 1),
            (1, 3),
            (0, 2147483647),
            (-1, 3),
        ]:
            cases.append(
                row(*setup_leaf(0, kind, 1, sets[2]), f'K 0 {lazy} {minimum} {maximum}', 'D 0')
            )
for kind in range(256):
    for options in [0, 1, 64, 1024]:
        for shape in [0, 1, 2]:
            cases.append(
                row(
                    *setup_leaf(0, kind, options, sets[2]),
                    'N 1 9 0 c 65',
                    'H 0 ' + str(shape) + (' 1' if shape else ''),
                    'F 1 Parent 0',
                    'B 0',
                    'D 0',
                )
            )
for kind in [3, 4, 5, 6, 7, 8, 43, 44, 45]:
    for options in [0, 1, 64, 1024]:
        for minimum in [0, 1, 2]:
            for maximum in [3, 2147483647]:
                for atomic in [False, True]:
                    ops = [
                        *setup_leaf(0, kind, options, sets[2], 65, minimum, maximum),
                        'N 1 9 0 c 66',
                        f'N 2 25 {options} z',
                        'H 2 2 0 1',
                        'F 0 Parent 2',
                        'F 1 Parent 2',
                    ]
                    current = 2
                    if atomic:
                        ops += [f'N 3 32 {options} z', 'H 3 1 2', 'F 2 Parent 3']
                        current = 3
                    cases.append(
                        row(
                            *ops,
                            f'N 4 28 {options} z',
                            f'H 4 1 {current}',
                            f'F {current} Parent 4',
                            'B 4',
                            'D 4',
                        )
                    )
pre_ordinal_flows = len(cases)
for kind in range(256):
    for text in [None, '', 'ab', '12', '\u00df', '\0\4\0ABab', '\0\4\0Aaab']:
        for minimum in [-2147483648, -2, -1, 0, 1, 2, 3]:
            cases.append(
                row(
                    *setup_leaf(0, kind, text=text, m=minimum, n=minimum),
                    'N 1 25 0 z',
                    'H 1 2 0',
                    'S 1 0 1 1',
                    'Q 0',
                )
            )
for kind in [9, 12, 11, 5, 8, 45, 14, 15, 16, 17, 18, 19, 20, 21, 23, 30, 31, 41, 42, 46]:
    for zero in [0, 1]:
        for start, bound in [(-1, 2), (0, 0), (0, 1), (0, 2), (0, 3), (1, 3), (3, 1)]:
            cases.append(
                row(
                    *setup_leaf(0, kind, text='\0\4\0ABab', char=49, m=2, n=2),
                    'N 1 12 0 s ' + enc('12'),
                    'N 2 9 0 c 50',
                    'N 3 25 0 z',
                    'H 3 2 0 1 2',
                    f'S 3 {start} {bound} {zero}',
                )
            )
(w / 'input.txt').write_text('\n'.join(cases) + '\n')
outputs = {}
contract = subprocess.run(
    [str(Path(a.native).resolve()), '--policy-contract'], capture_output=True, text=True
)
(w / 'native-policy-contract.txt').write_text(contract.stdout + contract.stderr)
assert contract.returncode == 0, contract.stderr
for name, cmd in [
    ('reference', [a.dotnet, str(w / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    r = subprocess.run(
        cmd, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=180
    )
    (w / (name + '.txt')).write_text(r.stdout)
    (w / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (name, r.stderr)
    outputs[name] = r.stdout.splitlines()
    assert len(outputs[name]) == len(cases), (name, len(outputs[name]), len(cases))
diff = [
    dict(index=i, input=cases[i], reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not diff,
    flows=len(cases),
    oneCharFactories=65536,
    reductionFlows=len(cases) - foundation_flows,
    concatenationFlows=concatenation_flows,
    atomicDecisionFlows=atomic_decision_flows,
    autoAtomicFlows=auto_atomic_flows,
    alternationHelperFlows=alternation_helper_flows,
    integratedReductionFlows=integrated_reduction_flows,
    finalOptimizeQuantifierFlows=pre_ordinal_flows
    - foundation_flows
    - loop_reduction_flows
    - concatenation_flows
    - atomic_decision_flows
    - auto_atomic_flows
    - alternation_helper_flows
    - integrated_reduction_flows,
    ordinalStringFlows=len(cases) - pre_ordinal_flows,
    differences=len(diff),
    firstDifferences=diff[:10],
    applicationParityVerified=False,
    scope='Pinned RegexNode constructors, family predicates, case-converting factory, child representations and parent identity, length bounds, literal prefixes, Set/group/nested-loop/concatenation reductions, adjacent string and loop merging, arity collapse, atomic leaf-loop mutation, auto-atomic eligibility and recursive optimizer, alternation flattening/class union and redundant empty/Nothing branches, and compilation depth. Full Reduce dispatcher, atomic/alternation/lookaround/conditional and ending-backtracking reductions plus AddChild/InsertChild/ReplaceChild are ported; graph snapshots cover new allocations and parent/text identity. FinalOptimize and MakeQuantifier are ported; parser/runner remain unported. Stack policy explicit; bounded source graphs use a true native policy; the native guard false/missing contract is checked separately, without stack-boundary equivalence claims.',
    outputSHA256={
        name: hashlib.sha256(('\n'.join(value) + '\n').encode()).hexdigest()
        for name, value in outputs.items()
    },
)
(w / 'results.json').write_text(json.dumps(report, indent=2))
assert report['passed'], report
print(f'PASS: {len(cases)} actual .NET regex node flows')
