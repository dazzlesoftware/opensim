"""Compare native script HTTP throttling with the managed module's methods."""

import argparse
from pathlib import Path
import random
import subprocess
import uuid

from compare_lsl_runtime_api import ROOT, extract


def managed_probe(source):
    declarations = (
        'private struct ThrottleData',
        'public bool CheckThrottle(',
        'public void StopHttpRequest(',
    )
    methods = '\n'.join(extract(source, declaration) for declaration in declarations)
    return """
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using UUID = System.Guid;

static class Util
{
    public static double Now;
    public static double GetTimeStamp() => Now;
}

class HttpRequestClass
{
    public UUID ItemID { get; set; }
    public UUID ReqID { get; set; }
    public void Stop() {}
}

class Probe
{
    static float m_primPerSec, m_primBurst, m_primOwnerPerSec, m_primOwnerBurst;
    static readonly object m_mainLock = new();
    static readonly Dictionary<UUID, HttpRequestClass> m_pendingRequests = new();
    readonly ConcurrentDictionary<uint, ThrottleData> m_RequestsThrottle = new();
    readonly ConcurrentDictionary<UUID, ThrottleData> m_OwnerRequestsThrottle = new();
""" + methods + """

    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var probe = new Probe();
        var settings = Console.ReadLine().Split(' ');
        m_primPerSec = float.Parse(settings[0]);
        m_primBurst = float.Parse(settings[1]);
        m_primOwnerPerSec = float.Parse(settings[2]);
        m_primOwnerBurst = float.Parse(settings[3]);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var fields = line.Split(' ');
            var prim = uint.Parse(fields[1]);
            var owner = UUID.Parse(fields[2]);
            Util.Now = double.Parse(fields[3]);
            if (fields[0] == "S")
                probe.StopHttpRequest(prim, owner);
            else
                Console.WriteLine(probe.CheckThrottle(prim, owner) ? "1" : "0");
        }
    }
}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    module = ROOT / 'OpenSim/Region/CoreModules/Scripting/HttpRequest/ScriptsHttpRequests.cs'
    (work / 'Probe.cs').write_text(managed_probe(module.read_text(encoding='utf-8-sig')), encoding='utf-8')
    project = work / 'Probe.csproj'
    project.write_text(
        '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType>'
        '<TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>',
        encoding='utf-8',
    )
    subprocess.run([args.dotnet, 'build', str(project), '--nologo', '-o', str(work / 'out')], check=True)

    generator = random.Random(718271)
    observations = 0
    for settings in ('1 3 25 5', '0 0 0 0', '.25 1.5 .5 2.25', '12 5 1 2', '0 6 0 1'):
        lines = [settings]
        now = 0
        for _ in range(4096):
            now += generator.choice((0, 0, 0, .0001, .01, .1, 1))
            operation = 'S' if generator.randrange(8) == 0 else 'A'
            prim = generator.randrange(6)
            owner = uuid.UUID(int=generator.randrange(5))
            lines.append(f'{operation} {prim} {owner} {now:.9f}')
        data = '\n'.join(lines) + '\n'
        managed = subprocess.check_output([args.dotnet, str(work / 'out/Probe.dll')], input=data, text=True)
        native = subprocess.check_output([args.native, '--http-throttle-probe'], input=data, text=True)
        expected = managed.splitlines()
        actual = native.splitlines()
        assert len(actual) == len(expected), f'{settings}: {len(actual)} replies, expected {len(expected)}'
        requests = [line for line in lines[1:] if line.startswith('A ')]
        for index, (wanted, received) in enumerate(zip(expected, actual)):
            assert wanted == received, f'{settings}: {requests[index]} returned {received}, expected {wanted}'
        observations += len(expected)
    print(f'PASS: {observations} managed script HTTP throttle decisions')


if __name__ == '__main__':
    main()
