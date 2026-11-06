"""Actual .NET8 FileSystemName raw-UTF16/identity/NFA comparison, no regex oracle."""

import argparse, hashlib, itertools, json, os, random, re, subprocess, time, xml.etree.ElementTree as ET
from pathlib import Path
from compare_invariant_datetime_parse import Peer, encoded

ROOT = Path(__file__).resolve().parents[2]


def compile_oracle(dotnet, work):
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(ROOT / 'native-port/tests/file_system_name_reference.cs'),
    )
    file = work / 'FileSystemNameReference.csproj'
    ET.ElementTree(project).write(file, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            dotnet,
            'build',
            str(file),
            '--configuration',
            'Release',
            '-o',
            str(work / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert not r.returncode, r.stdout + r.stderr
    return [dotnet, str(work / 'app/FileSystemNameReference.dll')]


def get_profile(peer):
    peer.send('PROFILE')
    chars = {}
    ordinal = {}
    while (line := peer.read()) != 'END':
        a = line.split('|')
        source, mapped = int(a[1], 16), int(a[2], 16)
        if a[0] == 'C':
            chars[source] = mapped
        else:
            assert a[3] == '1', line
            ordinal[source] = mapped
    return dict(chars=chars, ordinal=ordinal)


def table(header, name):
    text = re.search(r'\b' + name + r'=\{\{(.*?)\}\};', header, re.S)
    assert text, name
    return {
        int(a, 16): int(b, 16)
        for a, b in re.findall(r'\{\{0x([0-9a-f]+),0x([0-9a-f]+)\}\}', text.group(1))
    }


def strings(alphabet, max_length):
    for n in range(max_length + 1):
        for p in itertools.product(alphabet, repeat=n):
            yield ''.join(p)


def match_rows(expression, name):
    for kind, ignore in itertools.product(['s', 'w'], ['0', '1']):
        yield 'M ' + kind + ' ' + ignore + ' ' + encoded(expression) + ' ' + encoded(name)


def corpus(profile, quick):
    alphabet = ['a', '.', '*', '?', '<', '>', '"', '\\', '\0']
    translation = list(strings(alphabet, 2 if quick else 4)) + [
        None,
        '*.*',
        'a.b',
        'prefix\\*.',
        '\ud800?.\udfff',
        'a' * 10000,
        '*.' * 1000,
        '?.' * 1000,
    ]
    rng = random.Random(335512)
    for _ in range(100 if quick else 2000):
        translation.append(
            ''.join(
                rng.choice(
                    alphabet + ['\u017f', '\u0131', '\u03c2', '\ud800', '\udfff', '\U00010428']
                )
                for _ in range(rng.randrange(120))
            )
        )
    for text in translation:
        for flag in ['', ' intern']:
            yield 'T ' + encoded(text) + flag
    patterns = list(strings(alphabet, 2 if quick else 3))
    names = list(strings(['a', 'A', '.', '\0'], 2 if quick else 3))
    for expression, name in itertools.product(patterns, names):
        yield from match_rows(expression, name)
    units = range(256) if quick else range(65536)
    for unit in units:
        c = chr(unit)
        upper = chr(profile['chars'].get(unit, unit))
        ordinal = chr(profile['ordinal'].get(unit, unit))
        yield from match_rows(c, upper)
        yield from match_rows('*' + c, 'p' + ordinal)
    for source, mapped in profile['ordinal'].items():
        if source <= 65535:
            continue
        a, b = chr(source), chr(mapped)
        ab = a.encode('utf-16-le')
        bb = b.encode('utf-16-le')
        ahi, alo = chr(int.from_bytes(ab[:2], 'little')), chr(int.from_bytes(ab[2:], 'little'))
        bhi, blo = chr(int.from_bytes(bb[:2], 'little')), chr(int.from_bytes(bb[2:], 'little'))
        for expression, name in [
            (a, b),
            ('*' + a, 'p' + b),
            ('**' + a, 'p' + b),
            ('*' + a + '?', 'p' + b + 'x'),
            ('?' + a, 'x' + b),
            (a + '*', b + 'x'),
            ('*' + a, '\ud800x' + b),
            ('*' + a, '\ud800' + b[-1:]),
            ('*' + a, b),
            ('*' + alo, b),
            ('*' + ahi, 'x' + bhi),
            ('*' + alo + ahi, blo + bhi),
            ('*' + ahi + '\0' + alo, bhi + '\0' + blo),
        ]:
            yield from match_rows(expression, name)
    special = [
        '',
        'a',
        '.',
        '..',
        'a.b',
        'a..b',
        '\0',
        '\ud800',
        '\udfff',
        '\ud800x',
        'x\udfff',
        '\u017f',
        's',
        '\u0131',
        'I',
        '\U00010400',
        '\U00010428',
    ]
    for e, n in itertools.product(special, special):
        for prefix, suffix in [('', ''), ('*', ''), ('**', ''), ('\\', ''), ('*', '?')]:
            yield from match_rows(prefix + e + suffix, n)
    pieces = alphabet + [
        'A',
        'b',
        '\n',
        '\t',
        '\u017f',
        'S',
        '\u0131',
        'I',
        '\u00e9',
        '\u00c9',
        '\u03c2',
        '\u03a3',
        '\ud800',
        '\udfff',
        '\udbff',
        '\udc00',
        '\U00010400',
        '\U00010428',
    ]
    for _ in range(300 if quick else 18000):
        expression = ''.join(rng.choice(pieces) for _ in range(rng.randrange(65)))
        name = ''.join(rng.choice(pieces) for _ in range(rng.randrange(90)))
        yield from match_rows(expression, name)
    for size in (
        [7, 8, 16, 32] if quick else [7, 8, 15, 16, 17, 31, 32, 63, 64, 127, 128, 257, 512, 1024]
    ):
        for expression in [
            '*' * size,
            '*a' * size + 'b',
            '*?' * size + 'X',
            '<a' * size + '"',
            '<>' * size + '"',
            '**a?' * size + '*',
            '*\\?' * size + 'b',
        ]:
            for name in [
                'a' * size + 'b',
                'a' * (size * 2) + 'X',
                'a.' * size,
                'a\0' * size,
                'a' * (size - 1),
                'a' * (size + 1),
            ]:
                yield from match_rows(expression, name)


def write_case_data(dotnet, oracle, work):
    profiles = {}
    metadata = {}
    for mode in ['0', '1']:
        env = os.environ.copy()
        env['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT'] = mode
        p = Peer(oracle, env)
        try:
            metadata[mode] = p.ask('META')
            profiles[mode] = get_profile(p)
        finally:
            p.close()
    header = '// Generated actual .NET8.0.31 casing profiles for FileSystemName.\n// char.ToUpperInvariant(code unit) and Ordinal.ToUpperOrdinal(scalar) are\n// intentionally separate. No CLR runtime is used by the native matcher.\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace opensim::framework::file_system_name_case_data {\n'
    for mode, data in profiles.items():
        for key, rows in data.items():
            header += (
                'inline constexpr std::array<std::array<std::uint32_t,2>,'
                + str(len(rows))
                + '> '
                + key
                + ('_normal' if mode == '0' else '_invariant')
                + '={{\n'
                + ''.join(' {{0x%x,0x%x}},\n' % (a, b) for a, b in rows.items())
                + '}};\n'
            )
    header += '}\n'
    target = ROOT / 'native-port/include/opensim/framework/file_system_name_case_data.hpp'
    target.write_text(header)
    (work / 'case-profile-results.json').write_text(
        json.dumps(
            dict(metadata=metadata, headerSHA256=hashlib.sha256(target.read_bytes()).hexdigest()),
            indent=2,
        )
        + '\n'
    )
    print('Generated pinned case data; rebuild native matcher before comparison')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native')
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--globalization-invariant', action='store_true')
    p.add_argument('--write-case-data', action='store_true')
    a = p.parse_args()
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    oracle = compile_oracle(a.dotnet, work)
    if a.write_case_data:
        write_case_data(a.dotnet, oracle, work)
        return
    assert a.native, '--native required for comparison'
    env = os.environ.copy()
    env['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT'] = '1' if a.globalization_invariant else '0'
    source = Peer(oracle, env)
    native = Peer([a.native, '--probe', 'invariant' if a.globalization_invariant else 'normal'])
    started = time.monotonic()
    differences = []
    cases = 0
    complete = False
    try:
        metadata = source.ask('META')
        assert metadata.startswith('8.0.31|'), metadata
        profile = get_profile(source)
        header = (
            ROOT / 'native-port/include/opensim/framework/file_system_name_case_data.hpp'
        ).read_text()
        suffix = '_invariant' if a.globalization_invariant else '_normal'
        for key, data in profile.items():
            assert data == table(
                header, key + suffix
            ), 'Selected BCL case backend differs from pinned profile: bind an explicit CasingBackend or regenerate the target profile'
        for query in corpus(profile, a.quick):
            expected = source.ask(query)
            actual = native.ask(query)
            cases += 1
            if actual != expected:
                differences.append(dict(index=cases, query=query, source=expected, native=actual))
                if len(differences) >= 20:
                    break
        else:
            complete = True
        report = dict(
            passed=complete and not differences,
            cases=cases,
            completeCorpus=complete,
            quick=a.quick,
            globalizationInvariant=a.globalization_invariant,
            metadata=metadata,
            runtimeSHA256=hashlib.sha256(Path(metadata.split('|')[1]).read_bytes()).hexdigest(),
            caseProfile=dict(chars=len(profile['chars']), ordinal=len(profile['ordinal'])),
            caseDataSHA256=hashlib.sha256(
                (
                    ROOT / 'native-port/include/opensim/framework/file_system_name_case_data.hpp'
                ).read_bytes()
            ).hexdigest(),
            elapsedSeconds=time.monotonic() - started,
            scope='Actual .NET8.0.31 public FileSystemName APIs; raw UTF16 preserves NUL/unpaired surrogates. Translation includes input identity and canonical-star identity. Full BMP char/Unicode scalar ordinal profiles checked against actual runtime. Exhaustive small wildcard language, all UTF16 units (full run), supplementary case pairs and many NFA array-growth paths. Normal profile is selected native ICU runtime; a different NLS/ICU case backend must be supplied explicitly.',
            differences=differences,
        )
        (work / 'file-system-name-results.json').write_text(json.dumps(report, indent=2) + '\n')
        assert report['passed'], json.dumps(differences[:3])
        print(f'PASS: {cases} actual FileSystemName observations')
    finally:
        source.close()
        native.close()


if __name__ == '__main__':
    main()
