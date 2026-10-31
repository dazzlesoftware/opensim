"""Original FetchLibSimpleRequest, item serializer and actual SDK XML writer."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
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
 wrapper_source=ROOT/'OpenSim/Framework/Servers/HttpServer/SimpleOSDMapHandler.cs';wrapper=extract(wrapper_source.read_text(),'public virtual void Handle(')
 source=ROOT/'OpenSim/Capabilities/Handlers/FetchInventory/FetchLib2Handler.cs';item=ROOT/'OpenSim/Framework/InventoryItemBase.cs';generated=w/'ExtractedLibraryCaps.cs';generated.write_text('using System;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;\npartial class LibraryReference{\n'+extract(source.read_text(),'public void FetchLibSimpleRequest(')+'\n}\npartial class InventoryItemBase{\n'+extract(item.read_text(),'public void ToLLSDxml(')+'\n}\npartial class Wrapper{\n'+wrapper+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/library_inventory_caps_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'LibraryInventoryCapsReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{seed} {count} {mode}' for seed in range(16) for count in [0,1,2,3,5,16] for mode in [0,1,2,3,4,5,8,9,16,17,18,20,24,32,33,64,66,68,72,128,130,132,136,256,257,512,513]];rows=[]
 for cmd in [[a.dotnet,str(w/'app/LibraryInventoryCapsReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 http_bodies=['','bad','{}','[]','{"items":[]}','{"items":[{}]}','{"items":[{"item_id":"00000000-0000-0000-0000-000000000001"}]}','<llsd><map><key>items</key><array><map><key>item_id</key><uuid>00000000-0000-0000-0000-000000000001</uuid></map></array></map></llsd>']
 http_commands=[f'{seed} {mode} {method} {b.encode().hex() or "-"}' for seed in [0,1,2,3] for mode in [0,1,2,4,8,16,17,32] for method in ['POST','GET','post'] for b in http_bodies];http_rows=[]
 for cmd in [[a.dotnet,str(w/'app/LibraryInventoryCapsReference.dll'),'--http'],[a.native,'--http']]:
  r=subprocess.run(cmd,input='\n'.join(http_commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;http_rows.append(r.stdout.splitlines())
 assert len(http_rows[0])==len(http_rows[1])==len(http_commands)
 http_diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(http_commands,*http_rows) if s!=n];assert not http_diffs,http_diffs[:2]
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n];report=dict(passed=not diffs,cases=len(commands),httpCases=len(http_commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,item,wrapper_source,ROOT/'bin/OpenMetaverse.StructuredData.dll']},scope='Original FetchLibSimpleRequest and InventoryItemBase.ToLLSDxml unchanged, actual SDK OSD/writer. Controlled library provider/cache, full flags and capturedagent; nullcache/library/zeroagent/malformed/nullitems/providerexception profiles. Libraryconstructor nullroot tested natively; Original SimpleOSDMapHandler HTTP wrapper compared for XML/JSON status/count/provider-failure branches; registration and real librarygraph remain separate.',differences=diffs[:2]);(w/'library-inventory-caps-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original library inventory capability results + {len(http_commands)} HTTP wrapper cases')
if __name__=='__main__':main()
