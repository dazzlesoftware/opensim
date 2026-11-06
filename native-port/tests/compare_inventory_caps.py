"""Actual FetchInventorySimpleRequest and InventoryItemBase.ToLLSDxml; actual SDK writer."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(s, signature):
    start = s.index(signature)
    brace = s.index('{', start)
    end = brace + 1
    depth = 1
    while depth:
        if s[end] == '{':
            depth += 1
        elif s[end] == '}':
            depth -= 1
        end += 1
    return s[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    handler = ROOT / 'OpenSim/Capabilities/Handlers/FetchInventory/FetchInventory2Handler.cs'
    item = ROOT / 'OpenSim/Framework/InventoryItemBase.cs'
    method = extract(handler.read_text(), 'public void FetchInventorySimpleRequest(')
    serializer = extract(item.read_text(), 'public void ToLLSDxml(')
    description = extract(item.read_text(), 'public string Description')
    wrapper_source = ROOT / 'OpenSim/Framework/Servers/HttpServer/SimpleOSDMapHandler.cs'
    wrapper = extract(wrapper_source.read_text(), 'public virtual void Handle(')
    generated = w / 'ExtractedInventoryCaps.cs'
    generated.write_text(
        'using System;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;\npartial class InventoryCapsReference {\n'
        + method
        + '\n}\npartial class InventoryItemBase {\npublic osUTF8 UTF8Description;\n'
        + description
        + '\n'
        + serializer
        + '\n}\npartial class Wrapper {\n'
        + wrapper
        + '\n}\n'
    )
    expiry = ROOT / 'OpenSim/Framework/ExpiringKey.cs'
    expiry_copy = w / 'ExtractedExpiringKey.cs'
    expiry_copy.write_text(
        expiry.read_text().replace(
            'using Timer = System.Threading.Timer ;', 'using Timer = global::ControlledTimer;'
        )
    )
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
    group = ET.SubElement(project, 'ItemGroup')
    for f in [generated, expiry_copy, ROOT / 'native-port/tests/inventory_caps_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'InventoryCapsReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [
        f'{seed} {count} {mode}'
        for seed in range(16)
        for count in [0, 1, 2, 3, 5, 16]
        for mode in [0, 1, 2, 3, 4, 5, 8, 9, 16, 18, 32, 34, 64, 65]
    ]
    payload = '\n'.join(commands) + '\n'
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/InventoryCapsReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(cmd, input=payload, capture_output=True, text=True, timeout=60)
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    cache_commands = [
        f'{at} {expire} {after} {purge}'
        for at in [0, 1, 2147453647, 2147483640, -2147483648]
        for expire in [-1, 0, 1, 29999, 30000, 30001, 2147483647]
        for after in [-2147483648, 0, 29999, 30000, 30001, 60001, 2147483647]
        for purge in [0, 1]
    ]
    cache_rows = []
    for cmd in [
        [a.dotnet, str(w / 'app/InventoryCapsReference.dll'), '--cache'],
        [a.native, '--cache'],
    ]:
        r = subprocess.run(
            cmd, input='\n'.join(cache_commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        cache_rows.append(r.stdout.splitlines())
    assert len(cache_rows[0]) == len(cache_rows[1]) == len(cache_commands)
    timer_rows = []
    for cmd in [
        [a.dotnet, str(w / 'app/InventoryCapsReference.dll'), '--timer'],
        [a.native, '--timer'],
    ]:
        r = subprocess.run(
            cmd, input='\n'.join(cache_commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        timer_rows.append(r.stdout.splitlines())
    assert len(timer_rows[0]) == len(timer_rows[1]) == len(cache_commands)
    timer_diffs = [
        dict(command=c, source=s, native=n)
        for c, s, n in zip(cache_commands, *timer_rows)
        if s != n
    ]
    assert not timer_diffs, timer_diffs[:2]
    http_bodies = [
        '',
        '{}',
        '[]',
        'null',
        'bad',
        '{"items":[]}',
        ' {"items":[]} ',
        '{"items":[{}]}',
        '{"items":[1]}',
        '{"items":"bad"}',
        '<llsd><map><key>items</key><array /></map></llsd>',
        ' <llsd><map><key>items</key><array /></map></llsd>',
        '<?xml version="1.0"?><llsd><map><key>items</key><array /></map></llsd>',
        '<llsd><array /></llsd>',
        '<llsd><map>',
        '<LLSD><map /></LLSD>',
        '\ufeff{"items":[]}',
        '<? llsd/xml ?><llsd><map><key>items</key><array /></map></llsd>',
    ]
    import struct

    def n(i):
        return struct.pack('>i', i)

    def key(s):
        return b'k' + n(len(s)) + s

    binary = [
        b'!',
        b'0',
        b'1',
        b'{' + n(0) + b'}',
        b'[' + n(0) + b']',
        b'{' + n(1) + key(b'items') + b'[' + n(0) + b']}',
    ]
    for binary_item in [
        b'{' + n(0) + b'}',
        b'{' + n(1) + key(b'item_id') + b'u' + bytes(16) + b'}',
        b'!',
        b'1',
        b's' + n(1) + b'x',
    ]:
        binary.append(b'{' + n(1) + key(b'items') + b'[' + n(1) + binary_item + b']}')
    raw_bodies = [body.encode('utf-8') for body in http_bodies]
    for body in binary:
        raw_bodies.append(b'<? llsd/binary ?>\n' + body)
        raw_bodies.extend(b'<? llsd/binary ?>\n' + body[:i] for i in range(len(body)))
    raw_bodies.extend(
        body.encode()
        for body in [
            '<llsd><map><key>items</key><array/></map><string bad="&unknown;">',
            '<llsd><map><key>items</key><array><foo></foo></array></map></llsd>',
        ]
    )
    notation = [
        "!",
        "{}",
        "[]",
        "{'items':[]}",
        "{'items':[{}]}",
        "{'items':[{'item_id':u00000000-0000-0000-0000-000000000000}]}",
        "{'items':[!]}",
        "{'items':[i1]}",
        "{'items':[l\"relative\"]}",
        "{'items':!}",
        "{'items':i1}",
    ]
    for body in notation:
        raw_bodies.append(('<? llsd/notation ?>' + body + '\0\0\0').encode())
        raw_bodies.extend(
            ('<? llsd/notation ?>' + body[:i] + '\0\0\0').encode() for i in range(len(body))
        )
    for body in [
        '{}',
        '{"items":[]}',
        '{"items":[{}]}',
        '{"items":[{"item_id":"00000000-0000-0000-0000-000000000000"}]}',
        '{"items":"\\ud800"}',
    ]:
        for encoding, bom in [
            ('utf-8', b'\xef\xbb\xbf'),
            ('utf-16-le', b'\xff\xfe'),
            ('utf-16-be', b'\xfe\xff'),
            ('utf-32-le', b'\xff\xfe\x00\x00'),
            ('utf-32-be', b'\x00\x00\xfe\xff'),
        ]:
            encoded = bom + body.encode(encoding)
            raw_bodies.append(encoded)
            raw_bodies.extend(encoded[:i] for i in range(len(encoded)))
    http_commands = [
        f"{method} {content} {body.hex() or '-'}"
        for method in ['POST', 'post', 'GET', 'PUT']
        for content in ['application/json', 'application/llsd+xml', 'text/plain']
        for body in raw_bodies
    ]

    http_rows = []
    for cmd in [
        [a.dotnet, str(w / 'app/InventoryCapsReference.dll'), '--http'],
        [a.native, '--http'],
    ]:
        r = subprocess.run(
            cmd, input='\n'.join(http_commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        http_rows.append(r.stdout.splitlines())
    assert len(http_rows[0]) == len(http_rows[1]) == len(http_commands)
    http_diffs = [
        dict(command=c, source=s, native=n) for c, s, n in zip(http_commands, *http_rows) if s != n
    ]
    assert not http_diffs, http_diffs[:2]
    cache_diffs = [
        dict(command=c, source=s, native=n)
        for c, s, n in zip(cache_commands, *cache_rows)
        if s != n
    ]
    assert not cache_diffs, cache_diffs[:2]
    diffs = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    report = dict(
        passed=not diffs,
        cases=len(commands),
        expiryCases=len(cache_commands),
        timerCases=len(cache_commands),
        httpCases=len(http_commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [
                handler,
                item,
                expiry,
                wrapper_source,
                ROOT / 'bin/OpenMetaverse.StructuredData.dll',
                ROOT / 'bin/OpenMetaverse.dll',
                ROOT / 'bin/OpenMetaverseTypes.dll',
            ]
        },
        scope='Actual source handler and item serializer extracted unchanged; actual SDK OSD/LLSD writer. Controlled inventory service, bad-key membership and normalized cast exceptions. Whole original ExpiringKey class with controlled clock/timer purge callbacks compared; Actual SimpleOSDMapHandler.Handle with SDK stream parser compared for XML/JSON, method/parse/handler failure counts. LLSD binary is native; notation is native with explicit quoted EOF termination difference; alternate JSON stream encodings require further implementation; library/descendents and whole provider/culture/invalid UTF8 remain separate.',
        differences=diffs[:2],
    )
    (w / 'inventory-caps-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not diffs, json.dumps(diffs[:1])
    print(
        f'PASS: {len(commands)} original inventory item capability results + {len(cache_commands)} expiry observations + {len(cache_commands)} timer traces + {len(http_commands)} HTTP cases'
    )


if __name__ == '__main__':
    main()
