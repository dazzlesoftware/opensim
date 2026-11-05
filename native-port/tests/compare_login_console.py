# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Actual original login command handler versus live native service settings."""
from pathlib import Path
import argparse,subprocess,json,hashlib,random,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
path=root/'OpenSim/Services/LLLoginService/LLLoginService.cs';text=path.read_text(encoding='utf-8-sig');start=text.index('private void HandleLoginCommand(');brace=text.index('{',start);end=brace+1;depth=1
while depth:
 if text[end]=='{':depth+=1
 if text[end]=='}':depth-=1
 end+=1
code='using System;using System.Linq;\nclass Config {public int GetInt(string key,int fallback)=>9;}\nclass ConsoleInstance {public string Last;public void Output(string format,params object[] values){Last=string.Format(format,values);}}\nclass MainConsole {public static ConsoleInstance Instance=new();}\nclass Reference {\nint m_MinLoginLevel=9;string m_WelcomeMessage="Initial";Config m_LoginServerConfig=new();\nMETHOD\nstatic string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());\nstatic string Encode(string value)=>string.Concat(value.Select(c=>((int)c).ToString("x4")));\nstatic void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;var reference=new Reference();string row;while((row=Console.ReadLine())!=null){MainConsole.Instance.Last=null;reference.HandleLoginCommand("Users",row.Split(\'\\t\').Select(Decode).ToArray());Console.WriteLine(reference.m_MinLoginLevel+"|"+Encode(reference.m_WelcomeMessage)+"|"+(MainConsole.Instance.Last==null?"-":Encode(MainConsole.Instance.Last)));}}\n}\n'.replace('METHOD',text[start:end])
(work/'Reference.cs').write_text(code,encoding='utf-8')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(work/'Reference.cs'));ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);assert build.returncode==0,build.stdout+build.stderr
cases=[['login','level'],['login','text'],['login','reset'],['login','other']]
values=['0','-1','+201',' 12 ','2147483647','-2147483648','2147483648','-2147483649','','bad','12\0','12\0x','١','１','1.0','--1','+ 1']
values += [chr(c)+'42'+chr(c) for c in [0,9,10,11,12,13,32,133,160,5760,8192,8193,8194,8195,8232,8233,8239,8287,12288]]
for value in values:cases.extend([['login','level',value],['login','reset'],['login','text',value,'ignored']])
rng=random.Random(3225)
for _ in range(5000):
 value=''.join(rng.choice('0123456789+- \t\n\0bad') for _ in range(rng.randrange(0,30)))
 cases.append(['login','level',value])
for value in ['Hello <USERNAME>','a\\nb','a\\\\nb','é中😀','',"a'b",'one two three','\n','\\n']:
 cases.extend([['login','text',value],['login','level','-23'],['login','reset']])
payload='\n'.join('\t'.join(word.encode('utf-16-be').hex() for word in case) for case in cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8')
outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 run=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=120);assert run.returncode==0,(name,run.stderr)
 (work/(name+'.txt')).write_text(run.stdout,encoding='utf-8');outputs[name]=run.stdout.splitlines();assert len(outputs[name])==len(cases)
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not differences,cases=len(cases),differenceCount=len(differences),differences=differences[:20],applicationParityVerified=False,scope='Unchanged HandleLoginCommand with invariant Int32.TryParse and fixed configured reset level, versus native service minimum, live completion welcome and console output. Console resolver/full simulator parity not proved.',sourceSHA256=hashlib.sha256(path.read_bytes()).hexdigest())
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert report['passed'],report
print(f'PASS: {len(cases)} original login console observations')
