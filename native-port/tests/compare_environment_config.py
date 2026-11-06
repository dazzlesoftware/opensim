import argparse, hashlib, itertools, json, os, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(value):
    return '-' if value is None else value.encode('utf-16-le', 'surrogatepass').hex() or '.'


def call(command, rows=None, env=None):
    r = subprocess.run(
        command,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=240,
        env=env,
    )
    assert r.returncode == 0, (command, r.returncode, r.stdout[-3000:], r.stderr[-3000:])
    return r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--ilspy')
    p.add_argument('--decompiled', type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    generated = []
    bodies = {}
    reference_text = (ROOT / 'native-port/tests/environment_config_reference.cs').read_text(
        encoding='utf-8'
    )
    expected_bodies = {
        'EnvAdapter': '838b094bb690d544b13a3d3300fc854340da2f25dff1cdb735753fc06f2c319a',
        'EnvMap': '1697d4db5ccf11070ba5c2b8fc4dfaa4292de877f40a7b34d2f6c9fe404f98cb',
        'EnvItem': 'd61e9fcace6fc066cab4f075b68afd59a9d3b6fb467e03e970432b4452a69670',
        'EnvConfig': 'ede272bb5e2e664086d888a45fd0e99fff6c3390935e18cefe38a1443a80d592',
        'EnvConfigSource': 'fc8e101f78bc684d0486de2b82fe58a344f3e23d4a693a9a3593bc18d456cfdc',
    }
    assert (
        hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest()
        == '1c491199a5258861b71037fbad6e5a61bc5257735576fbd98b6469c9651336d5'
    )
    for name in ['EnvAdapter', 'EnvMap', 'EnvItem', 'EnvConfig', 'EnvConfigSource']:
        begin = '// BEGIN EXTRACTED ' + name + '\n'
        end = '// END EXTRACTED ' + name
        body = reference_text.split(begin, 1)[1].split(end, 1)[0]
        bodies[name] = hashlib.sha256(body.encode()).hexdigest()
        assert bodies[name] == expected_bodies[name], ('Extracted Nini class changed', name)
        if a.ilspy or a.decompiled:
            namespace = 'Env' if name in ['EnvAdapter', 'EnvMap', 'EnvItem'] else 'Config'
            text = (
                (a.decompiled / (name + '.original.cs')).read_text(encoding='utf-8')
                if a.decompiled
                else '\n'.join(
                    call(
                        [
                            a.ilspy,
                            '-t',
                            'Nini.' + namespace + '.' + name,
                            str(ROOT / 'bin/Nini.dll'),
                        ]
                    )
                )
                + '\n'
            )
            assert text[text.index('public ') :] == body, name
    original = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    start = original.index('public static void MergeEnvironmentToConfig(')
    brace = original.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (original[end] == '{') - (original[end] == '}')
        end += 1
    method = original[start:end]
    for name, alias in [
        ('ControlledUtil', 'ScopedEnv.Config.EnvConfigSource'),
        ('RealUtil', 'Nini.Config.EnvConfigSource'),
    ]:
        target = w / (name + '.cs')
        target.write_text(
            'using Nini.Config;using EnvConfigSource='
            + alias
            + ';\nstatic class '
            + name
            + ' {\n'
            + method
            + '\n}\n',
            encoding='utf-8',
        )
        generated.append(target)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        EnableDefaultCompileItems='false',
        Nullable='disable',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for path in [ROOT / 'native-port/tests/environment_config_reference.cs'] + generated:
        ET.SubElement(group, 'Compile', Include=str(path))
    r = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'EnvironmentReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    reference = [a.dotnet, str(w / 'app/EnvironmentReference.dll')]
    native = [a.native, '--probe']
    rows = []
    assert call(reference + ['--identity']) == call([a.native, '--identity']) == ['3:True']
    runtime = call(reference + ['--runtime'])[0].split('|')
    assert runtime[0] == '8.0.31', runtime

    def op(name, *args):
        return ':'.join([name] + [enc(x) for x in args])

    section = op('sec', 'Environment')

    def row(ops, point=0, nth=1, mutation=0):
        rows.append(f'C {point} {nth} {mutation} ' + ','.join(ops))

    row(['merge'])
    row(['null', 'merge'])
    row([op('sec', 'environment'), 'merge'])
    names = [None, '', 'KEY', 'Key', 'a=b', 'a\0b', '\ud800', '名字😀', '${x}']
    values = [None, '', 'value', '${Other|x}', '\udfff', 'v\0tail']
    for key, value in itertools.product(names, values):
        setup = [section, op('add', 'Environment', key, 'ignored'), op('env', key, value)]
        row(
            setup
            + ['merge', 'merge', 'new', section, op('add', 'Environment', 'OTHER', ''), 'merge']
        )
    base = [
        section,
        op('add', 'Environment', 'KEY', 'old'),
        op('add', 'Environment', 'SECOND', 'ignored'),
        op('env', 'KEY', 'first'),
        op('env', 'SECOND', 'second'),
    ]
    for point, nth, mutation, auto in itertools.product(range(7), range(1, 5), range(6), range(2)):
        row(base + ['auto:' + str(auto), 'merge', 'merge'], point, nth, mutation)
    for name, value in itertools.product(names, values):
        row(
            [
                op('cache', 'entry', name, value),
                section,
                op('add', 'Environment', 'KEY', 'ignored'),
                'merge',
                'cv:0:' + enc('changed'),
                'new',
                section,
                op('add', 'Environment', 'NEW', 'ignored'),
                'merge',
            ]
        )
    for mutation in range(1, 6):
        row(base + ['mut:' + str(mutation), 'merge'])
    row(
        base
        + [
            'mut:1',
            'merge',
            'ecase:0',
            op('key', 'Environment', 'key', 'updated'),
            op('key', 'Environment', 'NEW', 'fresh'),
        ]
    )
    for value in [
        '${Environment|KEY}',
        '${Environment|missing}',
        '${missing}',
        'prefix ${Environment|KEY} suffix',
    ]:
        row(base + [op('sec', 'Other'), op('add', 'Other', 'x', value), 'merge'])
    for key, value, previous, mutation in itertools.product(
        ['KEY', 'Key', '名字😀', '\ud800'],
        ['', 'value', '\udfff', '${Other|x}'],
        [False, True],
        range(6),
    ):
        prefix = []
        if previous:
            prefix = [
                section,
                op('add', 'Environment', 'PRIOR', 'ignored'),
                op('env', 'PRIOR', 'cached'),
                'merge',
                'new',
            ]
        row(
            prefix
            + [
                section,
                op('add', 'Environment', key, 'ignored'),
                op('env', key, value),
                op('sec', 'Other'),
                op('add', 'Other', 'x', 'resolved'),
                'mut:' + str(mutation),
                'merge',
                'merge',
            ]
        )
    expected = call(reference, rows)
    actual = call(native, rows)
    assert len(expected) == len(actual) == len(rows)
    failures = [
        dict(index=i, row=row, source=s, native=n)
        for i, (row, s, n) in enumerate(zip(rows, expected, actual))
        if s != n
    ]
    report = dict(
        literal_identity_cases=1,
        runtime=runtime,
        corelib_sha256=hashlib.sha256(Path(runtime[1]).read_bytes()).hexdigest(),
        controlled_cases=len(rows),
        failures=failures[:20],
        method_sha256=hashlib.sha256(method.encode()).hexdigest(),
        class_body_sha256=bodies,
        native_sha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
        nini_sha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
    )
    (w / 'environment-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    assert not failures, json.dumps(failures[:2], indent=2)
    # Every real-env row is a fresh child; no host environment or registry writes.
    osrows = []
    key = 'OPENSIM_NATIVE_ENV_TEST_' + str(os.getpid())
    for k, v in itertools.product(
        [key, key + '名字😀', '', None, '\0x', 'a=b', 'a\0b', 'a\0=b'],
        [None, '', 'value', '名字😀', 'x\0tail', '\ud800'],
    ):
        osrows.append('R 0 1 0 ' + ','.join([op('env', k, v), op('get', k)]))
    for v in [None, '', 'value', '名字😀']:
        osrows.append(
            'R 0 1 0 '
            + ','.join(
                [
                    op('env', key, v),
                    section,
                    op('add', 'Environment', key, 'ignored'),
                    'merge',
                    'merge',
                    op('get', key),
                ]
            )
        )
    osrows.append(
        'R 0 1 0 '
        + ','.join(
            [
                op('env', key, 'b=c'),
                op('get', key + '=b'),
                section,
                op('add', 'Environment', key + '=b', 'ignored'),
                'merge',
            ]
        )
    )
    if os.name != 'nt':
        for initial, changed, managed in itertools.product(
            ['initial', '名字😀'], ['changed', None], ['managed', '\ud800']
        ):
            osrows.append(
                'R 0 1 0 '
                + ','.join(
                    [
                        op('rawset', key, initial),
                        op('get', key),
                        op('env', key, managed),
                        op('rawget', key),
                        op('get', key),
                        op('rawset', key, changed),
                        op('rawget', key),
                        op('get', key),
                    ]
                )
            )
        # The private PAL startup snapshot ignores later native libc mutations.
        osrows.append(
            'R 0 1 0 '
            + ','.join(
                [
                    op('rawset', key, None),
                    section,
                    op('add', 'Environment', key, ''),
                    'merge',
                    op('rawset', key, 'late-native'),
                    op('rawget', key),
                    op('get', key),
                ]
            )
        )
        for raw in ['ff', 'f0', 'c0af', 'eda080', 'e282', 'f0908080', 'efbfbd']:
            rawkey = key.encode().hex() + raw
            decoded = bytes.fromhex(raw).decode('utf-8', 'replace')
            osrows.append(
                'R 0 1 0 '
                + ','.join(
                    [
                        'rawbytes:' + rawkey + ':' + raw,
                        op('get', key + decoded),
                        op('env', key + 'CACHE', 'freeze'),
                        op('get', key + decoded),
                    ]
                )
            )
        osrows.append(
            'R 0 1 0 '
            + ','.join(
                [
                    'rawbytes:' + key.encode().hex() + 'ff:6669727374',
                    'rawbytes:' + key.encode().hex() + 'fe:7365636f6e64',
                    op('env', key + 'CACHE', 'freeze'),
                    op('get', key + '\ufffd'),
                ]
            )
        )
    if os.name == 'nt':
        for size in [127, 128, 129, 255, 256, 32766, 32767, 32768]:
            osrows.append('R 0 1 0 ' + ','.join([op('env', key, 'x' * size), op('get', key)]))
        for size in [32766, 32767, 32768]:
            osrows.append(
                'R 0 1 0 ' + ','.join([op('env', 'K' * size, 'value'), op('get', 'K' * size)])
            )
    failures = []
    for i, r in enumerate(osrows):
        s = call(reference, [r])[0]
        n = call(native, [r])[0]
        if s != n:
            failures.append(dict(index=i, row=r, source=s, native=n))
    env = dict(os.environ)
    env[key] = ''
    env[key + 'OTHER'] = ''
    identity_row = 'R 0 1 0 ' + op('emptyid', key, key + 'OTHER')
    source = call(reference, [identity_row], env)[0]
    actual = call(native, [identity_row], env)[0]
    assert source == actual and source.startswith('ID:True/'), (source, actual)
    utf16_values = ['x' + chr(c) + 'y' for c in range(65536)] + [
        '\ud800\ud800',
        '\udc00\ud800',
        '\ud800\udc00',
        '\ud800x\udfff',
    ]
    for start in range(0, len(utf16_values), 512):
        ops = []
        for value in utf16_values[start : start + 512]:
            ops += [op('env', key, value), op('get', key)]
        r = 'R 0 1 0 ' + ','.join(ops)
        source = call(reference, [r])[0]
        actual = call(native, [r])[0]
        if source != actual:
            failures.append(dict(utf16_value_chunk=start, source=source, native=actual))
    startup_count = 0
    if os.name != 'nt':
        for raw in [
            b'',
            b'value',
            b'\xff',
            b'\xf0',
            b'\xc0\xaf',
            b'\xed\xa0\x80',
            b'\xe2\x82',
            b'\xf0\x90\x80\x80',
        ]:
            env = dict(os.environb)
            env[key.encode()] = raw
            r = 'R 0 1 0 ' + ','.join(
                [
                    op('get', key),
                    section,
                    op('add', 'Environment', key, 'ignored'),
                    'merge',
                    op('get', key),
                ]
            )
            source = call(reference, [r], env)[0]
            actual = call(native, [r], env)[0]
            startup_count += 1
            if source != actual:
                failures.append(dict(startup_value_hex=raw.hex(), source=source, native=actual))
        # Actual PAL startup conversion, including its distinct invalid-prefix consumption.
        byte_values = [bytes([x]) for x in range(1, 256)] + [
            bytes([x, y]) for x in range(1, 256) for y in range(1, 256)
        ]
        byte_values += [
            bytes.fromhex(x)
            for x in [
                'e08080',
                'eda080',
                'edbfbf',
                'f0808080',
                'f4908080',
                'f48fbfbf',
                'f09f9880',
                'e282ac',
                'e282',
                'f09080',
            ]
        ]
        for start in range(0, len(byte_values), 512):
            env = dict(os.environb)
            ops = []
            for index, raw in enumerate(byte_values[start : start + 512]):
                name = key + 'BYTE' + str(index)
                env[name.encode()] = raw
                ops.append(op('get', name))
            r = 'R 0 1 0 ' + ','.join(ops)
            source = call(reference, [r], env)[0]
            actual = call(native, [r], env)[0]
            startup_count += len(ops)
            if source != actual:
                failures.append(dict(startup_byte_chunk=start, source=source, native=actual))
    report.update(
        os_empty_identity_cases=1,
        utf16_set_get_cases=len(utf16_values),
        startup_environment_cases=startup_count,
        real_child_cases=len(osrows),
        real_failures=failures[:20],
    )
    (w / 'environment-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    assert not failures, json.dumps(failures[:2], indent=2)
    print(
        json.dumps(
            dict(
                controlled_cases=len(rows),
                real_child_cases=len(osrows),
                utf16_set_get_cases=len(utf16_values),
                startup_environment_cases=startup_count,
                failures=0,
            )
        )
    )


if __name__ == '__main__':
    main()
