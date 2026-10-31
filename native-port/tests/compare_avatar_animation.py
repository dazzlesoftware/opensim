"""Original SendAnimations, actual SDK writes/codec and whole DefaultAvatarAnimations/XML."""
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
def run(cmd,input='',cwd=None):
 r=subprocess.run(cmd,input=input,capture_output=True,text=True,cwd=cwd,timeout=60);assert r.returncode==0,r.stdout[-500:]+r.stderr;return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True,type=Path);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();native=str(a.native.resolve());w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs';s=source.read_text();method=extract(s,'public void SendAnimations(UUID[] animations, int[] seqs, UUID sourceAgentId, UUID[] objectIDs)')
 # Bounds instrumentation prevents the SDK's unchecked Unsafe UUID/int writes
 # from corrupting the oracle heap on source4096-capacity failure cases.
 method=method.replace('sourceAgentId.ToBytes(data, 7)','SafeUuid(sourceAgentId,data,7)').replace('animations[i].ToBytes(data, pos)','SafeUuid(animations[i],data,pos)').replace('objectIDs[i].ToBytes(data, pos)','SafeUuid(objectIDs[i],data,pos)').replace('Utils.IntToBytesSafepos(seqs[i], data, pos)','SafeInt(seqs[i],data,pos)')
 start=s.index('        static private readonly byte[] AvatarAnimationHeader');end=s.index('        public void SendAnimations(',start);header=s[start:end]
 generated=w/'ExtractedSendAnimations.cs';generated.write_text('using System;using System.Collections.Generic;using OpenMetaverse;\npartial class AvatarAnimationReference {\n'+header+method+'\n}\n')
 catalog=ROOT/'OpenSim/Region/Framework/Scenes/Animation/DefaultAvatarAnimations.cs';xml=ROOT/'bin/data/avataranimations.xml'
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,catalog,ROOT/'native-port/tests/avatar_animation_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'AvatarAnimationReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 oracle=[a.dotnet,str(w/'app/AvatarAnimationReference.dll')]
 commands=[f'{count} {mode}' for count in [0,1,2,3,4,5,112,113,114,115,145,169,170,171,202,203,204,254,255] for mode in range(40)]
 payload='\n'.join(commands)+'\n';expected=run(oracle,payload);actual=run([native,'--packets'],payload);assert len(expected)==len(actual)==len(commands)
 diff=[dict(command=c,source=s[:100],native=n[:100]) for c,s,n in zip(commands,expected,actual) if s!=n]
 sdk255=run(oracle+['--codec255']);native255=run([native,'--codec255']);assert sdk255==native255,'generic SDK255 codec differs'
 queries=[];entries=ET.parse(xml).getroot()
 for item in entries:
  if 'name' not in item.attrib:continue
  name=item.attrib['name'];value=item.text.strip()
  for key in [name,name.lower(),name.swapcase(),value,value.upper(),'{'+value+'}',value.replace('-','')]:queries.append('R '+key.encode().hex())
  queries.append('N '+value)
 for key in ['','UNKNOWN',' stand ','walk!','00000000-0000-0000-0000-000000000000','11111111-1111-4111-8111-111111111111']:queries.append('R '+(key.encode().hex() or '-'))
 queries+=['N 00000000-0000-0000-0000-000000000000','N 11111111-1111-4111-8111-111111111111'];payload='\n'.join(queries)+'\n'
 source_catalog=run(oracle+['--catalog'],payload,ROOT/'bin');native_catalog=run([native,'--catalog',str(xml)],payload);assert len(source_catalog)==len(native_catalog)
 diff += [dict(catalog_line=i,source=s,native=n) for i,(s,n) in enumerate(zip(source_catalog,native_catalog)) if s!=n]
 files=[source,catalog,xml,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diff,packets=len(commands),sourceBufferRejections=expected.count('BUFFER'),genericSdk255Bytes=len(sdk255[0])//2,catalogObservations=len(source_catalog),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Original SendAnimations with only SDK-unsafe UUID/int writes bounds-instrumented before delegation to actual SDK; actual4096-byte SDK buffer; exact source success/normalized capacity failures and successful SDK packet round trips. Separate generic SDK codec255. Whole original DefaultAvatarAnimations and bundled XML, invariant/ASCII valid lookup profiles. Invalid XML/static-cctor/culture edges, transport splitting/delivery and actual Unix execution remain separate.',differences=diff[:3]);(w/'avatar-animation-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diff,json.dumps(diff[:2],indent=2)
 print(f'PASS: {len(commands)} source animation packets/capacity outcomes, SDK255 codec, {len(source_catalog)} original catalog observations')
if __name__=='__main__':main()
