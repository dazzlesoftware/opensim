"""Reproduce invariant resources from the pinned test-only .NET runtime."""

from pathlib import Path
import argparse, json, subprocess, xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
source = work / 'Program.cs'
source.write_text(
    '''using System;using System.Reflection;using System.Text.Json;using System.Collections.Generic;using System.Text.RegularExpressions;
var type=typeof(Regex).Assembly.GetType("System.SR");var rows=new List<string[]>();foreach(var name in Enum.GetNames<RegexParseError>()){var property=type.GetProperty(name,BindingFlags.Static|BindingFlags.NonPublic|BindingFlags.Public);rows.Add(new[]{name,property==null?"":(string)property.GetValue(null)});}Console.WriteLine(JsonSerializer.Serialize(rows));
''',
    encoding='utf-8',
)
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for key, value in dict(
    TargetFramework='net8.0',
    OutputType='Exe',
    EnableDefaultCompileItems='false',
    RuntimeFrameworkVersion='8.0.31',
    RollForward='Disable',
).items():
    ET.SubElement(props, key).text = value
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=str(source))
ET.ElementTree(project).write(work / 'Reference.csproj')
build = subprocess.run(
    [args.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
(work / 'build.txt').write_text(build.stdout + build.stderr, encoding='utf-8')
assert build.returncode == 0, build.stdout + build.stderr
run = subprocess.run([args.dotnet, str(work / 'app/Reference.dll')], capture_output=True, text=True)
assert run.returncode == 0, run.stderr
rows = json.loads(run.stdout)
assert len(rows) == 32 and rows[0] == ['Unknown', '']
lines = [
    '// Pinned .NET8.0.31 invariant RegexParser resources; MIT runtime license.',
    'constexpr std::u16string_view parse_messages[]={',
]
for name, value in rows:
    lines.append(' u' + json.dumps(value, ensure_ascii=True) + ', // ' + name)
lines.append('};')
args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
