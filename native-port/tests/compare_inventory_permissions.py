"""Compare the original XInventory permission reply method with the native connector."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from compare_presence_endpoint import ROOT, extract


def build_reference(dotnet, work):
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    text = source.read_text(encoding='utf-8-sig')
    method = extract(text, r'public int GetAssetPermissions\(UUID principalID, UUID assetID\)')
    wrapper = '''using System;using System.Collections.Generic;using UUID=System.Guid;
class Reference {
string reply="";
Dictionary<string,object> MakeRequest(string request) => new() {{"RESULT",reply}};
METHOD
static void Main() { var reference=new Reference(); string line;
while((line=Console.ReadLine())!=null) {reference.reply=line;
Console.WriteLine(reference.GetAssetPermissions(Guid.Empty,Guid.Empty));}}
}'''.replace('METHOD', method)
    (work / 'Reference.cs').write_text(wrapper, encoding='utf-8')
    project = work / 'Reference.csproj'
    project.write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                       '<TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType>'
                       '<EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
                       '</PropertyGroup><ItemGroup><Compile Include="Reference.cs"/>'
                       '</ItemGroup></Project>')
    result = subprocess.run([dotnet, 'build', str(project), '-o', str(work / 'app'),
                             '--nologo', '-v', 'quiet'], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


def run(command, cases):
    result = subprocess.run(command, input='\n'.join(cases) + '\n', capture_output=True,
                            text=True, encoding='utf-8', timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    values = [int(line) for line in result.stdout.splitlines()]
    assert len(values) == len(cases), (len(values), len(cases), result.stderr)
    return values


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work)
    cases = ['', 'false', 'true', '0', '-0', '+0', '123', '+123', '-123',
             '2147483647', '2147483648', '-2147483648', '-2147483649',
             '4294967295', '4294967296', '18446744073709551615',
             '  +123  ', '\t-2147483648\t', '+ 123', '- 123', '++1', '--1',
             '+-1', '-+1', '1.0', '1,000', '0x1', '12junk',
             '\u00a0123\u00a0', '\u2003123\u2003', '\uff11\uff12\uff13']
    for boundary in [-2147483648, 0, 2147483647, 4294967295]:
        cases += [str(boundary + offset) for offset in range(-4, 5)]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--permissions-probe'], cases)
    differences = [dict(input=value, original=a, native=b)
                   for value, a, b in zip(cases, original, native) if a != b]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
                  sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  scope='Exact extracted original GetAssetPermissions method; native public connector with XML reply')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, differences
    print(f'PASS: {len(cases)} original/native inventory permission reply observations')
