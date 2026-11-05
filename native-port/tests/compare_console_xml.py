# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Original Commands.GetXml DOM and post-export tree mutation, three exports."""
from pathlib import Path
import argparse,subprocess,json,hashlib,random,itertools,xml.etree.ElementTree as ET
from compare_inventory_asset_caps import extract
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
path=root/'OpenSim/Framework/Console/CommandConsole.cs';source=path.read_bytes().decode('utf-8-sig')
code=r"""using System;using System.Collections.Generic;using System.Text.RegularExpressions;using System.Xml;using System.Linq;
public interface ICommands {} public delegate void CommandDelegate(string module,string[] words);
PARSER
COMMANDS
class Program {
static string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());
static string Encode(string value)=>value==null?"-":string.Concat(value.Select(c=>((int)c).ToString("x4")));
static string List(IEnumerable<string> values)=>string.Concat(values.Select(value=>"|"+Encode(value)));
static void Tree(XmlNode node,System.Text.StringBuilder output){output.Append("|"+(int)node.NodeType+":"+Encode(node.Name)+":"+Encode(node.Value)+":"+(node is XmlElement element?(element.IsEmpty?"1":"0"):"-")+":"+(node.Attributes?.Count??0)+":"+node.ChildNodes.Count);if(node.Attributes!=null)foreach(XmlNode attribute in node.Attributes)Tree(attribute,output);foreach(XmlNode child in node.ChildNodes)Tree(child,output);}
static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;string row;while((row=Console.ReadLine())!=null){var fields=row.Split('\t');int flags=int.Parse(fields[0]);var commands=new Commands();for(int i=1;i<fields.Length;i++){var command=Decode(fields[i]);commands.AddCommand("Module "+i%3,i%2==0,command,command+" <&>","L<&>é\n"+i,i%2!=0?"Details '\"\r\n":"",null);}for(int attempt=0;attempt<3;attempt++){var document=new XmlDocument();if((flags&4)!=0)document.AppendChild(document.CreateElement("Existing"));var output=new System.Text.StringBuilder();try{var xml=commands.GetXml((flags&3)==attempt?null:document);output.Append("OK:"+(Object.ReferenceEquals(xml.OwnerDocument,document)?"1":"0")+":"+(xml.ParentNode!=null?"1":"0"));Tree(xml,output);}catch(Exception){output.Append("EX");}output.Append(";D:"+Encode(document.DocumentElement?.Name));try{output.Append(";N"+List(commands.FindNextOption(Array.Empty<string>(),false)));}catch(Exception){output.Append(";NEX");}try{output.Append(";H"+List(commands.GetHelp(new[]{"help","all"})));}catch(Exception){output.Append(";HEX");}try{output.Append(";C:"+(commands.HasCommand("help")?"1":"0")+":"+(commands.HasCommand("quit")?"1":"0"));}catch(Exception){output.Append(";CEX");}Console.Write(output.ToString()+(attempt<2?"~":"\n"));}}}
}
""".replace('PARSER',extract(source,'public class Parser')).replace('COMMANDS',extract(source,'public class Commands'))
(work/'Reference.cs').write_text(code,encoding='utf-8',newline='')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(work/'Reference.cs'));ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);assert build.returncode==0,build.stdout+build.stderr
cases=[]
for mask,flags in itertools.product(range(64),range(8)):
 commands=[value for i,value in enumerate(['help','quit','help topic','quit now','run','show users']) if mask&(1<<i)]
 cases.extend([(flags,commands),(flags,list(reversed(commands)))])
rng=random.Random(91023);pool=['help','quit','help topic','quit now','run','run fast','show users','show','shutdown','café test','soft\u00adhyphen','"one two"','"" nested','n\0ame','中 é','a&b <x>', 'a '*20+'end']
for _ in range(2000):
 commands=rng.sample(pool,rng.randrange(len(pool)+1));cases.append((rng.randrange(8),commands))
payload='\n'.join(str(flags)+''.join('\t'+command.encode('utf-16-be').hex() for command in commands) for flags,commands in cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8');outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 run=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=180);assert run.returncode==0,(name,run.stderr)
 (work/(name+'.txt')).write_text(run.stdout,encoding='utf-8');outputs[name]=run.stdout.splitlines();assert len(outputs[name])==len(cases)
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not differences,cases=len(cases),exportAttempts=3*len(cases),differenceCount=len(differences),differences=differences[:8],applicationParityVerified=False,scope='Original GetXml concrete XmlDocument DOM shape/value/ownership, three consecutive exports and post-export completion/help/HasCommand. Null-document and missing-help/quit partial mutations included. Non-null metadata only; XML import, serialization text, remote transport and full simulator parity not proved.',sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest())
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert report['passed'],report
print(f'PASS: {len(cases)} original console XML export scenarios')
