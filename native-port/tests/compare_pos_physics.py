"""Original POS simulation/collision/avatar property bodies, controlled PhysicsActor interfaces."""
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
 scene=ROOT/'OpenSim/Region/PhysicsModules/POS/POSScene.cs';character=ROOT/'OpenSim/Region/PhysicsModules/POS/POSCharacter.cs';s=scene.read_text();c=character.read_text()
 methods=['public override PhysicsActor AddAvatar(','public override void RemoveAvatar(','public override PhysicsActor AddPrimShape(','public override void RemovePrim(','private bool isColliding(','private bool isCollidingWithPrim(','public override float Simulate(','public override void SetTerrain(','public override void DeleteTerrain(','public override void SetWaterLevel(']
 properties=['public override Vector3 Position','public override Vector3 Velocity','public override Vector3 Size','public override Vector3 Acceleration','public override Vector3 RotationalVelocity','public override bool Flying','public override bool IsColliding']
 generated=w/'ExtractedPosPhysics.cs';generated.write_text('using System;using System.Collections.Generic;using OpenMetaverse;\npartial class PosPhysicsReference {\n'+'\n'.join(extract(s,m).replace('public override','public') for m in methods)+'\n}\npartial class POSCharacter {\n'+'\n'.join(extract(c,p) for p in properties)+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',AllowUnsafeBlocks='true',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/pos_physics_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'PosPhysicsReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{seed} {mode}' for seed in [-999,-9,0,1,3,17,40,999,1024,999999] for mode in range(2048)]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/PosPhysicsReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands),(len(rows[0]),len(rows[1]))
 diff=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[scene,character,ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diff,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Original POS Simulate/collision/AddAvatar and character property bodies extracted; PhysicsActor/property/event shell controlled. Exact position/observed velocity/target velocity/size/collision/event-count/return bits compared. Preserves old-position horizontal clamps and collision behavior. Ground/flying, target velocity, half-size, default-size bug, gravity/terrain/prim contacts, boundaries, zero/negative timesteps, sequential steps and bad indices. ubODE/Bullet, control-to-target translation, whole scene event wiring and Unix execution separate.',differences=diff[:3]);(w/'pos-physics-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:2],indent=2)
 print(f'PASS: {report["cases"]} exact original POS simulation cases')
if __name__=='__main__':main()
