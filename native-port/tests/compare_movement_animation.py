"""Original full DetermineMovementAnimation/update/force methods, controlled scene/physics effects."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(source,signature):
 start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  if source[end]=='{':depth+=1
  if source[end]=='}':depth-=1
  end+=1
 return source[start:end]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/Framework/Scenes/Animation/ScenePresenceAnimator.cs';s=source.read_text()
 methods=['public ScenePresenceAnimator(ScenePresence sp)','public enum motionControlStates : byte','private string DetermineMovementAnimation()','public bool UpdateMovementAnimations()','public bool ForceUpdateMovementAnimations()']
 generated=w/'ExtractedMovementAnimation.cs';generated.write_text('using System;using OpenMetaverse;\npartial class ScenePresenceAnimator {\n'+'\n'.join(extract(s,m) for m in methods).replace('Environment.TickCount','Clock')+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/movement_animation_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'MovementAnimationReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{mode} {clock}' for mode in list(range(1024))+[base|extra for base in range(128) for extra in [1024,2048,4096]] for clock in [0,1,-1,2147483147,-2147483647,-4000,12345]]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/MovementAnimationReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)*32,(len(rows[0]),len(rows[1]))
 diff=[dict(command=commands[i//32],frame=i%32,source=s,native=n) for i,(s,n) in enumerate(zip(*rows)) if s!=n]
 files=[source,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diff,transitions=len(rows[0]),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Original complete constructor, enum, DetermineMovementAnimation, UpdateMovementAnimations and ForceUpdateMovementAnimations extracted; only Environment.TickCount injected. Actual SDK control flags/UUID with controlled scene/actor/animation-set callbacks. Exact animation/state/timers/jump/falling/lastfall/actor-flying/name-change/apply-result and ordered side effects. Physics itself, animation UUID sets/overrides/viewer delivery and actual Unix execution remain separate.',differences=diff[:3]);(w/'movement-animation-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:2],indent=2)
 print(f'PASS: {report["transitions"]} original movement animation transitions/effect traces')
if __name__=='__main__':main()
