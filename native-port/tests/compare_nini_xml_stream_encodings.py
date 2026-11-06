import argparse, json, pathlib, re, subprocess, random

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


def row(enc, commands, size=128, fail=0, write=1, pos=0, seek=1):
    name_tag = '#' if enc == '__default__' else h(enc)
    rows.append(f'{write},{seek},{pos},{fail},{size},0,0,#,{name_tag};' + ';'.join(commands))


canonical = [
    '__default__',
    'utf-8',
    'utf-16',
    'utf-16be',
    'utf-32',
    'utf-32be',
    'us-ascii',
    'iso-8859-1',
]
# Pinned runtime8.0.31 EncodingTable names; independent source-derived input corpus.
aliases = [
    'ansi_x3.4-1968',
    'ansi_x3.4-1986',
    'ascii',
    'cp367',
    'cp819',
    'csascii',
    'csisolatin1',
    'csunicode11utf7',
    'ibm367',
    'ibm819',
    'iso-10646-ucs-2',
    'iso-8859-1',
    'iso-ir-100',
    'iso-ir-6',
    'iso646-us',
    'iso8859-1',
    'iso_646.irv:1991',
    'iso_8859-1',
    'iso_8859-1:1987',
    'l1',
    'latin1',
    'ucs-2',
    'unicode',
    'unicode-1-1-utf-7',
    'unicode-1-1-utf-8',
    'unicode-2-0-utf-7',
    'unicode-2-0-utf-8',
    'unicodefffe',
    'us',
    'us-ascii',
    'utf-16',
    'utf-16be',
    'utf-16le',
    'utf-32',
    'utf-32be',
    'utf-32le',
    'utf-7',
    'utf-8',
    'x-unicode-1-1-utf-7',
    'x-unicode-1-1-utf-8',
    'x-unicode-2-0-utf-7',
    'x-unicode-2-0-utf-8',
]
for enc in (
    aliases
    + [s.upper() for s in aliases]
    + [
        None,
        '',
        'unknown',
        ' UTF-8',
        'utf-8 ',
        'utf-8\0',
        'utf\u017f-8',
        'utf-\ud800',
        'ＵＴＦ-８',
        'windows-1252',
    ]
):
    for write in [-1, 0, 1]:
        for fail in [0, 1, 2, 3, 4, 5, 6]:
            row(
                enc,
                [
                    'W',
                    'B',
                    'M,-1',
                    'M,0',
                    'M,715827881',
                    'M,715827882',
                    'M,1073741822',
                    'M,1073741823',
                    'M,2147483646',
                    'M,2147483647',
                    'S,' + h('éĀ€𐀀\ud800'),
                    'F',
                    'X',
                ],
                fail=fail,
                write=write,
            )
for enc in canonical:
    for size in [128, 1024]:
        row(enc, ['S,' + h(''.join(chr(i) for i in range(65536))), 'F', 'X'], size=size)
        for text in ['', 'éĀ€𐀀\ud800', '\ud800\udc00', '\ud800a', '\udc00', '\ud800\ud800\udc00']:
            for kind in ['S', 'H', 'P']:
                for fail in range(10):
                    row(
                        enc,
                        [kind + ',' + h(text), 'AF,1', 'C,d800', 'C,dc00', 'AF,0', 'F', 'X'],
                        size=size,
                        fail=fail,
                    )
        for n in [127, 128, 129, 1023, 1024, 1025]:
            row(
                enc,
                ['S,' + h('a' * (n - 1) + '\ud800'), 'S,' + h('\udc00' + 'b' * n), 'F', 'X'],
                size=size,
            )
    for alias in aliases:
        row(enc, ['R,' + h(alias), 'X'])
for enc in canonical:
    for pos, seek in [(0, 1), (1, 1), (0, 0)]:
        row(enc, ['F', 'X'], pos=pos, seek=seek)
row('__default__', ['W', 'B', 'M,-1', 'M,0', 'M,715827881', 'M,715827882', 'M,2147483647', 'X'])
payload = '\n'.join(rows) + '\n'
(a.work / 'inputs.txt').write_text(payload)


def run(cmd, name):
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True)
    (a.work / (name + '.txt')).write_text(r.stdout)
    (a.work / (name + '-error.txt')).write_text(r.stderr)
    assert r.returncode == 0, (r.returncode, r.stderr)
    v = r.stdout.splitlines()
    assert len(v) == len(rows), (len(v), len(rows))
    return v


expected = run([a.dotnet, a.reference], 'managed')
actual = run([a.native, '--probe'], 'native')
diff = [
    {'input': row, 'expected': e, 'actual': n}
    for row, e, n in zip(rows, expected, actual)
    if e != n
]
report = {
    'flows': len(rows),
    'differences': len(diff),
    'examples': diff[:15],
    'scope': 'Intrinsic Encoding.GetEncoding aliases, replacement/best-fit/preamble/identity and GetMaxByteCount; all BMP code units plus paired/split surrogate/StreamWriter fault flows. UTF7 default-disabled; explicit custom-provider/alternate policy support remains separate.',
}
(a.work / 'results.json').write_text(json.dumps(report, indent=2))
print(len(rows), 'intrinsic encoding flows;', len(diff), 'differences')
assert not diff
