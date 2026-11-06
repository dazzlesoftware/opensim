"""Consumed invariant Convert(string, provider), raw UTF16 and direct float bits."""

import argparse, fractions, hashlib, itertools, json, random, struct, subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def call(command, rows=None):
    r = subprocess.run(
        command,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=180,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr[-2000:])
    return r.stdout.splitlines()


def encoded(value):
    return '-' if value is None else value.encode('utf-16-le', 'surrogatepass').hex() or '.'


def decimal(value):
    sign = '-' if value < 0 else ''
    value = abs(value)
    power = value.denominator.bit_length() - 1
    assert value.denominator == 1 << power
    digits = str(value.numerator * 5**power)
    if not power:
        return sign + digits
    digits = digits.rjust(power + 1, '0')
    return sign + digits[:-power] + '.' + digits[-power:]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(
        group,
        'Compile',
        Include=str(ROOT / 'native-port/tests/invariant_number_parse_reference.cs'),
    )
    cs = w / 'InvariantNumberReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    rows = []

    def add(text, ops=range(4)):
        for op in ops:
            rows.append(str(op) + ' ' + encoded(text))

    for value in [
        None,
        '',
        ' ',
        '+',
        '-',
        '0',
        '-0',
        '+0',
        'NaN',
        'nan',
        'NAN',
        '+NaN',
        '-NaN',
        'Infinity',
        '-Infinity',
        '+Infinity',
        'INF',
        '∞',
        '1,',
        '1,,',
        '1,,e2',
        '.1',
        '1.',
        '1.e-2',
        '1\0',
        '1 \0\0',
        '1\0 ',
        '1e1000',
        '1e1001',
        '1e9999',
        '0e99999999999999',
        '-0e99999999999999',
        '0.' + '0' * 10000 + '1e1001',
        '1' + '0' * 10000 + 'e-1001',
        '0.' + '0' * 11000 + '1e100000',
    ]:
        add(value)
    for bits in [31, 63]:
        for n in [
            -(1 << bits) - 1,
            -(1 << bits),
            -(1 << bits) + 1,
            -1,
            0,
            1,
            (1 << bits) - 2,
            (1 << bits) - 1,
            1 << bits,
        ]:
            for prefix, suffix in itertools.product(
                ['', '+', '-', ' \t'], ['', ' ', '\0', ' \0', '\0\0', 'x', '\0 ']
            ):
                add(prefix + str(n) + suffix)
    # Exhaust every code unit in numeric, special-symbol and whitespace positions.
    for code in range(65536):
        c = chr(code)
        add('1' + c + '2')
        add(c + 'NaN' + c, [2, 3])
    # Short grammar combinations include failed exponent rollback and group runs.
    for size in range(4):
        for chars in itertools.product('01e+-,. \0', repeat=size):
            add(''.join(chars))
    # Exact binary midpoint decimal strings and one decimal unit on either side.
    rng = random.Random(70105)
    for op, width, fmt, maximum in [(2, 4, 'f', 0x7F7FFFFF), (3, 8, 'd', 0x7FEFFFFFFFFFFFFF)]:
        points = [0, 1, 2, 3, maximum - 1, maximum] + [rng.randrange(maximum) for _ in range(500)]
        points += [
            bits
            for exponent in range(1, 255 if width == 4 else 2047, 3 if width == 4 else 17)
            for bits in [
                (exponent << (23 if width == 4 else 52)) - 1,
                exponent << (23 if width == 4 else 52),
            ]
        ]
        for bits in points:
            x = struct.unpack('<' + fmt, bits.to_bytes(width, 'little'))[0]
            add(repr(x), [op])
            add('-' + repr(x), [op])
            if bits == maximum:
                continue
            y = struct.unpack('<' + fmt, (bits + 1).to_bytes(width, 'little'))[0]
            middle = (fractions.Fraction(x) + fractions.Fraction(y)) / 2
            exact = decimal(middle)
            scale = len(exact.partition('.')[2]) + 10
            unit = fractions.Fraction(1, 10**scale)
            # Decimal unit has factors5 too, so format these through scaled integers.
            for delta in [-1, 0, 1]:
                scaled = middle * 10**scale + delta
                assert scaled.denominator == 1
                digits = str(scaled.numerator).rjust(scale + 1, '0')
                text = digits[:-scale] + '.' + digits[-scale:]
                add(text, [op])
                add('-' + text, [op])
    for _ in range(10000):
        add(''.join(rng.choices('0123456789eE+-,. \t\0\ud800\udc00', k=rng.randrange(40))))
    outputs = [
        call([a.dotnet, str(w / 'app/InvariantNumberReference.dll')], rows),
        call([a.native, '--probe'], rows),
    ]
    assert len(outputs[0]) == len(outputs[1]) == len(rows)
    diff = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, *outputs))
        if s != n
    ]
    report = dict(
        passed=not diff,
        cases=len(rows),
        allCodeUnits=65536,
        referenceSHA256=hashlib.sha256(
            (ROOT / 'native-port/tests/invariant_number_parse_reference.cs').read_bytes()
        ).hexdigest(),
        scope='Actual invariant Convert.ToInt32/Int64/Single/Double string overloads; raw UTF16 error messages, null-to-zero, integer bounds, every code unit, special-symbol trimming, direct IEEE rounding midpoint triples and exponent saturation. Fixed invariant NumberFormatInfo only; arbitrary cultures, styles and object/IConvertible overloads are outside this consumed closure.',
        differences=diff[:5],
    )
    (w / 'invariant-number-parse-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:2])
    print('PASS:', len(rows), 'invariant numeric comparisons')


if __name__ == '__main__':
    main()
