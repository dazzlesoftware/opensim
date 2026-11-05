# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Original AddCommand tree and ResolveCommand, including aliased mutations."""
from pathlib import Path
import argparse,subprocess,json,hashlib,random,itertools,xml.etree.ElementTree as ET
from compare_inventory_asset_caps import extract
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
path=root/'OpenSim/Framework/Console/CommandConsole.cs';source=path.read_text(encoding='utf-8-sig')
marker='string descriptivehelp,';at=source.index(marker);start=source.rfind('public void AddCommand(',0,at)
add=extract(source[start:],'public void AddCommand(')
commands=['login level', 'login reset', 'login text', 'show', 'show users', 'show user', 'shutdown', 'quit', 'fcache clear', 'fcache clearnegatives', 'fcache status', 'café test', 'café other', 'x-y', 'x-z', 'soft\xadhyphen tail']
code='using System;using System.Collections.Generic;using System.Text.RegularExpressions;using System.Linq;\nPARSER\nclass Reference {\npublic delegate void CommandDelegate(string module,string[] words);\nINFO\nDictionary<string,object> tree=new();Dictionary<string,List<CommandInfo>> m_modulesCommands=new();\nADD\nRESOLVE\nstatic string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());\nstatic string Encode(string value)=>string.Concat(value.Select(c=>((int)c).ToString("x4")));\nstatic void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;var reference=new Reference();foreach(var name in NAMES)reference.AddCommand("Module",false,name,name,"","",(m,w)=>{});string row;while((row=Console.ReadLine())!=null){var words=Parser.Parse(Decode(row));string status;try{status=reference.ResolveCommand(words,out var result)==null?"0":"1";}catch(InvalidCastException){status="EX";}Console.Write(status);foreach(var word in words)Console.Write("|"+Encode(word));Console.WriteLine();}}\n}\n'.replace('PARSER',extract(source,'public class Parser')).replace('INFO',extract(source,'private class CommandInfo')).replace('ADD',add).replace('RESOLVE',extract(source,'private CommandInfo ResolveCommand(')).replace('NAMES','new string[]{'+','.join(json.dumps(c,ensure_ascii=False) for c in commands)+'}')
(work/'Reference.cs').write_text(code,encoding='utf-8')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(work/'Reference.cs'));ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);assert build.returncode==0,build.stdout+build.stderr
cases=['','l','lo le 3','login l 3','login r','login t "hello world"','sh','show','show u','show user','show users','show unknown','show ""','login level ""','""','FCACHE status','login level 4 more']
for c in range(65536):
 if not 0xd800<=c<=0xdfff:cases.append(chr(c)+'login')
for name in commands:
 for length in range(len(name)+1):
  for tail in ['', ' arg',' ""',' extra words']:cases.append(name[:length]+tail)
rng=random.Random(1619);parts=['login','lo','l','level','le','reset','text','sh','show','user','users','u','shutdown','quit','fcache','cl','clear','clearnegatives','status','','""','"hello world"','café','café','\u00ad','soft','x','x-y','X','\0']
for _ in range(10000):cases.append(' '.join(rng.choice(parts) for _ in range(rng.randrange(0,7))))
payload='\n'.join(value.encode('utf-16-be').hex() for value in cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8');outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 run=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=180);assert run.returncode==0,(name,run.stderr)
 (work/(name+'.txt')).write_text(run.stdout,encoding='utf-8');outputs[name]=run.stdout.splitlines();assert len(outputs[name])==len(cases)
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not differences,cases=len(cases),differenceCount=len(differences),differences=differences[:20],applicationParityVerified=False,scope='Actual original Parser, AddCommand and ResolveCommand with fixed command registrations; resolution, ambiguity, exact-key preference and input alias mutations. Shared callback/help/completion/XML layers remain separate.',sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest())
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert report['passed'],report
print(f'PASS: {len(cases)} original console command resolution observations')
