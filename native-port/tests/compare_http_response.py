"""Whole original HttpResponse and cookie classes, actual SDK bytes and managed streams."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def S(value):
    return None if value is None else {'h': value.encode().hex()}


def op(action, value=None, **fields):
    return dict(op=action, v=value, **fields)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    originals = [
        ROOT / 'OpenSim/Framework/Servers/HttpServer/OSHttpServer' / name
        for name in [
            'HttpResponse.cs',
            'RequestCookie.cs',
            'ResponseCookie.cs',
            'ResponseCookies.cs',
            'IHttpResponse.cs',
        ]
    ]
    files = []
    for source in originals:
        text = (
            source.read_text(encoding='utf-8-sig')
            .replace('DateTime.Now', 'ResponseClock.LocalNow')
            .replace('DateTime.UtcNow', 'ResponseClock.UtcNow')
        )
        f = w / source.name
        f.write_text(text, encoding='utf-8')
        files.append(f)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in files + [ROOT / 'native-port/tests/http_response_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverseTypes', 'OpenMetaverse']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'HttpResponse.csproj'
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

    def add(ops, **kw):
        cases.append(dict(ops=ops, **kw))

    for internal, version, connection in itertools.product(
        [False, True], [None, '', ' ', 'HTTP/1.1', 'HTTP/1.0', 'é😀', '\0'], [-1, 0, 1, 2]
    ):
        add(
            [op('headers'), op('header', S('x'), key=S('late')), op('clear')],
            internal=internal,
            version=S(version),
            connection=connection,
        )
    for internal, code, connection, maximum in itertools.product(
        [False, True],
        [-1, 0, 100, 101, 200, 201, 202, 204, 206, 301, 302, 307, 400, 404, 500, 502, 503, 999],
        [0, 1],
        [0, 1, 20],
    ):
        add(
            [
                op('status', code),
                op('headers'),
                op('send'),
                op('next', 1024),
                op('continue'),
                op('clear'),
            ],
            internal=internal,
            connection=connection,
            max=maximum,
        )
    for keep, timeout in itertools.product(
        [-2147483648, -1, 0, 1, 60, 120, 400, 401, 2147483647],
        [-2147483648, -1, 0, 999, 1000, 30000, 120000],
    ):
        add([op('keep', keep), op('send'), op('next', 12), op('continue')], timeout=timeout)
    for values in [
        [1, 2, 3, 4, 1],
        [100, 1, 1],
        [-1, 0, 1],
        [2147483647, 2, 1],
        [-2147483648, 4, 2],
    ]:
        add([op('priority', v) for v in values])
    for text, start, length in itertools.product(
        ['', 'abc', 'a' * 7900, 'a' * 8192, 'a' * 9000],
        [-1, 0, 1, 3, 2147483647],
        [-2147483648, -1, 0, 1, 4, 2147483647],
    ):
        add(
            [
                op('raw', S(text)),
                op('start', start),
                op('length', length),
                op('send'),
                op('next', 2048),
                op('continue'),
                op('next', 2048),
                op('continue'),
                op('clear'),
            ]
        )
    for limit, body, raw in itertools.product(
        [-2147483648, -1, 0, 1, 6, 1024, 128 * 1024],
        ['', 'abc', 'é世界', 'x' * 9000],
        [None, 'raw', 'r' * 9000],
    ):
        add(
            [op('body_write', S(body)), op('raw', S(raw)), op('send')]
            + [action for _ in range(4) for action in [op('next', limit), op('continue')]]
            + [op('clear')]
        )
    for akey, bkey in [
        ('X-Test', 'x-test'),
        ('Content-Type', 'content-type'),
        ('É', 'e\u0301'),
        ('x\0', 'x'),
        ('æ', 'Æ'),
        ('a\u200b', 'A'),
        ('ß', 'ss'),
        ('Ａ', 'ａ'),
        (None, None),
    ]:
        add(
            [
                op('header', S('first'), key=S(akey)),
                op('header', S('second'), key=S(bkey)),
                op('headers'),
                op('headers'),
            ]
        )
    for key, value in itertools.product(
        [
            None,
            '',
            'Content-Type',
            'content-type',
            'Connection',
            'connection',
            'Date',
            'date',
            'Server',
            'server',
            'Content-Length',
            'content-length',
            'Ü😀',
        ],
        [None, '', 'hello', '\r', '\n', 'a\rb', 'a\nb', 'a \r\nb', 'é世界'],
    ):
        add(
            [
                op('type', S('äType')),
                op('header', S(value), key=S(key)),
                op('headers'),
                op('redirect', S(None)),
                op('clear'),
            ]
        )
    for protocol, reason in itertools.product(
        [None, '', ' ', 'HTTP/1.0', 'é😀'], [None, '', 'Ready', 'é😀']
    ):
        add(
            [
                op('protocol', S(protocol)),
                op('reason', S(reason)),
                op('declared', '9223372036854775807'),
                op('chunked', True),
                op('send'),
            ]
        )
    for action, value in [
        ('body_touch', None),
        ('body_dispose', None),
        ('body_position', '-1'),
        ('body_position', '4'),
        ('body_length', '0'),
        ('body_length', '-1'),
        ('body_length', '1000'),
    ]:
        add(
            [
                op('body_write', S('ab')),
                op(action, value),
                op('body_write', S('c')),
                op('send'),
                op('next', 128),
                op('continue'),
                op('next', 128),
                op('continue'),
                op('clear'),
            ]
        )
    for encoding in ['null', 'utf8', 'ascii', 'unicode', 'newUtf8']:
        add([op('encoding', encoding), op('send'), op('clear')])
    for failure in [
        'version',
        'context',
        'connection',
        'id',
        'arrival',
        'closing',
        'max',
        'get_timeout',
        'set_timeout',
        'local',
        'utc',
        'start',
        'write',
        'continue',
        'end',
    ]:
        for nth in [1, 2, 3]:
            add(
                [
                    op('body_write', S('body')),
                    op('raw', S('r' * 9000)),
                    op('send'),
                    op('next', 3),
                    op('continue'),
                    op('next', 3),
                    op('continue'),
                    op('clear'),
                ],
                failure=failure,
                nth=nth,
            )
    for at, mutation in itertools.product(
        ['start', 'write', 'continue', 'end'], ['clear', 'continue']
    ):
        add(
            [
                op('body_write', S('b')),
                op('raw', S('r' * 9000)),
                op('send'),
                op('next', 4096),
                op('continue'),
                op('next', 4096),
                op('continue'),
                op('clear'),
            ],
            at=at,
            mutation=mutation,
        )
    for send_ok in [False, True]:
        for raw, body in itertools.product([None, '', 'raw', 'r' * 9000], [None, '', 'body']):
            add(
                ([op('body_write', S(body))] if body is not None else [])
                + [
                    op('raw', S(raw)),
                    op('send'),
                    op('next', 512),
                    op('continue'),
                    op('next', 512),
                    op('clear'),
                ],
                send_ok=send_ok,
            )
    for null_request, null_context in itertools.product([False, True], [False, True]):
        add(
            [op('headers'), op('send'), op('next', 512), op('continue'), op('clear')],
            null_request=null_request,
            null_context=null_context,
        )
    for name, value, path, domain in itertools.product(
        [None, '', 'name', 'a b', 'é😀', ' '],
        [None, '', 'value', 'a+b/?', 'é世界'],
        [None, '', '/', '/é'],
        [None, '', 'example.test'],
    ):
        add(
            [
                op('cookie', S(value), key=S(name), path=S(path), domain=S(domain)),
                op('headers'),
                op('cookie_clear'),
                op('headers'),
            ]
        )
    for expiry, offset in itertools.product(
        [0, 1, 3155378975999999999, 638397614450000000], [-252000000000, 0, 198000000000]
    ):
        add(
            [op('cookie', S('v'), key=S('id'), expires=str(expiry)), op('headers')],
            local=str(638397614450000000 + offset),
            utc='638397614450000000',
        )
    for custom in ['custom', 'null', 'throw', 'clear', 'add', 'replace']:
        add(
            [
                op('cookie', custom=custom),
                op('cookie', S('v'), key=S('tail')),
                op('headers'),
                op('headers'),
                op('clear'),
            ]
        )
    add([op('cookie', S('v'), key=S('id')), op('cookie_null', S('id')), op('headers')])
    add([op('cookie_null', None), op('headers')])
    words = [
        '',
        'a',
        'A',
        'a\0',
        'a\u200b',
        'a\u00ad',
        'é',
        'e\u0301',
        'æ',
        'Æ',
        'ae',
        'ß',
        'ẞ',
        'ss',
        'SS',
        'Ａ',
        'ａ',
        'A',
        'K',
        'K',
        'İ',
        'i',
        'ı',
        'I',
        'Σ',
        'σ',
        'ς',
        'ァ',
        'ぁ',
        'カ',
        'か',
        '😀',
        '試験',
    ]
    for akey, bkey in itertools.product(words, words):
        add(
            [
                op('header', S('one'), key=S(akey)),
                op('header', S('two'), key=S(bkey)),
                op('headers'),
            ]
        )
    for value, path, expiry, kind in itertools.product(
        [None, '', 'v', 'é+'], [None, '', '/scope'], [0, 1, 638397614450000000], [0, 1, 2]
    ):
        add(
            [
                op('cookie', S('original'), key=S('id')),
                op('cookie_value', S(value), key=S('id')),
                op('cookie_path', S(path), key=S('id')),
                op('cookie_expiry', str(expiry), key=S('id'), kind=kind),
                op('cookie_get', key=S('id')),
                op('cookie_render', key=S('id')),
                op('headers'),
            ]
        )
    for key, name, value in itertools.product(
        [None, 'old', 'new'], [None, '', 'renamed', ' '], [None, '', 'v', ' ']
    ):
        add(
            [
                op('cookie', S('v'), key=S('old')),
                op('cookie_set', S(value), key=S(key), name=S(name)),
                op('cookie_get', key=S('old')),
                op('cookie_get', key=S(name)),
                op('headers'),
            ]
        )
    for name, value, after in itertools.product(
        [None, '', 'id', ' '], [None, '', 'é+'], [None, '', 'new']
    ):
        add(
            [
                op('request_cookie', S(value), key=S(name), after=S(after)),
                op('cookie_copy', S(value), key=S(name), after=S(after)),
                op('headers'),
            ]
        )
    for start, count, position in itertools.product([0, 2], [0, 3, 7], [0, 2, 4, 300]):
        add(
            [
                op('body_write', S('abcdefghij')),
                op('body_position', str(position)),
                op('body_copy', start=start, count=count),
                op('send'),
                op('next', 999),
                op('continue'),
                op('next', 999),
                op('continue'),
            ]
        )
    for length, readable, failure in itertools.product(
        [0, 3, 9223372036854775807], [True, False], ['', 'body_len', 'body_read', 'body_dispose']
    ):
        add(
            [
                op('body_fake', str(length), readable=readable),
                op('raw', S('raw')),
                op('headers'),
                op('send'),
                op('next', 32),
                op('continue'),
                op('next', 32),
                op('continue'),
                op('clear'),
            ],
            failure=failure,
        )
    for at in ['body_len', 'body_read', 'body_dispose']:
        add(
            [
                op('body_fake', '3'),
                op('headers'),
                op('send'),
                op('next', 32),
                op('continue'),
                op('next', 32),
                op('clear'),
            ],
            at=at,
            mutation='clear',
        )
    for position, offset, origin in itertools.product(
        [0, 1, 10, 2147483647],
        [
            -9223372036854775808,
            -2147483648,
            -20,
            -1,
            0,
            1,
            20,
            2147483647,
            2147483648,
            9223372036854775807,
        ],
        [-1, 0, 1, 2, 3],
    ):
        add(
            [
                op('body_write', S('abcdefghij')),
                op('body_position', str(position)),
                op('body_seek', str(offset), origin=origin),
                op('body_read', 3),
                op('body_byte'),
                op('body_array'),
                op('body_write_to'),
            ]
        )
    for disposed, position, count in itertools.product(
        [False, True], [0, 1, 10, 20], [0, 1, 3, 10, 20]
    ):
        add(
            [op('body_write', S('abcdefghij')), op('body_position', str(position))]
            + ([op('body_dispose')] if disposed else [])
            + [
                op('body_read', count),
                op('body_byte'),
                op('body_array'),
                op('body_write_to'),
                op('clear'),
            ]
        )
    commands = [
        json.dumps(c, ensure_ascii=False, separators=(',', ':')).encode().hex() for c in cases
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/HttpResponse.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=180
        )
        assert r.returncode == 0, r.stderr[-3000:]
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases), (len(rows[0]), len(rows[1]), len(cases))
    diffs = [
        {'case': c, 'source': s[:2500], 'native': n[:2500]}
        for c, s, n in zip(cases, *rows)
        if s != n
    ]
    (w / 'http-response-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sources': {s.name: hashlib.sha256(s.read_bytes()).hexdigest() for s in originals},
                'scope': 'Whole original HttpResponse/cookie classes and original IHttpResponse; actual SDK UTF8 builder, managed MemoryStream, NameValueCollection, Dictionary, HttpUtility, enum names. Clock property projection; controlled request/context/network callbacks. Full OS networking, SDK pooling/GC/array capacity/reuse and general concurrent/invalid UTF16/encoding/runtime/platform behavior remain pending.',
                'differences': diffs[:6],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not diffs, json.dumps(diffs[:1])
    print(f'PASS: {len(cases)} HTTP response lifecycle and cookie source observations')


if __name__ == '__main__':
    main()
