"""Compare the unchanged original transfer state machine with controlled sleep/logging."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    path = ROOT / 'OpenSim/Region/CoreModules/Framework/EntityTransfer/EntityTransferStateMachine.cs'
    original = path.read_text(encoding='utf-8-sig')
    source = re.sub(r'^using .*?;\s*$', '', original, flags=re.MULTILINE)
    source = source.replace('Thread.Sleep(100)', 'global::TransferFixture.Sleep(100)')
    source = 'using System;using System.Collections.Generic;using System.Reflection;using UUID=System.Guid;using log4net;\n' + source
    (work / 'Original.cs').write_text(source, encoding='utf-8')
    fixture = Path(__file__).with_name('entity_transfer_state_reference.cs').read_text(encoding='utf-8')
    util = (ROOT / 'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig')
    begin = util.index('public static bool IsOutsideView(')
    end = util.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (util[end] == '{') - (util[end] == '}')
        end += 1
    outside_method = util[begin:end]
    fixture = fixture.replace('/*OUTSIDE_VIEW*/', outside_method)
    (work / 'Fixture.cs').write_text(fixture, encoding='utf-8')
    project = work / 'Reference.csproj'
    project.write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework>'
                       '<OutputType>Exe</OutputType><StartupObject>TransferFixture</StartupObject>'
                       '</PropertyGroup></Project>')
    build = subprocess.run([args.dotnet, 'build', str(project), '-c', 'Release', '--nologo', '-v:q'],
                           capture_output=True, text=True, timeout=120)
    assert build.returncode == 0, build.stdout + build.stderr
    starts = ['', 's', 's u1', 's u1 u2', 's u3', 's u4', 's u5']
    scenarios = []
    for start in starts:
        for operation in ['s', 'r', 'a', 'w', 'm0 w', 'p0 w', 'p1 w'] + [f'u{n}' for n in range(-2, 10)]:
            for failure in ['', 'b1', 'l1', 'z1']:
                scenarios.append(f'{start} {failure} {operation} g r g'.strip())
    for count in [0, 1, 2, 398, 399, 400, 401]:
        for action in 'hxf':
            scenarios.append(f's u1 {action}{count} w g')
    scenarios += ['s u1 a a w', 's u1 a z1 a g', 'a s a u1 a a', 'm0 w p1 w', 's u1 h1 w r w',
                  's u1 h399 w', 's u1 h400 w', 's u1 x1 w', 's u1 f1 w']
    rng = random.Random(81124)
    operations = ['s', 'g', 'r', 'a', 'w', 'm0', 'm1', 'p0', 'p1', 'b0', 'b1', 'l0', 'l1', 'z0', 'z1']
    operations += [f'u{n}' for n in range(-1, 8)] + ['h1', 'h399', 'h400', 'x1', 'f1']
    scenarios += [' '.join(rng.choices(operations, k=16)) for _ in range(2000)]
    inputs = '\n'.join(scenarios) + '\n'
    (work / 'inputs.txt').write_text(inputs, encoding='utf-8')
    outputs = []
    for command in [[args.native], [args.dotnet, str(work / 'bin/Release/net8.0/Reference.dll')]]:
        result = subprocess.run(command, input=inputs, capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stdout + result.stderr
        outputs.append(result.stdout.splitlines())
    (work / 'managed-output.txt').write_text('\n'.join(outputs[1]) + '\n', encoding='utf-8')
    assert len(outputs[0]) == len(outputs[1]) == len(scenarios)
    for index, (native, managed) in enumerate(zip(*outputs)):
        assert native == managed, f'{index}: {scenarios[index]}\nNative: {native}\nSource: {managed}'
    observations = sum(len(line.split(';')) for line in outputs[0])
    outside_cases = []
    for bits in [0, 0x3f800000, 0x437f0000, 0x44000000, 0xbf800000, 0x7fc00000, 0x7f800000, 0xff800000]:
        for coordinate in [0, 1, 1000, 0xffffffff]:
            for size in [0, 1, 256, 512, -1, -2147483648, 2147483647]:
                outside_cases.append(f'{bits} {coordinate} {(coordinate+1)&0xffffffff} {coordinate} {coordinate} {size} 256 256 {size}')
    for _ in range(10000):
        values = [rng.getrandbits(32) for _ in range(5)]
        values += [rng.randint(-2147483648, 2147483647) for _ in range(4)]
        outside_cases.append(' '.join(map(str, values)))
    outside_input = '\n'.join(outside_cases) + '\n'
    outside_outputs = []
    for command in [[args.native, '--outside-view'],
                    [args.dotnet, str(work / 'bin/Release/net8.0/Reference.dll'), '--outside-view']]:
        result = subprocess.run(command, input=outside_input, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        outside_outputs.append(result.stdout.splitlines())
    assert len(outside_outputs[0]) == len(outside_outputs[1]) == len(outside_cases)
    for index, (native, managed) in enumerate(zip(*outside_outputs)):
        assert native == managed, f'Outside-view {index}: {outside_cases[index]}: {native} != {managed}'
    (work / 'outside-inputs.txt').write_text(outside_input)
    (work / 'outside-managed-output.txt').write_text('\n'.join(outside_outputs[1]) + '\n')
    report = dict(passed=True, scenarios=len(scenarios), observations=observations,
                  sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  controlledDependencies=['UUID identity', 'module settings', 'sleep', 'logging'],
                  actualRuntimeIntegrationVerified=False, outsideViewObservations=len(outside_cases),
                  outsideViewMethodSha256=hashlib.sha256(outside_method.encode()).hexdigest())
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {observations} original transfer-state observations in {len(scenarios)} scenarios')


if __name__ == '__main__':
    main()
