# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Actual original Commands callback registration, HasCommand and Resolve."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
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
static string Encode(string value)=>string.Concat(value.Select(c=>((int)c).ToString("x4")));
static string List(IEnumerable<string> values)=>string.Join(",",values.Select(Encode));
static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;string row;while((row=Console.ReadLine())!=null){var fields=row.Split('\t',2);int seed=int.Parse(fields[0]);var query=Decode(fields[1]);var commands=new Commands();var events=new List<string>();CommandDelegate a=null,b=null,c=null;
c=(module,words)=>events.Add("C:"+module);
a=(module,words)=>{events.Add("A:"+module);if((seed&32)!=0)words[0]="changed";if((seed&64)!=0)throw new Exception("callback failure");if((seed&128)!=0)commands.AddCommand("New",false,"run","run","description","",c);if((seed&256)!=0)commands.AddCommand("New",false,"run","run","description","",a);if((seed&512)!=0)commands.AddCommand("Other",false,"other","other","","",c);};
b=(module,words)=>{events.Add("B:"+module);if((seed&1024)!=0&&words.Length>1)words[1]="tail";if((seed&2048)!=0)throw new Exception("callback failure");};
var first=(seed&2)!=0?null:a;commands.AddCommand("Original é",(seed&1)!=0,"run","run","description","",first);if((seed&4)!=0)commands.AddCommand("Duplicate",false,"run","run","description","",first);if((seed&8)!=0)commands.AddCommand("New",false,"run","run","description","",b);if((seed&16)!=0)commands.AddCommand("New",false,"run","run","description","",null);if((seed&4096)!=0)commands.AddCommand("Other",false,"rush","rush","","",c);
for(int iteration=0;iteration<2;++iteration){var words=Parser.Parse(query);var result=Array.Empty<string>();bool has=false,aliased=false;string status="OK";try{has=commands.HasCommand(query);result=commands.Resolve(words);aliased=Object.ReferenceEquals(result,words);}catch(Exception){status="EX";}Console.Write(status+";"+(has?"1":"0")+";"+(aliased?"1":"0")+";"+List(result)+";"+List(words)+";"+List(events)+(iteration==0?"/":"\n"));}}}
}
""".replace('PARSER',extract(source,'public class Parser')).replace('COMMANDS',extract(source,'public class Commands'))
(work/'Reference.cs').write_text(code,encoding='utf-8',newline='')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(work/'Reference.cs'));ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);assert build.returncode==0,build.stdout+build.stderr
cases=[(seed,query) for seed in range(8192) for query in ['run value','r value','run','run ""','unknown']]
payload='\n'.join(str(seed)+'\t'+query.encode('utf-16-be').hex() for seed,query in cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8');outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 run=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=180);assert run.returncode==0,(name,run.stderr)
 (work/(name+'.txt')).write_text(run.stdout,encoding='utf-8');outputs[name]=run.stdout.splitlines();assert len(outputs[name])==len(cases)
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not differences,cases=len(cases),invocationAttempts=2*len(cases),differenceCount=len(differences),differences=differences[:20],applicationParityVerified=False,scope='Unchanged original Commands callback registration/HasCommand/Resolve with shared and null/duplicate handlers, mutable argument array, exceptions and registrations during callbacks. Delegate identity represented by reused native callback handles; full console/XML/interactive parity not proved.',sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest())
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert report['passed'],report
print(f'PASS: {len(cases)} original console callback observations')
