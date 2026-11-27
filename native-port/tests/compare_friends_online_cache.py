"""Compare cache observations with methods extracted unchanged from FriendsModule."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet')
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = (Path(__file__).resolve().parents[2] /
              'OpenSim/Region/CoreModules/Avatar/Friends/FriendsModule.cs').read_text(encoding='utf-8-sig')
    methods = []
    for signature in ('public bool IsFriendOnline(', 'public void CacheFriendsOnline(',
                      'public virtual void CacheFriendOnline(', 'public virtual List<UUID> GetCachedFriendsOnline('):
        begin = source.index(signature)
        end = source.index('{', begin) + 1
        depth = 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        methods.append(source[begin:end])
    rng = random.Random(8112026)
    users = [f'00000000-0000-0000-0000-{n:012d}' for n in range(100, 104)]
    commands = []
    for user in users:
        commands += [f'Q {user}', f'B {user} 0 0', f'B {user} 1 0', f'E {user}',
                     f'B {user} 1 5 1 2 1 3 4', f'B {user} 0 2 2 3', f'B {user} 1 2 5 6']
    for unused in range(4000):
        user = rng.choice(users)
        operation = rng.choice('BBBBSSQE')
        if operation == 'B':
            ids = [rng.randrange(25) for unused in range(rng.randrange(12))]
            commands.append(f'B {user} {rng.randrange(2)} {len(ids)} ' + ' '.join(map(str, ids)))
        elif operation == 'S':
            commands.append(f'S {user} {rng.randrange(2)} {rng.randrange(25)}')
        else:
            commands.append(f'{operation} {user}')
    inputs = '\n'.join(commands) + '\n'
    (work / 'inputs.txt').write_text(inputs)
    if args.reference:
        reference = args.reference.read_text().splitlines()
    else:
        if not args.dotnet:
            parser.error('--dotnet or --reference is required')
        fixture = '''using System;using System.Collections.Generic;using System.Linq;using UUID=System.Guid;
class Fixture {
Dictionary<UUID,HashSet<UUID>> m_OnlineFriendsCache = new();
METHODS
static UUID Id(int n) => UUID.Parse("00000000-0000-0000-0000-" + n.ToString("D12"));
static void Main() {
var f=new Fixture(); string line;
while((line=Console.ReadLine())!=null) {
var p=line.Split(' ',StringSplitOptions.RemoveEmptyEntries); var u=UUID.Parse(p[1]);
if(p[0]=="B") f.CacheFriendsOnline(u,p.Skip(4).Select(x=>Id(int.Parse(x))).ToList(),p[2]=="1");
else if(p[0]=="S") f.CacheFriendOnline(u,Id(int.Parse(p[3])),p[2]=="1");
else if(p[0]=="E") f.m_OnlineFriendsCache.Remove(u);
var result=f.GetCachedFriendsOnline(u);
Console.WriteLine((result==null?"NULL":string.Join(",",result))+"|"+
string.Concat(Enumerable.Range(0,25).Select(n=>f.IsFriendOnline(u,Id(n))?"1":"0")));
}}}'''.replace('METHODS', '\n'.join(methods))
        (work / 'Fixture.cs').write_text(fixture)
        project = work / 'Oracle.csproj'
        project.write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework>'
                           '<OutputType>Exe</OutputType><EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
                           '</PropertyGroup><ItemGroup><Compile Include="Fixture.cs" /></ItemGroup></Project>')
        build = subprocess.run([args.dotnet, 'build', str(project), '-c', 'Release', '--nologo', '-v:q'],
                               capture_output=True, text=True, timeout=90)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        result = subprocess.run([args.dotnet, str(work / 'bin/Release/net8.0/Oracle.dll')], input=inputs,
                                capture_output=True, text=True, timeout=30, check=True)
        reference = result.stdout.splitlines()
    (work / 'reference.txt').write_text('\n'.join(reference) + '\n')
    native = subprocess.run([args.native], input=inputs, capture_output=True, text=True, timeout=30, check=True)
    observations = native.stdout.splitlines()
    if observations != reference:
        for n, (actual, expected) in enumerate(zip(observations, reference)):
            if actual != expected:
                raise AssertionError(f'Observation {n}: {commands[n]}: {actual} != {expected}')
        raise AssertionError(f'Observation counts {len(observations)} != {len(reference)}')
    report = dict(passed=True, observations=len(reference),
                  originalMethodsSha256=hashlib.sha256('\n'.join(methods).encode()).hexdigest(),
                  inputsSha256=hashlib.sha256(inputs.encode()).hexdigest(),
                  referenceSha256=hashlib.sha256(('\n'.join(reference)+'\n').encode()).hexdigest())
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(reference)} original friend-online cache observations')


if __name__ == '__main__':
    main()
