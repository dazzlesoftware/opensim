# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Original Commands.FromXml, nullable metadata, partial trees and re-export."""

from pathlib import Path
import argparse, subprocess, json, hashlib, random, itertools, xml.etree.ElementTree as ET
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
code = r"""using System;using System.Collections.Generic;using System.Text.RegularExpressions;using System.Xml;using System.Linq;
public interface ICommands {} public delegate void CommandDelegate(string module,string[] words);
PARSER
COMMANDS
class Program {
static string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());
static string Encode(string value)=>value==null?"-":string.Concat(value.Select(c=>((int)c).ToString("x4")));
static string List(IEnumerable<string> values)=>string.Concat(values.Select(value=>"|"+Encode(value)));
static void Tree(XmlNode node,System.Text.StringBuilder output){output.Append("|"+(int)node.NodeType+":"+Encode(node.Name)+":"+Encode(node.Value)+":"+(node is XmlElement element?(element.IsEmpty?"1":"0"):"-")+":"+(node.Attributes?.Count??0)+":"+node.ChildNodes.Count);if(node.Attributes!=null)foreach(XmlNode attribute in node.Attributes)Tree(attribute,output);foreach(XmlNode child in node.ChildNodes)Tree(child,output);}
static void Rewrite(XmlNode node){if(node==null)return;if(node.Value=="~NULL~")node.Value=null;else if(node.Value!=null&&node.Value.Contains("~NUL~"))node.Value=node.Value.Replace("~NUL~","\0");foreach(XmlNode child in node.ChildNodes)Rewrite(child);}
static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;string row;while((row=Console.ReadLine())!=null){int tab=row.IndexOf('\t'),flags=int.Parse(row.Substring(0,tab));var input=row.Substring(tab+1);var commands=new Commands();var events=new List<string>();CommandDelegate old=(module,words)=>events.Add("old:"+Encode(module)+List(words)),imported=(module,words)=>events.Add("new:"+Encode(module)+List(words));foreach(var name in new[]{"help","quit","old","old nested","help oldchild","quit oldchild"}){if(name=="help"&&(flags&1)!=0)continue;if(name=="quit"&&(flags&2)!=0)continue;commands.AddCommand("Original",false,name,name+" usage","Original long","",old);}for(int attempt=0;attempt<2;attempt++){string status;var document=new XmlDocument();XmlElement root=null;try{if(input!="-"){document.LoadXml(Decode(input));root=document.DocumentElement;Rewrite(root);}status="LOADOK";}catch(Exception){status="LOADEX";}if(status=="LOADOK")try{commands.FromXml(root,(flags&16)!=0?null:imported);status="OK";}catch(Exception){status="FROMEX";}var output=new System.Text.StringBuilder(status);
foreach(var query in new[]{"help","quit","old","run","run value","run nested","a","missing",""}){events.Clear();var words=Parser.Parse(query);try{bool has=commands.HasCommand(query);var result=commands.Resolve(words);output.Append(";Q:"+(has?"1":"0")+":"+(Object.ReferenceEquals(result,words)?"1":"0")+List(words)+":"+List(events));}catch(Exception){output.Append(";QEX"+List(words)+":"+List(events));}}
foreach(var query in new[]{new[]{"help","all"},new[]{"help","Original"},new[]{"help","New"},new[]{"help","run"},new[]{"help","run","nested"}})try{output.Append(";H"+List(commands.GetHelp(query)));}catch(Exception){output.Append(";HEX");}
try{output.Append(";N"+List(commands.FindNextOption(Array.Empty<string>(),false)));}catch(Exception){output.Append(";NEX");}try{var exported=commands.GetXml(new XmlDocument());output.Append(";X");Tree(exported,output);}catch(Exception){output.Append(";XEX");}Console.Write(output.ToString()+(attempt==0?"~":"\n"));}}}
}
""".replace(
    'PARSER', extract(source, 'public class Parser')
).replace(
    'COMMANDS', extract(source, 'public class Commands')
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
samples = [
    None,
    '<HelpTree/>',
    '<HelpTree><Level Name="run"><Command/></Level></HelpTree>',
    '<HelpTree><Command/></HelpTree>',
    '<HelpTree><Level><Command/></Level></HelpTree>',
    '<HelpTree><?Command raw?></HelpTree>',
    '<HelpTree><Level Name="a"/><?Level raw?></HelpTree>',
    '<HelpTree><Level Name="run"><Command><Module/></Command></Level></HelpTree>',
    '<HelpTree><Level Name="help"><Command><Module>New</Module></Command><Level Name="new"><Command/></Level></Level><Level Name="quit"><Command/></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command/><Command><Module>New</Module><HelpText>U</HelpText></Command></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command/></Level><Level Name="run"><Level Name="nested"><Command/></Level></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command><?Module New?><?HelpText H?><LongHelp>a<x>b</x><![CDATA[c]]><!--ignored--><?pi ignored?>d</LongHelp></Command></Level></HelpTree>',
    '<p:HelpTree xmlns:p="urn:x"><Level xmlns="urn:x" Name="run"><Command><Module>New</Module></Command></Level><p:Level Name="a"><Command/></p:Level></p:HelpTree>',
    '<!DOCTYPE HelpTree [<!ENTITY v "true">]><HelpTree><Level Name="run"><Command><Shared>&v;</Shared></Command></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command><Shared>bad</Shared><Shared>true</Shared></Command></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command/></Level><broken>',
]
samples += [
    '<HelpTree><Level Name="run"><Command><?Module ~NULL~?><?Shared ~NULL~?><?HelpText ~NULL~?><?LongHelp ~NULL~?><?Description ~NULL~?></Command></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command><Module>~NULL~</Module><Shared>~NULL~</Shared></Command></Level></HelpTree>',
    '<HelpTree><Level Name="run"><Command><Module><![CDATA[~NULL~]]></Module><HelpText>~NULL~</HelpText><Description>~NULL~</Description></Command></Level></HelpTree>',
]
for mask in range(32):
    fields = ''.join(
        '<' + key + '>' + value + '</' + key + '>'
        for i, (key, value) in enumerate(
            [
                ('Module', 'New'),
                ('Shared', 'true'),
                ('HelpText', 'U&lt;&amp;&gt;'),
                ('LongHelp', ''),
                ('Description', ''),
            ]
        )
        if mask & (1 << i)
    )
    samples.append(
        '<HelpTree><Level Name="run"><Command>'
        + fields
        + '</Command><Level Name="nested"><Command/></Level></Level></HelpTree>'
    )
for value in [
    'true',
    'True',
    'FALSE',
    ' false ',
    '',
    '1',
    'yes',
    'Falſe',
    '~NUL~true~NUL~',
    '~NUL~false~NUL~',
    'tru~NUL~e',
    '&#xA0;true&#x2028;',
    '<![CDATA[true]]>',
    ' tr<x>u</x>e ',
    '<?pi true?>',
    '<!--true-->',
    'false~NUL~x',
]:
    samples.append(
        '<HelpTree><Level Name="run"><Command><Shared>'
        + value
        + '</Shared></Command></Level></HelpTree>'
    )
cases = [(flags, xml) for flags in [0, 1, 2, 3, 16, 17, 18, 19] for xml in samples]
rng = random.Random(91334)
for _ in range(600):
    fields = []
    for _ in range(rng.randrange(8)):
        name = rng.choice(['Module', 'Shared', 'HelpText', 'LongHelp', 'Description', 'ignored'])
        value = rng.choice(
            ['', 'New', 'true', 'false', 'a&lt;&amp;é', 'bad', '<x>true</x>', '~NUL~true~NUL~']
        )
        fields.append('<' + name + '>' + value + '</' + name + '>')
    command = '<Command>' + ''.join(fields) + '</Command>'
    xml = (
        '<HelpTree><Level Name="'
        + rng.choice(['run', 'a', 'help', 'quit', ''])
        + '">'
        + command
        + '</Level><Level Name="run"><Level Name="nested">'
        + command
        + '</Level></Level></HelpTree>'
    )
    cases.append((rng.choice([0, 0, 0, 16, 1, 2]), xml))
payload = (
    '\n'.join(
        str(flags) + '\t' + ('-' if xml is None else xml.encode('utf-16-be').hex())
        for flags, xml in cases
    )
    + '\n'
)
(work / 'input.txt').write_text(payload, encoding='utf-8')
outputs = {}
for name, command in [
    ('reference', [a.dotnet, str(work / 'app/Reference.dll')]),
    ('native', [str(Path(a.native).resolve())]),
]:
    run = subprocess.run(
        command, input=payload, capture_output=True, text=True, encoding='utf-8', timeout=180
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
    importAttempts=2 * len(cases),
    differenceCount=len(differences),
    differences=differences[:8],
    applicationParityVerified=False,
    scope='Original FromXml with nullable metadata, two imports/re-exports, callback/module and returned-array identity, stale category index, completion and partial failure. Concrete DOM/reader only; remote transport and full simulator parity not proved.',
    sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest(),
)
(work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], f"{report['differenceCount']} import differences; see results.json"
print(f'PASS: {len(cases)} original console XML import scenarios')
