"""Original Util DNS/endpoint helpers, complete cache and actual System.Net parser.
Only DNS results, cache timer delivery and monotonic time are controlled.
"""

import argparse, hashlib, ipaddress, json, random, re, subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    methods = [
        extract(source, r'public static IPAddress ' + n + r'\(')
        for n in ['GetHostFromDNS', 'GetHostFromURL']
    ]
    methods += [
        extract(source, r'public static IPEndPoint getEndPoint\(' + t + r' ')
        for t in ['IPAddress', 'string']
    ]
    text = (
        'using System;using System.Net;using System.Net.Sockets;namespace OpenSim.Framework{public static class Util{static ExpiringCacheOS<string,IPAddress> dnscache=new(30000);static readonly char[] SplitSlashColonArray={\'/\',\':\'};public static double GetTimeStampMS()=>DnsFixture.Clock();public static int CacheCount=>dnscache.Count;public static void Reset(){dnscache.Dispose();dnscache=new(30000);}public static void Clear()=>dnscache.Clear();'
        + ''.join(methods).replace('Dns.GetHostEntry(', 'DnsFixture.Lookup(')
        + '}}'
    )
    period = re.search(r'dnscache = new\((\d+)\)', source).group(1)
    text = text.replace('new(30000)', 'new(' + period + ')')
    declaration = re.search(
        r'public static readonly char\[\] SplitSlashColonArray = [^;]+;', source
    ).group()
    text = text.replace("static readonly char[] SplitSlashColonArray={'/',':'};", declaration)
    wrapper = w / 'Util.cs'
    wrapper.write_text(text, encoding='utf-8')
    cache = w / 'Cache.cs'
    cache.write_text(
        (ROOT / 'OpenSim/Framework/ExpiringCacheOS.cs')
        .read_text(encoding='utf-8-sig')
        .replace('using Timer = System.Threading.Timer;', 'using Timer = CacheFixtureTimer;'),
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [wrapper, cache, ROOT / 'native-port/tests/host_resolver_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    path = w / 'HostResolver.csproj'
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

    def t(s):
        return '-' if s is None else s.encode().hex() or '.'

    addresses = [
        None,
        '',
        '0',
        '1',
        '127.1',
        '127.0.1',
        '127.0.0.1',
        '0177.0.0.1',
        '0x7f000001',
        '4294967295',
        '4294967296',
        '0x',
        '0x0',
        '01',
        '08',
        '1.2.3.4.5',
        '1.2.3.',
        '1..2',
        '1.16777215',
        '1.16777216',
        '1.2.65535',
        '1.2.65536',
        '255.255.255.255',
        ' 127.0.0.1',
        '127.0.0.1 ',
        '127.0.0.1\0',
        '::',
        '::1',
        '[::1]',
        '[::1]:80',
        '[::1]:',
        '[::1]:0xFF',
        '[::1]:0x',
        '[::1]:0XFF',
        '[::1]:-1',
        '::1]',
        '[::1',
        '::1%',
        '::%3',
        'fe80::1%3',
        'fe80::1%4294967295',
        'fe80::1%4294967296',
        'fe80::1%foo',
        'fe80::1%foo bar',
        'fe80::1%foo/bar',
        '[fe80::1%5]:80',
        '2001:db8:0:0:1:0:0:1',
        '::ffff:192.0.2.1',
        '::192.0.2.1',
        '::ffff:0:192.0.2.1',
        'fe80::5efe:192.0.2.1',
        '2001:db8:1:2:0:5efe:192.0.2.1',
        '::192.168.1.01',
        '::192.168.01.1',
        '::1/64',
        '[::1]:65536',
    ]
    rng = random.Random(877)
    for _ in range(1000):
        n = rng.getrandbits(128)
        v = ipaddress.IPv6Address(n)
        addresses.extend([str(v), v.exploded])
    for _ in range(500):
        parts = [str(rng.randrange(1000)) for _ in range(rng.randrange(1, 6))]
        addresses.append('.'.join(parts))
    addresses += [
        '::1%[',
        '::1%5\0',
        '::1%5\0\0',
        '[::1%5\0]:80',
        '::1% 5',
        '::1%+5',
        '::1%5:80',
        '[::1%scope?]:123',
        '[::1%foo[bar]:80',
    ]
    commands = ['p ' + t(s) for s in addresses]
    commands += ['reset']
    for host in [
        None,
        '',
        ' ',
        '\u00a0',
        '127.1',
        '0',
        '::',
        '::%3',
        'mixed',
        'v6',
        'any',
        'throw',
        'empty',
        'null',
        'badlist',
        'later-null',
        'first-v4-null',
        'MIXED',
    ]:
        commands += ['h ' + t(host), 'h ' + t(host)]
    for host in ['127.0.0.1', 'mixed', 'v6', 'any']:
        for port in [-1, 0, 65535, 65536]:
            commands += ['e ' + t(host) + ' ' + str(port)]
    for url in [
        None,
        '',
        'http://mixed:9000/path',
        'http://v6/',
        'http:///mixed',
        'wrong',
        '://mixed',
        'http://[::1]:9000/',
    ]:
        commands += ['u ' + t(url)]
    commands += [
        'reset',
        'h ' + t('mixed'),
        'time 300000',
        'purge',
        'h ' + t('mixed'),
        'time 600001',
        'purge',
        'h ' + t('mixed'),
        'time 900002',
        'h ' + t('mixed'),
        'purge',
        'h ' + t('mixed'),
        'clear',
        'h ' + t('mixed'),
    ]
    commands += [
        'reset',
        'h ' + t('v6'),
        'scope 9',
        'e ' + t('v6') + ' 80',
        'scope 11',
        'h ' + t('v6'),
        'scope -1',
        'scope 4294967296',
        'h ' + t('mixed'),
        'v4 -1',
        'e ' + t('mixed') + ' 1',
        'v4 0',
        'h ' + t('mixed'),
        'scope 3',
    ]
    for moment in [
        '-1000',
        '2147183647',
        '2147483647',
        '2147483648',
        '4294967296',
        'NaN',
        'Infinity',
    ]:
        commands += ['reset', 'time ' + moment, 'h ' + t('mixed'), 'purge', 'h ' + t('mixed')]
    for address in ['0.0.0.0', '::', '::%5', 'invalid', None]:
        commands += ['d ' + t(address) + ' 9']
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/HostResolver.dll')], [a.native]]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=45,
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-500:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(commands, *rows) if x != y]
    system_commands = [
        'reset',
        'h ' + t('localhost'),
        'e ' + t('localhost') + ' 9000',
        'h ' + t('LOCALHOST'),
        'h ' + t('127.1'),
        'e ' + t('[::1]:80') + ' 9000',
        'h ' + t('bad\0host'),
        'h ' + t('bad/host'),
        'h ' + t('0.0.0.0'),
        'h ' + t('::'),
    ]
    system_rows = []
    for cmd in [[a.dotnet, str(w / 'app/HostResolver.dll'), 'system'], [a.native, 'system']]:
        run = subprocess.run(
            cmd,
            input='\n'.join(system_commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=45,
        )
        if run.returncode:
            raise RuntimeError(run.stdout[-500:] + run.stderr)
        system_rows.append(run.stdout.splitlines())
    assert len(system_rows[0]) == len(system_rows[1]) == len(system_commands)
    diffs += [
        {'systemInput': c, 'source': x, 'native': y}
        for c, x, y in zip(system_commands, *system_rows)
        if x != y
    ]

    runtime = subprocess.run(
        [a.dotnet, str(w / 'app/HostResolver.dll'), 'assembly'],
        capture_output=True,
        text=True,
        encoding='utf-8',
        check=True,
    )
    library = json.loads(runtime.stdout)
    library['sha256'] = hashlib.sha256(Path(library.pop('path')).read_bytes()).hexdigest()
    (w / 'host-resolver-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands) + len(system_commands),
                'systemObservations': len(system_commands),
                'runtimeLibrary': library,
                'sourceHashes': {
                    s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                    for s in ['OpenSim/Framework/Util.cs', 'OpenSim/Framework/ExpiringCacheOS.cs']
                },
                'differences': diffs[:8],
            },
            indent=2,
        )
        + '\n',
        encoding='utf-8',
    )
    if diffs:
        raise AssertionError(json.dumps(diffs[:3], indent=2))
    print(f'PASS: {len(commands)+len(system_commands)} host resolver source observations')


if __name__ == '__main__':
    main()
