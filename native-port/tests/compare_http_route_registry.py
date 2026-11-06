"""Original BaseHttpServer route methods with actual ConcurrentDictionary and handler property facades."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import extract

ROOT = Path(__file__).resolve().parents[2]
METHODS = [
    r'public void AddStreamHandler\(',
    r'public void AddGenericStreamHandler\(',
    r'public void AddSimpleStreamHandler\(',
    r'public List<string> GetStreamHandlerKeys\(',
    r'public List<string> GetSimpleStreamHandlerKeys\(',
    r'private static string GetHandlerKey\(',
    r'public bool AddPollServiceHTTPHandler\(string',
    r'public bool AddPollServiceHTTPHandler\(PollServiceEventArgs',
    r'public bool AddPollServiceHTTPHandlerVarPath\(',
    r'public List<string> GetPollServiceHandlerKeys\(',
    r'private bool TryGetStreamHandler\(',
    r'private bool TryGetPollServiceHTTPHandler\(',
    r'private bool TryGetSimpleStreamHandler\(',
    r'public void RemoveStreamHandler\(string httpMethod',
    r'public void RemoveStreamHandler\(string path',
    r'public void RemoveSimpleStreamHandler\(',
    r'public void RemovePollServiceHTTPHandler\(string httpMethod',
    r'public void RemovePollServiceHTTPHandler\(string path',
]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Framework/Servers/HttpServer/BaseHttpServer.cs'
    text = source.read_text(encoding='utf-8-sig')
    wrapper = w / 'Methods.cs'
    wrapper.write_text(
        'using System;using System.Collections.Generic;public partial class RouteReference{'
        + ''.join(extract(text, m) for m in METHODS)
        + '}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    properties = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(properties, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [wrapper, ROOT / 'native-port/tests/http_route_registry_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = w / 'Routes.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8')
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(cs),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    cases = []
    paths = [
        None,
        '',
        '/',
        '//',
        '/x',
        '/x/y',
        '/x/child',
        'GET:/x',
        'GET:',
        ' /x ',
        '\u2000',
        '/é',
        'é/x',
        '😀/x',
        '/a\0',
        '/e\u0301',
        '/é',
        '/lslhttp/00000000-0000-0000-0000-000000000001',
    ]

    def add(ops, **kwargs):
        cases.append(dict(ops=ops, **kwargs))

    for path, op, method in itertools.product(
        paths, ['add', 'generic', 'simple', 'poll', 'poll_variable'], ['GET', 'POST', None]
    ):
        add(
            [
                {'op': op},
                {'op': 'get', 'path': None},
                {'op': 'get', 'path': ('GET:' + (path or ''))},
                {'op': 'get_simple', 'path': path},
                {'op': 'get_poll', 'path': path},
                {'op': 'remove', 'path': path},
                {'op': 'remove_simple', 'path': path},
                {'op': 'remove_poll', 'path': path},
            ],
            A={'path': path, 'method': method},
        )
    for variable, path in itertools.product([True, False], paths):
        add(
            [{'op': 'simple', 'variable': variable}]
            + [
                {'op': 'get_simple', 'path': v}
                for v in [path, (path or '') + '/a', (path or '') + '/', (path or '') + '/a/b']
            ]
            + [
                {'op': 'remove_simple', 'path': path},
                {'op': 'get_simple', 'path': (path or '') + '/a'},
            ],
            A={'path': path},
        )
    for count, unicode in itertools.product(
        [0, 1, 10, 11, 21, 22, 42, 43, 44, 45, 46], ['a', 'é', '😀']
    ):
        path = '/' + unicode * count
        add(
            [{'op': 'poll_variable'}]
            + [
                {'op': 'get_poll', 'path': p}
                for p in [path, path + '/child', path + '/', path + 'child']
            ]
            + [
                {'op': 'poll_at', 'h': None, 'path': path},
                {'op': 'get_poll', 'path': path},
                {'op': 'remove_poll', 'path': path},
                {'op': 'get_poll', 'path': path},
                {'op': 'mutate_poll', 'path': 'changed'},
                {'op': 'get_poll', 'path': path},
            ],
            A={'path': path},
        )
    for setup, remove in itertools.product(
        ['stream-generic', 'simple-variable', 'poll-variable'],
        ['remove', 'remove_key', 'remove_simple', 'remove_poll', 'remove_poll_method'],
    ):
        ops = {
            'stream-generic': [{'op': 'add'}, {'op': 'generic', 'h': 'B'}],
            'simple-variable': [{'op': 'simple'}, {'op': 'simple', 'h': 'B', 'variable': True}],
            'poll-variable': [{'op': 'poll'}, {'op': 'poll_variable', 'h': 'B'}],
        }[setup]
        add(
            ops
            + [
                {'op': remove},
                {'op': 'get', 'path': 'GET:/x'},
                {'op': 'get', 'path': '/x'},
                {'op': 'get_simple', 'path': '/x/child'},
                {'op': 'get_poll', 'path': '/x'},
            ],
            A={'path': '/x'},
            B={'path': '/x'},
        )
    for op, seq, method in itertools.product(
        ['add', 'generic', 'simple'],
        [['/x', '/y'], ['/x', None], ['/x', ''], ['/', '/y'], [' ', '/x']],
        [None, 'GET', 'POST'],
    ):
        add(
            [
                {'op': op},
                {'op': 'add', 'h': 'B'},
                {'op': op},
                {'op': 'get', 'path': 'GET:/x/child'},
            ],
            A={'paths': seq, 'method': method},
            B={'path': '/x'},
        )
    for op, nth in itertools.product(['path:A', 'method:A'], [1, 2, 3]):
        add([{'op': 'add'}, {'op': 'add'}, {'op': 'generic'}], failure=op, fail_nth=nth)
    for op in ['add', 'generic', 'simple']:
        add(
            [{'op': op}, {'op': 'add', 'h': 'A'}],
            mutation='path:A',
            A={'path': '/a'},
            B={'path': '/b'},
        )
    for base, child in [
        ('/x', '/x/y'),
        ('/e\u0301', '/é/child'),
        ('/a\0', '/a/child'),
        ('GET:', 'GET:/x'),
        ('', 'anything'),
    ]:
        add(
            [
                {'op': 'generic'},
                {'op': 'add', 'h': 'B'},
                {'op': 'get', 'path': base},
                {'op': 'get', 'path': child},
                {'op': 'remove_key', 'path': base},
                {'op': 'get', 'path': child},
            ],
            A={'path': base},
            B={'path': child},
        )
    for op in ['add', 'generic', 'simple', 'poll', 'poll_at', 'poll_variable']:
        add([{'op': op, 'h': None}, {'op': 'get_poll', 'path': '/x'}])
    for path, method in itertools.product([None, '', '/', '/x'], [None, '', 'GET']):
        add([{'op': 'key', 'path': path, 'method': method}])

    # The SDK-compatible JSON parser intentionally turns empty strings into OSD.Undefined.
    # Preserve nullable route strings in this test transport with an explicit hex wrapper.
    def transport(value):
        if value == '':
            return {'__route_string': ''}
        if isinstance(value, list):
            return [transport(v) for v in value]
        if isinstance(value, dict):
            return {k: transport(v) for k, v in value.items()}
        return value

    for A, B, C in [('/x', '/x/y', '/x/y/z'), ('/é', '/e\u0301', '/éxy')]:
        add(
            [
                {'op': 'add'},
                {'op': 'add', 'h': 'B'},
                {'op': 'add', 'h': 'C'},
                {'op': 'get', 'path': 'GET:' + C + '/tail'},
                {'op': 'get', 'path': 'GET:' + A},
                {'op': 'get', 'path': 'GET:' + B + '/tail'},
                {'op': 'remove', 'path': C},
                {'op': 'get', 'path': 'GET:' + C + '/tail'},
            ],
            A={'path': A},
            B={'path': B},
            C={'path': C},
        )
    add(
        [
            {'op': 'add'},
            {'op': 'add', 'h': 'B'},
            {'op': 'add', 'h': 'C'},
            {'op': 'remove', 'path': '/'},
        ],
        A={'path': '/'},
        B={'path': '/'},
        C={'path': '/', 'method': 'POST'},
    )
    for path in ['é', '😀', '/😀']:
        add(
            [
                {'op': 'simple', 'variable': True},
                {'op': 'get_simple', 'path': path + '/x'},
                {'op': 'get_simple', 'path': path + '/'},
            ],
            A={'path': path},
        )
    commands = [
        json.dumps(transport(c), ensure_ascii=False, separators=(',', ':')).encode().hex()
        for c in cases
    ]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/Routes.dll')], [a.native]]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=120
        )
        assert r.returncode == 0, r.stdout[-1000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    diffs = [{'input': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n]
    (w / 'http-route-registry-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': '18 complete original route methods, actual ConcurrentDictionary and controlled handler-property facades. Normal/generic/simple/poll ownership, root replacement, nulls, property order/failures/argument aliases, UTF16 variable path bounds, cultural longest prefixes, snapshots and removal precedence. Unordered key membership canonicalized; equal-length cultural ties, full HTTP dispatch/generic/RPC/WebSocket/poll runtime, CLR/concurrent scheduling and Unix remain pending.',
                'differences': diffs[:6],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:2]))
    print(f'PASS: {len(cases)} HTTP route registry source observations')


if __name__ == '__main__':
    main()
