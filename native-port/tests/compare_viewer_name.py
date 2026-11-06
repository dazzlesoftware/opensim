"""Original utility/actual DTO and invariant-culture Unicode reference profile; native runtime is C++ only."""

import argparse, hashlib, itertools, json, subprocess
from pathlib import Path
from compare_agent_data import ROOT, build_reference
from compare_presence_endpoint import extract


def units(s):
    return s.encode('utf-16-be', errors='surrogatepass').hex()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native')
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--write-profile', type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/Util.cs'
    method = extract(
        source.read_text(encoding='utf-8-sig'), r'public static string GetViewerName\('
    )
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using OpenSim.Framework;public partial class ViewerReference{' + method + '}',
        encoding='utf-8',
    )
    assembly = build_reference(
        a.dotnet,
        w,
        [
            wrapper,
            ROOT / 'native-port/tests/viewer_name_reference.cs',
            ROOT / 'OpenSim/Framework/AgentCircuitData.cs',
        ],
        'ViewerReference',
    )

    def run(cmd, text=None):
        r = subprocess.run(cmd, input=text, capture_output=True, text=True, timeout=180)
        if r.returncode:
            raise RuntimeError(r.stdout[-1000:] + r.stderr)
        return r.stdout.splitlines()

    managed = [a.dotnet, str(assembly)]
    profile = run(managed + ['profile'])
    if a.write_profile:
        bits = bytes.fromhex(profile[1].split(':')[1])
        ranges = []
        start = None
        for i in range(65537):
            active = i < 65536 and bits[i // 8] & (1 << (i % 8))
            if active and start is None:
                start = i
            if not active and start is not None:
                ranges.append((start, i - 1))
                start = None
        lower = []
        for line in profile[2:]:
            _, cp, value = line.split(':')
            text = bytes.fromhex(value).decode('utf-16-be', errors='surrogatepass')
            assert len(text) == 1
            lower.append((int(cp, 16), ord(text)))
        header = (
            '// Generated reference observations from .NET '
            + profile[0].split(':')[1]
            + '.\n// Regenerate with tests/compare_viewer_name.py --write-profile; no managed runtime is used by the native library.\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace opensim::framework::viewer_unicode {\n'
        )
        for name, values in [('letters', ranges), ('lower', lower)]:
            header += (
                f'inline constexpr std::array<std::array<std::uint32_t,2>,{len(values)}> {name}={{{{\n'
                + ''.join('    {{0x%x,0x%x}},\n' % pair for pair in values)
                + '}};\n'
            )
        a.write_profile.write_text(header + '}\n', encoding='utf-8')
        print(f'Generated {len(ranges)} letter ranges and {len(lower)} lower mappings')
        return
    assert a.native, '--native is required for verification'
    native_profile = run([a.native, 'profile'])
    assert profile[1:] == native_profile[1:], 'Unicode profile differs'
    source_all = run(managed + ['all'])
    native_all = run([a.native, 'all'])
    assert source_all == native_all, (source_all, native_all)
    values = [
        None,
        '',
        'Viewer',
        'VIEWER 1',
        '4.5.1.38838',
        ' 4. 5 ',
        '123',
        ' 123 ',
        '123\t456',
        '試験',
        '😀',
        '𐐀',
        '\u0301',
        '\u2160',
        '\u00b2',
        'Firestorm-Beta',
        '\u00a0Viewer\u3000',
        '\t\n',
        '\x00123',
        'Viewer\x00',
    ]
    cases = [
        {'viewer': viewer, 'channel': channel}
        for viewer, channel in itertools.product(values, values)
    ] + [{'null_agent': True}]
    commands = [
        json.dumps(c, ensure_ascii=False, separators=(',', ':')).encode().hex() for c in cases
    ]
    texts = [
        '',
        'ASCII ABC',
        'İIıi',
        'ΟΣ ΣΣ Σς',
        'ẞ ß',
        '𐐀𐐨',
        '\ud800',
        '\udc00',
        '\ud800\ud801\udc00',
        '\ud800\udc00\udc00',
        'Α\u0301Σ',
        'I\u0307',
        'KÅℲ',
    ]
    texts += [
        chr(cp) + suffix
        for cp, suffix in itertools.product(
            [0x41, 0x130, 0x131, 0x3A3, 0x1E9E, 0x10400, 0x10C80],
            ['', ' IΣ', '\ud800', '\udc00', '\u0307'],
        )
    ]
    commands += ['L ' + units(s) for s in texts]
    rows = [run(managed, '\n'.join(commands) + '\n'), run([a.native], '\n'.join(commands) + '\n')]
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    (w / 'viewer-name-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'scenarios': len(commands),
                'unicodeScalars': 0x110000 - 2048,
                'surrogateUnits': 2048,
                'codePointValues': 0x110000,
                'charClassifications': 65536,
                'viewerScalarDigest': source_all,
                'runtime': profile[0],
                'profileSha256': hashlib.sha256('\n'.join(profile[1:]).encode()).hexdigest(),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Complete original GetViewerName/actual circuit property and all-scalar invariant-culture string lower mapping plus contextual/raw UTF16 samples. Native UTF8 viewer model cannot represent arbitrary unpaired UTF16; custom cultures/runtime Unicode profiles and full scene/platform integration remain partial.',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(str(diffs[:3]))
    print(
        f'PASS: {len(commands)} viewer utility scenarios, 65536 char classifications and 1114112 Unicode code-point observations'
    )


if __name__ == '__main__':
    main()
