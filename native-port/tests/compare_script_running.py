"""Original YEngine OnGetScriptRunning and LLClientView EQ/UDP reply control flow."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(s,signature):
 start=s.index(signature);brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[start:end]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 engine=ROOT/'OpenSim/Region/ScriptEngine/YEngine/XMREngine.cs';client=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs';generated=w/'ExtractedScriptRunning.cs';generated.write_text('using System;using OpenMetaverse;using OpenMetaverse.Packets;using IClientAPI=Client;\npartial class ScriptRunningReference{\n'+extract(engine.read_text(),'public void OnGetScriptRunning(')+'\n}\npartial class Client{\n'+extract(client.read_text(),'public void SendScriptRunningReply(')+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/script_running_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ScriptRunningReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[' '.join(map(str,args)) for args in itertools.product(range(256),[0,20,77],[0,10,99],[0,42])];rows=[]
 for cmd in [[a.dotnet,str(w/'app/ScriptRunningReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n];report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [engine,client,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll']},scope='Complete original YEngine.OnGetScriptRunning and LLClientView.SendScriptRunningReply, actual SDK UDP packet bytes; controlled instance/property/metadata/trace/EQ dependencies. Lookup/trace/getter/setter/reply exceptions and missing instances, true/false, zero/mismatched request IDs and agent projection. Existing native EQ serializer reused by host; engine running-state lifecycle, packet admission and scheduling remain separate.',differences=diffs[:2]);(w/'script-running-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original script-running control/reply observations')
if __name__=='__main__':main()
