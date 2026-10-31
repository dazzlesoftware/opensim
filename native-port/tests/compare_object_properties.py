"""Exact original active ObjectProperties raw projection and SDK osUTF8 limits."""
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
 source=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs';encoder=source.parent/'LLUDPZeroEncoder.cs';generated=w/'ExtractedObjectProperties.cs';generated.write_text('using System;using OpenMetaverse;using OpenSim.Region.ClientStack.LindenUDP;\npartial class ObjectPropertiesReference{\n'+extract(source.read_text(),'private static void CreateObjectPropertiesBlock(')+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',AllowUnsafeBlocks='true',NoWarn='0649').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,encoder,ROOT/'native-port/tests/object_properties_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ObjectPropertiesReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{s} {m}' for s,m in itertools.product([-2147483648,-1,0,1,32767,32768,65535,65536,2147483647],range(256))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/ObjectPropertiesReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,encoder,ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs',ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),bytes=sum(len(s)//2 for s in rows[0]),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged original active CreateObjectPropertiesBlock, entire original LLUDPZeroEncoder and actual SDK osUTF8 string preparation. Exact reliable zeroencoded singleblock packets; root-vs-part permission/sale selection, UUID fields, group owner encoding, signed dates/serials, field defaults, ASCII/Unicode/emoji/NUL/truncation. Pure projection; scene properties queue/priority/batching and mutation semantics remain host. Invalid native UTF8 rejected; C# unpaired UTF16 profile not implied.',differences=diffs[:3]);(w/'object-properties-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original object properties packets, {report["bytes"]} wire bytes')
if __name__=='__main__':main()
