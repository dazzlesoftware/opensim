"""Complete original event queue core with controlled scene, caps, logger and RNG."""

import argparse, hashlib, itertools, json, random, subprocess, xml.etree.ElementTree as ET
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
    source = ROOT / 'OpenSim/Region/ClientStack/Linden/Caps/EventQueue/EventQueueGetModule.cs'
    s = (
        source.read_text(encoding='utf-8-sig')
        .replace(
            'private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);',
            'private static readonly global::QueueLog m_log = new();',
        )
        .replace('UUID.Random()', 'QueueHost.RandomUUID()')
        .replace('Random.Shared.Next(30000000)', 'QueueHost.Next(30000000)')
    )
    wrapper = w / 'Module.cs'
    wrapper.write_text(s, encoding='utf-8')
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    handlers = ROOT / 'OpenSim/Region/ClientStack/Linden/Caps/EventQueue/EventQueueGetHandlers.cs'
    h = w / 'Handlers.cs'
    h.write_text(
        handlers.read_text(encoding='utf-8-sig').replace('UUID.Random()', 'QueueHost.RandomUUID()'),
        encoding='utf-8',
    )
    interfaces = ROOT / 'OpenSim/Region/Framework/Interfaces/IEventQueue.cs'
    source_interface = interfaces.read_text(encoding='utf-8-sig')
    record = source_interface[
        source_interface.index(
            '    public struct GroupChatListAgentUpdateData'
        ) : source_interface.index('    public interface IEventQueue')
    ]
    dto = w / 'GroupChat.cs'
    dto.write_text(
        'using OpenMetaverse;namespace OpenSim.Region.Framework.Interfaces{' + record + '}',
        encoding='utf-8',
    )
    for f in [
        wrapper,
        h,
        dto,
        ROOT / 'OpenSim/Framework/GroupData.cs',
        ROOT / 'native-port/tests/event_queue_reference.cs',
        ROOT / 'native-port/tests/event_queue_producers_reference.cs',
        ROOT / 'OpenSim/Framework/Servers/HttpServer/PollServiceEventArgs.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in [
        'OpenMetaverse',
        'OpenMetaverseTypes',
        'OpenMetaverse.StructuredData',
        'Nini',
        'log4net',
    ]:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    path = w / 'QueueReference.csproj'
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
    assert r.returncode == 0, r.stdout + r.stderr
    cases = [
        ''.join(seq) + 'GHT' for n in range(4) for seq in itertools.product('RABZMGHCDN', repeat=n)
    ]
    cases += [
        start + middle + 'GGHT'
        for start, middle in itertools.product(
            ['R0', 'RX', 'RY', 'RARR', 'RAN', 'NC'],
            ['AZ', 'AMBR', 'MRAB', 'RRRA', 'OG', 'UG', 'CR'],
        )
    ]
    rng = random.Random(721)
    cases += ['R' + ''.join(rng.choices('RABZMOGHCDNU0XY', k=40)) + 'GGHT' for _ in range(500)]
    cases += [
        f'@{p}:' + start + op + 'GHT'
        for p, start, op in itertools.product(
            range(64), ['R', 'RA', 'RCR', ''], 'efcaiurqgpjhwltvbks'
        )
    ]
    cases += ['RLGHT', 'RALLGHT', 'RLRLAGHT', 'RCLGHT']
    rows = []
    for command in [[a.dotnet, str(w / 'app/QueueReference.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    diffs = [{'case': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'event-queue-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sourceHashes': {
                    f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in [source, handlers, interfaces, ROOT / 'OpenSim/Framework/GroupData.cs']
                },
                'scope': 'Whole original core and all producer method bodies; logger field and deterministic RNG projections, real SDK XML/UUID/InventoryCRC/UTF8 and managed queue/dictionary. Raw bytes compared as hex. Controlled scene/capability registration and DTO field facades. SDK buffer pools/capacity, invalid UTF16/encoding, diagnostics/reentrant/concurrent/runtime/platform parity and production world remain pending.',
                'differences': diffs[:5],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not diffs, diffs[:2]
    print(f'PASS: {len(cases)} event queue source observations')


if __name__ == '__main__':
    main()
