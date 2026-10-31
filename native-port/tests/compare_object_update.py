"""Extract the active actual LLClientView method and compile with original zero encoder/SDK."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs';s=source.read_text();start=s.index('        protected void CreatePrimUpdateBlock(SceneObjectPart part, ScenePresence sp, LLUDPZeroEncoder zc)');end=s.index('        [Flags]\n        private enum CompressedFlags',start)
 method=w/'ExtractedObjectMethod.cs';method.write_text('using System;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Region.ClientStack.LindenUDP;\npartial class ObjectUpdateReference {\n'+s[start:end]+'\n}\n')
 encoder=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLUDPZeroEncoder.cs'
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',AllowUnsafeBlocks='true',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [method,encoder,ROOT/'native-port/tests/object_update_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ObjectUpdateReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{seed} {mode}' for seed in [-9,0,1,3,9,17,31,32,40,100,999] for mode in range(512)]
 rows=[]
 for cmd in [[a.dotnet,str(w/'app/ObjectUpdateReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=30);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands),(len(rows[0]),len(rows[1]))
 diff=[dict(command=c,offset=next((i//2 for i in range(min(len(s),len(n))) if s[i]!=n[i]),-1),source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,encoder,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diff,packets=len(commands),bytes=sum(len(row)//2 for row in rows[0]),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Exact wire comparison against extracted original active object method, whole original LLUDPZeroEncoder and actual SDK. Plain-root shape, transforms/motion, default/material/click fields, texture bytes, flags/owner/CreateSelected filtering, signed/zero/near-identity quaternions, signed-zero vectors, dilation clamps. SceneObjectPart and permission inputs controlled; attachments/vegetation/sculpt/mesh, linked parts, text/media/particles/sounds/extra params, physics and actual Unix execution remain separate.',differences=diff[:2]);(w/'object-update-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:1],indent=2)
 print(f'PASS: {report["packets"]} original object packets, {report["bytes"]} exact wire bytes')
if __name__=='__main__':main()
