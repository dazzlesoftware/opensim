"""Extract complete original Scene.GetNewRezLocation and compare positions/callbacks."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/Framework/Scenes/Scene.cs';s=source.read_text();start=s.index('        public Vector3 GetNewRezLocation(');end=s.index('\n        /// <summary>',start)
 method=w/'ExtractedRezMethod.cs';method.write_text('using System;using System.Collections.Generic;using OpenMetaverse;\npartial class RezLocationReference {\n'+s[start:end]+'\n}\n')
 physics=ROOT/'OpenSim/Region/PhysicsModules/SharedBase/PhysicsScene.cs';s=physics.read_text();start=s.index('    public enum RayFilterFlags : ushort');end=s.index('    public delegate void RequestAssetDelegate',start);flags=w/'ExtractedRayFilterFlags.cs';flags.write_text(s[start:end])
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',AllowUnsafeBlocks='true',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [method,flags,ROOT/'native-port/tests/rez_location_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'RezLocationReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{seed} {mode}' for seed in [-999,-9,0,1,3,17,40,999] for mode in range(4096)]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/RezLocationReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=30);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands),(len(rows[0]),len(rows[1]))
 diff=[dict(command=c,offset=next((i//2 for i in range(min(len(s),len(n))) if s[i]!=n[i]),-1),source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,physics,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diff,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Complete extracted original Scene.GetNewRezLocation control flow, actual SDK Vector3 arithmetic, source filter enum, controlled physics/part/OBB/closest inputs. Exact float-bit positions and backend invocation traces. Water, ray intersections, zero length, bypass, target hit/miss/missing and contact selection. Actual physics engines, OBB intersection implementation and Unix execution remain separate.',differences=diff[:2]);(w/'rez-location-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:1],indent=2)
 print(f'PASS: {report["cases"]} exact original rez locations and backend traces')
if __name__=='__main__':main()
