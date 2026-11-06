"""Whole source manager/request/args with controlled job, thread, clock and HTTP hosts."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    sources = [
        ROOT / 'OpenSim/Framework/Servers/HttpServer' / n
        for n in [
            'PollServiceRequestManager.cs',
            'PollServiceHttpRequest.cs',
            'PollServiceEventArgs.cs',
        ]
    ]
    files = []
    for source in sources:
        text = (
            source.read_text(encoding='utf-8-sig')
            .replace('Thread.Sleep(', 'Host.Sleep(')
            .replace('Environment.TickCount', 'Host.Tick')
        )
        if source.name == 'PollServiceHttpRequest.cs':
            text = text.replace('System.Host.Tick', 'Host.Started').replace(
                'UUID.Random()', 'UUID.Zero'
            )
        f = w / source.name
        f.write_text(text, encoding='utf-8')
        files.append(f)
    facade = (
        (ROOT / 'native-port/tests/poll_request_reference.cs')
        .read_text(encoding='utf-8')
        .split('class Program ')[0]
        .replace(
            'void Warn(string m,Exception e);',
            'void Warn(string m,Exception e);void Error(string m);',
        )
        .replace(
            'public class Logger:ILog{',
            'public class Logger:ILog{public void Error(string m)=>Capture.Event("error");',
        )
        .replace('Stream Body{get;}', 'Stream Body{get;}global::Context Context{get;}')
        .replace(
            'public class Incoming:IHttpRequest{',
            'public class Incoming:IHttpRequest{public global::Context Context{get{Capture.Event("context");return new global::Context();}}',
        )
    )
    f = w / 'Facades.cs'
    f.write_text(facade, encoding='utf-8')
    files += [f, ROOT / 'native-port/tests/poll_manager_reference.cs']
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in {
        'TargetFramework': 'net8.0',
        'OutputType': 'Exe',
        'EnableDefaultCompileItems': 'false',
    }.items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in files:
        ET.SubElement(group, 'Compile', Include=str(f))
    ref = ET.SubElement(group, 'Reference', Include='OpenMetaverseTypes')
    ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin/OpenMetaverseTypes.dll')
    cs = w / 'Manager.csproj'
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

    def add(
        can=1,
        sending=0,
        has=0,
        started=0,
        now=0,
        timeout=0,
        fail='',
        workers=2,
        pending=0,
        ops='start,job,watch,stop',
        null='none',
    ):
        cases.append(
            '|'.join(
                map(
                    str,
                    [can, sending, has, started, now, timeout, fail, workers, pending, ops, null],
                )
            )
        )

    for can, sending, has, elapsed, timeout in itertools.product(
        [0, 1], [0, 1], [0, 1], [-1, 0, 1, 30, 31], [0, 30]
    ):
        add(can=can, sending=sending, has=has, now=elapsed, timeout=timeout)
    for started, now, timeout in itertools.product(
        [-2147483648, -1, 0, 1, 2147483647],
        [-2147483648, -1, 0, 1, 2147483647],
        [-2147483648, -1, 0, 1, 2147483647],
    ):
        add(started=started, now=now, timeout=timeout)
    for ops in [
        'null,job,enqueue,stop',
        'start,start,enqueue,job,stop,stop,job',
        'start,job,job,watch,enqueue',
        'start,stop,start,job,watch',
        'start,job,stop',
        'start,job,job,stop',
        'start,enqueue,stop',
    ]:
        for can, sending, has in itertools.product([0, 1], [0, 1], [0, 1]):
            add(ops=ops, can=can, sending=sending, has=has)
    for fail, has, sending, can in itertools.product(
        [
            'context',
            'can',
            'sending',
            'has',
            'get',
            'no',
            'drop',
            'tick',
            'body',
            'read',
            'dispose',
            'send',
            'factory',
            'pool_dispose',
            'count',
            'watchdog',
            'enqueue',
            'watcher',
            'sleep:100',
            'pool:PollServiceWorker:4000:2',
        ],
        [0, 1],
        [0, 1],
        [0, 1],
    ):
        add(fail=fail, has=has, sending=sending, can=can, now=2, ops='start,enqueue,job,watch,stop')
    for null, has, can in itertools.product(
        ['nullhas', 'nullget', 'nullno', 'nulldrop'], [0, 1], [0, 1]
    ):
        add(null=null, has=has, can=can, now=3)
    for workers, pending in itertools.product(
        [0, 1, 2, 2147483647, 2147483648, 4294967295], [0, 1, 10]
    ):
        add(workers=workers, pending=pending)
    rows = []
    for command in [[a.dotnet, str(w / 'app/Manager.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(cases) + '\n', capture_output=True, text=True, timeout=90
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(cases)
    diffs = [{'case': c, 'source': s, 'native': n} for c, s, n in zip(cases, *rows) if s != n]
    (w / 'poll-manager-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(cases),
                'sources': {s.name: hashlib.sha256(s.read_bytes()).hexdigest() for s in sources},
                'scope': 'Whole original manager/request/args with clock/sleep projections, actual ConcurrentQueue and controlled job/thread/watchdog/HTTP/log facades. Source scheduling decisions, callback failures, retry draining, start/stop partial states and int32 wrap. Native actual threaded host, blocked callback/GC/worker lifecycle and async HTTP transport need independent verification.',
                'differences': diffs[:10],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    assert not diffs, json.dumps(diffs[:3])
    print(f'PASS: {len(cases)} polling manager source observations')


if __name__ == '__main__':
    main()
