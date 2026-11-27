"""Compare original BannedRegionCache and ExpiringCacheOS using a controlled clock and timer."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import uuid
import xml.etree.ElementTree as ET

from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run

MODULE = ROOT / 'OpenSim/Region/CoreModules/Framework/EntityTransfer/EntityTransferModule.cs'
CACHE = ROOT / 'OpenSim/Framework/ExpiringCacheOS.cs'


def build_reference(dotnet, work):
    original = extract(MODULE.read_text(encoding='utf-8-sig'), r'private class BannedRegionCache\b')
    wrapper = r'''
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading;
using OpenMetaverse;
using OpenSim.Framework;

namespace OpenSim.Framework {
    public static class Util {
        public static double GetTimeStamp() => F.NowMS * .001;
        public static double GetTimeStampMS() => F.NowMS;
    }
}
namespace System.Threading {
    public sealed class Timer : IDisposable {
        readonly TimerCallback callback;
        readonly object state;
        bool active = true;
        public Timer(TimerCallback cb, object value, int due, int period) {
            callback = cb; state = value;
            F.Trace.Add("new:" + due); F.Timers.Add(this);
        }
        public bool Change(int due, int period) {
            active = due >= 0; F.Trace.Add("change:" + due); return true;
        }
        public void Dispose() { active = false; F.Trace.Add("dispose"); }
        public void Fire() { if (active) { active = false; callback(state); } }
    }
}
static class F {
    public static double NowMS;
    public static List<string> Trace = new();
    public static List<System.Threading.Timer> Timers = new();
}
class Reference {
    ORIGINAL
    public static void Main() {
        string line;
        while ((line = Console.ReadLine()) != null) {
            F.NowMS = 0; F.Trace.Clear(); F.Timers.Clear();
            var cache = new BannedRegionCache();
            var results = new List<string>();
            using var document = JsonDocument.Parse(line);
            foreach (var op in document.RootElement.GetProperty("ops").EnumerateArray()) {
                string result = "ok";
                try {
                    string kind = op.GetProperty("op").GetString();
                    if (kind == "time") F.NowMS = op.GetProperty("value").GetDouble() * 1000;
                    else if (kind == "tick") foreach (var timer in F.Timers.ToArray()) timer.Fire();
                    else if (kind == "dispose") cache.Dispose();
                    else {
                        ulong region = ulong.Parse(op.GetProperty("region").GetString());
                        UUID agent = new UUID(op.GetProperty("agent").GetString());
                        if (kind == "add") cache.Add(region, agent, op.GetProperty("seconds").GetDouble());
                        else if (kind == "remove") cache.Remove(region, agent);
                        else result = cache.IfBanned(region, agent) ? "1" : "0";
                    }
                } catch (Exception) { result = "throw"; }
                results.Add(result);
            }
            Console.WriteLine(JsonSerializer.Serialize(new {results, timers = F.Trace}));
            cache.Dispose();
        }
    }
}
'''.replace('ORIGINAL', original)
    (work / 'Reference.cs').write_text(wrapper, encoding='utf-8')
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false').items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, 'ItemGroup')
    for path in (work / 'Reference.cs', CACHE):
        ET.SubElement(items, 'Compile', Include=str(path))
    for name in ('OpenMetaverse', 'OpenMetaverseTypes'):
        reference = ET.SubElement(items, 'Reference', Include=name)
        ET.SubElement(reference, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    ET.ElementTree(project).write(work / 'Reference.csproj')
    result = subprocess.run([dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'),
                             '--nologo', '-v', 'quiet'], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work)
    agents = [str(uuid.UUID(int=n)) for n in (0, 1, 2)]

    def operation(kind, region=1, agent=0, **values):
        return dict(op=kind, region=str(region), agent=agents[agent], **values)

    cases = []
    for seconds, at, tick in itertools.product((-1, 0, .001, 1, 15, 60),
                                               (0, .001, .999, 1, 14.999, 15, 59.999, 60, 60.001, 75),
                                               (False, True)):
        ops = [operation('add', seconds=seconds), operation('time', value=at)]
        if tick:
            ops.append(operation('tick'))
        ops += [operation('get'), operation('get'), operation('remove'), operation('get')]
        cases.append(dict(ops=ops))
    for handle in (0, 1, (1000 * 256) << 32, (1 << 64) - 1):
        cases.append(dict(ops=[operation('add', handle, 0, seconds=60), operation('time', value=30),
                              operation('add', handle, 1, seconds=60), operation('time', value=61),
                              operation('tick'), operation('get', handle, 0), operation('get', handle, 1),
                              operation('get', handle, 0), operation('remove', handle, 0),
                              operation('get', handle, 0), operation('dispose'), operation('get', handle, 0)]))
    rng = random.Random(153)
    for _ in range(400):
        ops = []
        now = 0
        for _ in range(40):
            kind = rng.choice(('add', 'get', 'get', 'remove', 'time', 'tick'))
            if kind == 'time':
                now += rng.choice((0, .001, 1, 15, 60))
                ops.append(operation(kind, value=now))
            elif kind == 'add':
                ops.append(operation(kind, rng.randrange(3), rng.randrange(3), seconds=rng.choice((-1, 0, 1, 60))))
            else:
                ops.append(operation(kind, rng.randrange(3), rng.randrange(3)))
        cases.append(dict(ops=ops))
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native], cases)
    differences = [dict(input=case, original=a, native=b) for case, a, b in zip(cases, original, native) if a != b]
    report = dict(passed=not differences, scenarios=len(cases), operations=sum(len(c['ops']) for c in cases),
                  differences=differences,
                  sourceHashes={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (MODULE, CACHE)},
                  scope='Exact original BannedRegionCache and full ExpiringCacheOS with actual SDK UUIDs. '
                        'Controlled seconds/millisecond clocks and manually fired timer callbacks; '
                        'sequential behavior, timer requests and disposal compared, not races or real timer scheduling.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} source/native crossing-ban scenarios')


if __name__ == '__main__':
    main()
