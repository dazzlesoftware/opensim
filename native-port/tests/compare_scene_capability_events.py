"""Original three scene event trigger methods and actual multicast delegate operations."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def method(text,signature):
 start=text.index(signature);body=text.index('{',start);depth=1;end=body+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/Framework/Scenes/EventManager.cs';text=source.read_text(encoding='utf-8-sig');methods=[method(text,s) for s in ['public void TriggerClientClosed(UUID ClientID, Scene scene)','public void TriggerOnRegisterCaps(UUID agentID, Caps caps)','public void TriggerOnDeregisterCaps(UUID agentID, Caps caps)']]
 facade='using System;using OpenMetaverse;using OpenSim.Framework.Capabilities;namespace OpenSim.Region.Framework.Scenes{public class EventManager{static EventLog m_log=new();public delegate void ClientClosed(UUID a,Scene s);public delegate void RegisterCapsEvent(UUID a,Caps c);public delegate void DeregisterCapsEvent(UUID a,Caps c);public event ClientClosed OnClientClosed;public event RegisterCapsEvent OnRegisterCaps;public event DeregisterCapsEvent OnDeregisterCaps;'+''.join(methods)+'}}';f=w/'Events.cs';f.write_text(facade,encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for file in [f,ROOT/'native-port/tests/scene_capability_events_reference.cs']:ET.SubElement(group,'Compile',Include=str(file))
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll');path=w/'Events.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 operations=[''.join(v)+'RDRC' for n in range(4) for v in itertools.product('abxmnR',repeat=n)];operations += ['rmmnR','rambnR','ramanR','rammnnR','rmRamR','rmmRnnR','rabeabDhabcC','reabDhabcCrabR','r0RamR']
 rng=random.Random(733);operations += [''.join(rng.choices('rehabcdxymn0RDC',k=40))+'RDC' for _ in range(150)]
 cases=[str(p)+':'+ops for p,ops in itertools.product([0,1,2,4,6,8,16,24,32,64,65,128,129,255],operations)];rows=[]
 for command in [[a.dotnet,str(w/'app/Events.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(cases)+'\n',capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'case':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'scene-capability-events-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Unchanged original three trigger methods, actual multicast add/remove/invocation snapshots; controlled logger, Scene/Caps arguments. Null/non-null argument forwarding; stack text, full actor semantics, arbitrary delegate types, concurrent schedules and remaining EventManager events pending.','differences':diffs[:4]},indent=2),encoding='utf-8');assert not diffs,diffs[:2];print(f'PASS: {len(cases)} scene capability event observations')
if __name__=='__main__':main()
