# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Original Commands help and completion, preserving source literal line endings."""

from pathlib import Path
import argparse, subprocess, json, hashlib, random, xml.etree.ElementTree as ET
from compare_inventory_asset_caps import extract

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--dotnet', required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
work = a.work.resolve()
work.mkdir(parents=True, exist_ok=True)
path = root / 'OpenSim/Framework/Console/CommandConsole.cs'
source = path.read_bytes().decode('utf-8-sig')
commands = [
    'login level',
    'login reset',
    'login text',
    'show',
    'show users',
    'show user',
    'shutdown',
    'quit',
    'fcache clear',
    'fcache clearnegatives',
    'fcache status',
    'café test',
    'café other',
    'x-y',
    'x-z',
    'soft\u00adhyphen tail',
]
commands += ['extra ' + str(i) for i in range(40)]
code = (
    r"""using System;using System.Collections.Generic;using System.Text.RegularExpressions;using System.Xml;using System.Linq;
public interface ICommands {} public delegate void CommandDelegate(string module,string[] words);
PARSER
COMMANDS
class Program {
static string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());
static string Encode(string value)=>string.Concat(value.Select(c=>((int)c).ToString("x4")));
static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;var commands=new Commands();var modules=new[]{"General","Users","Assets","café","CAFÉ","İ","I"};int i=0;foreach(var name in NAMES){commands.AddCommand(modules[i%modules.Length],i%2==0,name,name+" <value>","Description "+i,i%3==0?"Details\nLine":"",(m,w)=>{});i++;}string row;while((row=Console.ReadLine())!=null){var parts=row.Split('\t',2);var words=Parser.Parse(Decode(parts[1]));try{var values=parts[0]=="H"?commands.GetHelp(words).ToArray():commands.FindNextOption(words,parts[0]=="N1");Console.Write("L");foreach(var value in values)Console.Write("|"+Encode(value));Console.WriteLine();}catch(Exception){Console.WriteLine("EX");}}}
}
""".replace(
        'PARSER', extract(source, 'public class Parser')
    )
    .replace('COMMANDS', extract(source, 'public class Commands'))
    .replace(
        'NAMES',
        'new string[]{' + ','.join(json.dumps(c, ensure_ascii=False) for c in commands) + '}',
    )
)
(work / 'Reference.cs').write_text(code, encoding='utf-8', newline='')
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
ET.SubElement(ET.SubElement(project, 'ItemGroup'), 'Compile', Include=str(work / 'Reference.cs'))
ET.ElementTree(project).write(work / 'Reference.csproj')
build = subprocess.run(
    [a.dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'), '-v:q'],
    capture_output=True,
    text=True,
)
assert build.returncode == 0, build.stdout + build.stderr
cases = []
for value in [
    '',
    'help',
    'help all',
    'help ALL',
    'help Users',
    'help USERS',
    'help café',
    'help CAFÉ',
    'help İ',
    'help I',
    'help login',
    'help login level',
    'help lo le',
    'help show missing',
    'help show ""',
    'help ""',
    'help unknown',
    'help Assets',
]:
    cases.append(('H', value))
for name in commands:
    for length in range(len(name) + 1):
        for tail in ['', ' arg', ' ""', ' extra words']:
            for mode in ['H', 'N0', 'N1']:
                cases.append((mode, ('help ' if mode == 'H' else '') + name[:length] + tail))
rng = random.Random(1720)
parts = [
    'help',
    'all',
    'Users',
    'USERS',
    'General',
    'Assets',
    'İ',
    'I',
    'café',
    'CAFÉ',
    'login',
    'lo',
    'l',
    'level',
    'le',
    'reset',
    'text',
    'sh',
    'show',
    'user',
    'users',
    'u',
    'shutdown',
    'quit',
    'fcache',
    'cl',
    'clear',
    'clearnegatives',
    'status',
    '',
    '""',
    '"hello world"',
    'café',
    '\u00ad',
    'soft',
    'x',
    'x-y',
    'X',
    '\0',
]
for _ in range(12000):
    cases.append(
        (
            rng.choice(['H', 'N0', 'N1']),
            ' '.join(rng.choice(parts) for _ in range(rng.randrange(0, 7))),
        )
    )
payload = '\n'.join(mode + '\t' + value.encode('utf-16-be').hex() for mode, value in cases) + '\n'
(work / 'input.txt').write_text(payload, encoding='utf-8')
outputs = {}
for name, command in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve()), '--help']),
]:
    run = subprocess.run(
        command, input=payload, capture_output=True, text=True, encoding='utf-8', timeout=120
    )
    assert run.returncode == 0, (name, run.stderr)
    (work / (name + '.txt')).write_text(run.stdout, encoding='utf-8')
    outputs[name] = run.stdout.splitlines()
    assert len(outputs[name]) == len(cases)
differences = [
    dict(index=i, input=cases[i], reference=x, native=y)
    for i, (x, y) in enumerate(zip(outputs['reference'], outputs['native']))
    if x != y
]
report = dict(
    passed=not differences,
    cases=len(cases),
    differenceCount=len(differences),
    differences=differences[:20],
    applicationParityVerified=False,
    scope='Complete unchanged original Commands class help/completion with fixed metadata. Source CRLF in verbatim ItemHelpText preserved. Callback execution/XML import-export and interactive editing remain separate.',
    sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest(),
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], report
print(f'PASS: {len(cases)} original console help and completion observations')
