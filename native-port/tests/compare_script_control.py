"""Unchanged Scene/YEngine script-control methods and Running setter."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(s,sig):
 start=s.index(sig);i=s.index('{',start)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 inventory=ROOT/'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs';handlers=inventory.parent/'Scene.PacketHandlers.cs';engine=ROOT/'OpenSim/Region/ScriptEngine/YEngine/XMREngine.cs';misc=engine.parent/'XMRInstMisc.cs';main=engine.parent/'XMRInstMain.cs'
 run=engine.parent/'XMRInstRun.cs';post=run.read_text();start=post.index('public bool PostEvent(');start=post.index('{',start)+1;end=post.index('if(m_minEventDelay != 0)',start);gate='public bool? PostGate(EventParams evt){'+post[start:end]+'} return null; }';generated=w/'ExtractedScriptControl.cs';generated.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using XMRInstance=ControlInstance;\npartial class Scene{\n'+extract(inventory.read_text(),'public void SetScriptRunning(')+'\n'+extract(handlers.read_text(),'public void ProcessScriptReset(')+'\n}\npartial class Engine{\n'+'\n'.join(extract(engine.read_text(),s) for s in ['public void OnStartScript(','public void OnStopScript(','public void OnScriptReset(','public void ResetScript('])+'\n}\n'+extract(main.read_text(),'public enum XMRInstState')+'\npartial class ExecutionInstance{\n'+extract(misc.read_text(),'public bool Running')+'\n'+gate+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',NoWarn='0649;0169;0219').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/script_control_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ScriptControlReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=['0 '+' '.join(map(str,c)) for c in itertools.product(range(7),range(64),range(14))]+['1 '+' '.join(map(str,c)) for c in itertools.product(range(11),range(2),range(2),[-1,0,1],range(2),range(5))]+['2 '+' '.join(map(str,c)) for c in itertools.product(range(11),range(2),range(2),range(2),range(2),range(2))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/ScriptControlReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[inventory,handlers,engine,misc,main,run,ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged Scene.SetScriptRunning/ProcessScriptReset and YEngine.OnStartScript/OnStopScript/OnScriptReset/ResetScript with controlled scene/permissions/instance/URL/event dependencies, plus complete unchanged XMRInstance.Running property with controlled queues. All enum states, pending/current events, suspend counts, queue exceptions and metadata ordering. Exact original early PostEvent fragment compared with unknown/missing handlers, all execution states, stopped/construct and one pending-state_entry semantics. Actual Reset and remaining PostEvent execution remain adapter responsibilities.',differences=diffs[:3]);(w/'script-control-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original script-control observations')
if __name__=='__main__':main()

