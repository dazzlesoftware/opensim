"""Original unchanged Region ConfigurationLoader with actual Nini.dll and explicit boundary providers."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enc(s):
    return 'null' if s is None else ''.join(f'{ord(c):04x}' for c in s) or '-'


def call(cmd, rows=None, cwd=None):
    r = subprocess.run(
        cmd,
        input=None if rows is None else '\n'.join(rows) + '\n',
        capture_output=True,
        text=True,
        encoding='utf-8',
        timeout=180,
        cwd=cwd,
    )
    assert r.returncode == 0, (r.returncode, r.stdout[-3000:], r.stderr[-3000:])
    return r.stdout.splitlines()


def extract(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    n = brace + 1
    while depth:
        if text[n] == '{':
            depth += 1
        elif text[n] == '}':
            depth -= 1
        n += 1
    return text[start:n]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    a.native = str(Path(a.native).resolve())
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    (w / 'Aliases.cs').write_text(
        'global using Path=Shim.Path;global using File=Shim.File;global using Directory=Shim.Directory;global using Environment=Shim.Environment;global using XmlReader=Shim.XmlReader;',
        encoding='utf-8',
    )
    body = (
        extract(
            (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8'),
            'public static string[] Glob(string path)',
        )
        .replace('Path.', 'System.IO.Path.')
        .replace('Directory.', 'System.IO.Directory.')
    )
    (w / 'Glob.cs').write_text(
        'using System;using System.Collections.Generic;static class SourceUtil{' + body + '}',
        encoding='utf-8',
    )
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
    for f in [
        ROOT / 'native-port/tests/configuration_loader_reference.cs',
        ROOT / 'OpenSim/Region/Application/ConfigurationLoader.cs',
        w / 'Aliases.cs',
        w / 'Glob.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    r = ET.SubElement(group, 'Reference', Include='Nini')
    ET.SubElement(r, 'HintPath').text = str(ROOT / 'bin/Nini.dll')
    cs = w / 'ConfigurationLoaderReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    call([a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'])
    base = w / 'fixture'
    base.mkdir(exist_ok=True)
    (base / 'include').mkdir(exist_ok=True)
    (base / 'overrides').mkdir(exist_ok=True)
    (base / 'combined-only').mkdir(exist_ok=True)
    cwd = w / 'process-cwd'
    cwd.mkdir(exist_ok=True)
    (cwd / 'overrides').mkdir(exist_ok=True)
    (cwd / 'overrides/from-cwd.ini').write_text(
        '[Values]\nkey=raw-directory-name\n', encoding='utf-8'
    )
    fixtures = {
        'OpenSimDefaults.ini': '[Startup]\nphysics=master\nmeshing=mesh\n[Values]\nkey=master\n',
        'OpenSim.ini': '[Startup]\nphysics=local\n[Values]\nkey=local\n',
        'OpenSim.xml': '[Startup]\nphysics=xml-local-ini\n',
        'includes.ini': '[Startup]\nInclude-1=include/*.ini\nInclude-2=include/second.ini\n[Values]\nkey=main\n',
        'include/first.ini': '[Values]\nkey=first\nextra=one\n',
        'include/é.ini': '[Unicode]\nvalue=unicode\n',
        'include/second.ini': '[Values]\nkey=second\nInclude-back=includes.ini\n',
        'overrides/override.INI': '[Values]\nkey=override\n',
        'overrides/skip.txt': '[Values]\nkey=ignored\n',
        'invalid.ini': '[bad',
        'expand.ini': '[Startup]\nphysics=${Values|key}\n[Values]\nkey=value\n',
        'missingkey.ini': '[Startup]\nphysics=${Values|absent}\n[Values]\nkey=value\n',
    }
    for name, text in fixtures.items():
        (base / name).write_text(text, encoding='utf-8')
    rows = []

    def add(mode, ini, dir=base, sources='', env='', fault='-', nth=1):
        rows.append(f'{mode} {enc(ini)} {enc(str(dir))} {enc(sources)} {enc(env)} {fault} {nth}')

    for section, key, file, initial in itertools.product(
        ['Startup', 'Other'],
        ['Include-1', 'include-lower', 'Include-', 'Include-\x00'],
        [
            'include/first.ini',
            'include/*.ini',
            'include/?.ini',
            'missing.ini',
            'missing*.ini',
            'include/second.ini',
            'https://example.test/config',
            'http://example.test/config',
        ],
        ['', str(base / 'include/first.ini')],
    ):
        add('I', f'[{section}]\n{key}={file}\n', sources=initial)
    includes = [
        'include/first.ini',
        'include/*.ini',
        'inc*/first.ini',
        'include/?.ini',
        'include/é.ini',
        'include/../include/first.ini',
        './include/first.ini',
        'missing.ini',
        'missing*.ini',
        'missing/first.ini',
        'http://example.test/a',
        'https://example.test/b',
        '',
        str(base / 'include/first.ini'),
    ]
    for first, second, section in itertools.product(includes, includes, ['Startup', 'Other']):
        add('I', f'[{section}]\nInclude-first={first}\nInclude-second={second}\n')
    include_config = '[First]\nInclude-1=include/*.ini\nInclude-2=missing.ini\n[Second]\nInclude-3=includes.ini\nInclude-4=https://example.test/remote\n'
    for fault, nth in itertools.product(
        ['configdir', 'full', 'combine', 'glob', 'log'], range(1, 16)
    ):
        add('I', include_config, fault=fault, nth=nth)
    for file, fault, nth in itertools.product(
        [
            str(base / 'OpenSim.ini'),
            str(base / 'invalid.ini'),
            str(base / 'missing.ini'),
            'https://example.test/remote',
            'http://example.test/config',
        ],
        ['-', 'full', 'log', 'xml'],
        [1, 2, 3],
    ):
        add('R', '[a]\nx=existing\n', sources=file, fault=fault, nth=nth)
    for master, ini, directory, env in itertools.product(
        ['none', 'OpenSimDefaults.ini', 'missing-master.ini', 'https://example.test/master'],
        [
            'OpenSim.ini',
            'includes.ini',
            'expand.ini',
            'missingkey.ini',
            'invalid.ini',
            'absent.ini',
            'https://example.test/config',
        ],
        ['missing-dir', 'combined-only', 'overrides', str(base / 'overrides')],
        ['', '[Values]\nkey=environment\n'],
    ):
        add(
            'L',
            f'[Startup]\ninimaster={master}\ninifile={ini}\ninidirectory={directory}\n',
            env=env,
        )
    for second, fault, nth in itertools.product(
        [
            '[Startup]\ninimaster=missing-master.ini\n',
            '[NoStartup]\nx=1\n',
            '[Startup]\ninimaster=none\ninifile=expand.ini\ninidirectory=missing\n',
        ],
        ['-', 'networknew', 'log'],
        [1, 2, 3],
    ):
        add(
            'T',
            '[Startup]\ninimaster=none\ninifile=OpenSim.ini\ninidirectory=missing\n',
            sources=second,
            fault=fault,
            nth=nth,
        )
    for master, ini in itertools.product(
        ['none', 'OpenSim.ini', './OpenSim.ini', 'opensim.ini'],
        ['OpenSim.ini', './OpenSim.ini', 'opensim.ini', 'include', 'include/', 'OpenSim.ini/'],
    ):
        add('L', f'[Startup]\ninimaster={master}\ninifile={ini}\ninidirectory=missing\n')
    add('L', '[NoStartup]\nx=1\n')
    add(
        'L',
        '[Startup]\ninimaster=none\ninifile=missing.ini\ninidirectory=missing\n',
        dir=w / 'empty',
    )
    for ini in ['OpenSim.ini', 'invalid.ini']:
        add(
            'L',
            f'[Startup]\ninimaster=none\ninifile={ini}\ninidirectory=missing\n',
            fault='mutate_source',
        )
    good = f'[Startup]\ninimaster=OpenSimDefaults.ini\ninifile=includes.ini\ninidirectory={base/"overrides"}\nphysics=argv\n'
    for fault, nth in itertools.product(
        [
            'networknew',
            'configdir',
            'combine',
            'full',
            'fileexists',
            'direxists',
            'files',
            'extension',
            'glob',
            'log',
            'environment',
            'networkload',
        ],
        range(1, 13),
    ):
        add('L', good, env='[Values]\nkey=env\n', fault=fault, nth=nth)
    source = call([a.dotnet, str(w / 'app/ConfigurationLoaderReference.dll')], rows, cwd)
    native = call([a.native, '--probe'], rows, cwd)
    assert len(source) == len(native) == len(rows), (len(source), len(native), len(rows))
    diff = [
        dict(index=i, command=c, source=s, native=n)
        for i, (c, s, n) in enumerate(zip(rows, source, native))
        if s != n
    ]
    report = dict(
        passed=not diff,
        cases=len(rows),
        sourceSha256=hashlib.sha256(
            (ROOT / 'OpenSim/Region/Application/ConfigurationLoader.cs').read_bytes()
        ).hexdigest(),
        niniSha256=hashlib.sha256((ROOT / 'bin/Nini.dll').read_bytes()).hexdigest(),
        scope='Unchanged original loader and original Util.Glob with actual Nini.dll; native real local INI/path/filesystem/glob. Explicit XML fault/environment/network DTO/log/exit boundary providers. Controlled ASCII culture and valid HTTP(S) URI fixture subset does not establish arbitrary culture or full Uri parsing.',
        differences=diff[:5],
    )
    (w / 'configuration-loader-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:2])
    print('PASS:', len(rows), 'actual original configuration loader cases')


if __name__ == '__main__':
    main()
