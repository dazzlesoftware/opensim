import argparse, json, pathlib, random, subprocess

p = argparse.ArgumentParser()
p.add_argument('--native')
p.add_argument('--dotnet')
p.add_argument('--reference')
p.add_argument('--work', type=pathlib.Path)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)


def h(s):
    return '~' if s is None else s.encode('utf-16-be', 'surrogatepass').hex() or '-'


rows = []


def flow(ops, fail=0, size=-1, leave=0, write=1, seek=1, pos=0):
    rows.append(f'{write},{seek},{pos},{fail},{size},{leave};' + ';'.join(ops))


for text in [
    None,
    '',
    'a',
    'abcd',
    'abcde',
    '<&>',
    '\ud800',
    '\udc00',
    '\ud800\udc00',
    'a\ud800b',
    'x' * 127,
    'x' * 128,
    'x' * 129,
    'x' * 1023,
    'x' * 1024,
    'x' * 1025,
    '\ud800\udc00' * 600,
]:
    for kind in ['S', 'H', 'P']:
        for fail in range(8):
            for size in [1, 128, 1024]:
                flow(
                    [kind + ',' + h(text), 'F', 'X', kind + ',' + h(text), 'C,0061', 'F', 'X'],
                    fail,
                    size,
                )
for write in [-1, 0, 1]:
    for seek in [0, 1]:
        for pos in [-1, 0, 1]:
            for size in [-2, -1, 0, 1, 128, 1024]:
                flow(['L', 'F', 'X'], size=size, write=write, seek=seek, pos=pos)
for text in [None, '', 'abc']:
    for index in [-2, -1, 0, 1, 3, 4, 2147483647]:
        for count in [-2, -1, 0, 1, 3, 4, 2147483647]:
            flow([f'A,{h(text)},{index},{count}', 'X', f'A,{h(text)},{index},{count}'])
for leave in [0, 1]:
    for newline in [None, '', '\n', '\r\n', 'x' * 1100]:
        for fail in range(15):
            flow(
                [
                    'N,' + h(newline),
                    'AF,1',
                    'S,' + h('x' * 1023),
                    'L',
                    'AF,0',
                    'X',
                    'S,-',
                    'L',
                    'C,0061',
                    'F',
                    'X',
                ],
                fail=fail,
                leave=leave,
            )
rng = random.Random(7191)
ops = ['S,' + h(x) for x in ['', 'a', 'x' * 127, 'x' * 1024, '\ud800', '\udc00']] + [
    'C,0061',
    'C,d800',
    'C,dc00',
    'L',
    'F',
    'AF,0',
    'AF,1',
    'X',
]
for _ in range(1500):
    flow(
        rng.choices(ops, k=12),
        fail=rng.randrange(18),
        size=rng.choice([1, 128, 1024]),
        leave=rng.randrange(2),
    )
for size in [128, 1024]:
    for trigger in range(1, 13):
        for action in ['X', 'F', 'AF0', 'AF1', 'S1', 'S128', 'S1024', 'NL']:
            for method in ['S', 'H', 'P', 'A']:
                for payload_size in [size - 1, size, size + 1, size * 2 + 1]:
                    text = h('a' * payload_size)
                    command = f'A,{text},0,{payload_size}' if method == 'A' else method + ',' + text
                    commands = [command, 'L', 'C,0061', 'F', 'X', 'S,-', 'F']
                    rows.append(f'1,1,0,0,{size},0,{trigger},{action};' + ';'.join(commands))

for original in list(rows):
    header, commands = original.split(';', 1)
    fields = header.split(',')
    fields[4] = '-1'
    fields[5] = '0'
    while len(fields) < 8:
        fields.append('0' if len(fields) == 6 else '#')
    fields.extend(['#', '1'])
    rows.append(','.join(fields) + ';' + commands)

for size in [1023, 1024, 1025, 2049, 5000]:
    for trigger in range(4, 11):
        for action in ['S1', 'S128', 'S1024', 'F', 'X']:
            text = h('a' * size)
            tail = h('tail')
            rows.append(f'1,1,0,0,-1,0,{trigger},{action},#,1;S,{text};S,{tail};F;X;S,-;F')

payload = '\n'.join(rows) + '\n'
(a.work / 'inputs.txt').write_text(payload)


def run(cmd, name):
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True)
    (a.work / (name + '.txt')).write_text(r.stdout)
    (a.work / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (r.returncode, r.stderr)
    lines = r.stdout.splitlines()
    assert len(lines) == len(rows), (len(lines), len(rows))
    return lines


expected = run([a.dotnet, a.reference], 'managed')
actual = run([a.native, '--probe'], 'native')
diff = [
    {'input': row, 'expected': e, 'actual': n}
    for row, e, n in zip(rows, expected, actual)
    if e != n
]
report = {'flows': len(rows), 'differences': len(diff), 'examples': diff[:15]}
(a.work / 'results.json').write_text(json.dumps(report, indent=2))
print(len(rows), 'StreamWriter flows;', len(diff), 'differences')
assert not diff
