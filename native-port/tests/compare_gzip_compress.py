"""Actual .NET GZipStream sender write/dispose pattern and bidirectional native interoperability."""

import argparse, gzip, hashlib, json, random, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    ET.SubElement(
        ET.SubElement(project, 'ItemGroup'),
        'Compile',
        Include=str(ROOT / 'native-port/tests/gzip_compress_reference.cs'),
    )
    path = w / 'Gzip.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            a.dotnet,
            'build',
            str(path),
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
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    rng = random.Random(80411)
    payloads = [
        b'',
        b'{}',
        b'hello world',
        bytes(range(256)),
        b'\0' * 150000,
        '{"name":"試験 😀","serviceurls":{"Large":"'.encode() + b'x' * 150000 + b'"}}',
    ]
    for n in [
        1,
        2,
        3,
        7,
        31,
        128,
        255,
        256,
        257,
        4095,
        4096,
        8191,
        8192,
        8193,
        16383,
        16384,
        32767,
        32768,
        32769,
        65534,
        65535,
        65536,
        65537,
        131071,
        131072,
        262144,
    ]:
        payloads.extend([b'x' * n, bytes(i % 251 for i in range(n)), rng.randbytes(n)])
    managed = [a.dotnet, str(w / 'app/Gzip.dll')]

    def run(cmd, commands):
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=120
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-1000:] + r.stderr)
        result = r.stdout.splitlines()
        assert len(result) == len(commands)
        return result

    commands = ['C ' + (b.hex() or '.') for b in payloads]
    source = run(managed, commands)
    native = run([a.native], commands)
    differences = []
    for index, (data, x, y) in enumerate(zip(payloads, source, native)):
        if x != y:
            differences.append(
                {
                    'index': index,
                    'inputLength': len(data),
                    'sourceLength': len(x) // 2,
                    'nativeLength': len(y) // 2,
                    'sourcePrefix': x[:80],
                    'nativePrefix': y[:80],
                }
            )
        assert gzip.decompress(bytes.fromhex(y)) == data
    n_to_source = run(managed, ['D ' + value for value in native])
    source_to_n = run([a.native], ['D ' + value for value in source])
    assert n_to_source == source_to_n == [data.hex() for data in payloads]
    parallel_payload = bytes(i % 251 for i in range(65536))
    parallel = subprocess.run(
        [a.native, 'parallel'],
        input=parallel_payload.hex() + '\n',
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert parallel.returncode == 0, parallel.stderr
    assert parallel.stdout.splitlines() == [source[payloads.index(parallel_payload)]] * 16
    report = {
        'parallelFirstCalls': 16,
        'passed': not differences,
        'payloads': len(payloads),
        'interopObservations': len(payloads) * 3,
        'exactCompressedBytes': not differences,
        'sourceHash': hashlib.sha256(
            (ROOT / 'OpenSim/Framework/WebUtil.cs').read_bytes()
        ).hexdigest(),
        'scope': 'Actual .NET 8 GZipStream compress write/dispose used by WebUtil, raw binary boundaries/entropy and bidirectional .NET/native/Python decode. Exact output verified for reference runtime/profile; other .NET/zlib versions, platform gzip header and >4GiB input remain pending.',
        'differences': differences[:6],
    }
    (w / 'gzip-compress-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if differences:
        raise AssertionError(str(differences[:3]) + ' count=' + str(len(differences)))
    print(
        f'PASS: {len(payloads)} gzip compression byte comparisons and {len(payloads)*3} interoperability observations'
    )


if __name__ == '__main__':
    main()
