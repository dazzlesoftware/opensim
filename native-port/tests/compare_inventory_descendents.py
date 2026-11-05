"""Unmodified original folder handler methods and SDK serializer/LLSD XML parser."""
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
def uuid(n):return f'00000000-0000-0000-0000-{n:012d}'
def body(entries):
 return '<llsd><map><key>folders</key><array>'+''.join('<map><key>folder_id</key><uuid>'+uuid(i)+'</uuid><key>owner_id</key><uuid>'+uuid(o)+'</uuid><key>sort_order</key><integer>7</integer><key>fetch_folders</key><boolean>'+str(f)+'</boolean><key>fetch_items</key><boolean>'+str(t)+'</boolean></map>' for i,o,f,t in entries)+'</array></map></llsd>'
def main():
 p=argparse.ArgumentParser();p.add_argument('--logs',action='store_true');p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Capabilities/Handlers/FetchInventory/FetchInvDescHandler.cs';item=ROOT/'OpenSim/Framework/InventoryItemBase.cs';s=source.read_text();methods='\n'.join(extract(s,signature) for signature in ['public void FetchInventoryDescendentsRequest(','private void AddLibraryFolders(','private List<InventoryCollection> Fetch(','private bool BadFolder(','private void ProcessLinks(']);serializer=extract(item.read_text(),'public void ToLLSDxml(')
 generated=w/'ExtractedDescendents.cs';generated.write_text('using System;using System.Linq;using System.Collections.Generic;using System.Text;using OpenMetaverse;using OpenMetaverse.StructuredData;\npartial class DescendentsReference{\n'+methods+'\n}\npartial class InventoryItemBase{\n'+serializer+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/inventory_descendents_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'InventoryDescendentsReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 bodies=['','bad','{}','<llsd><map /></llsd>','<llsd><array /></llsd>','<llsd><map><key>folders</key><string>bad</string></map></llsd>','<llsd><map><key>folders</key><array><integer>1</integer></array></map></llsd>','<llsd><map><key>folders</key><array><map /></array></map></llsd>']
 for key,owner,f,t in itertools.product([0,1,2,999],[100,200],[0,1],[0,1]):bodies.append(body([(key,owner,f,t)]))
 for entries in [[(1,100,1,1),(1,100,0,0)],[(0,100,1,1),(0,101,1,1)],[(2,100,1,1),(1,100,1,1)],[(3,100,1,1),(1,101,1,1)],[(1,100,1,1),(2,200,0,0)],[(1,200,0,0),(1,100,1,1)],[(2,100,1,1),(3,100,1,1),(1,100,1,1)]]:bodies.append(body(entries))
 for total in [6,9,10,11,14]:bodies.append(body([(i,200 if False else 100,1,1) for i in range(1,total+1)]))
 commands=[f'{mode} {options} {b.encode().hex() or "-"}' for mode in range(9) for options in list(range(16))+[16,17,18,32,33,34] for b in bodies];rows=[]
 for cmd in [[a.dotnet,str(w/'app/InventoryDescendentsReference.dll')]+(['--logs'] if a.logs else []),[a.native,'--probe-logs' if a.logs else '--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines());(w/('reference.txt' if len(rows)==1 else 'native.txt')).write_text(r.stdout,encoding='utf-8')
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n];report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,item,ROOT/'bin/OpenMetaverse.StructuredData.dll']},logMessagesCompared=a.logs,scope='Complete five original handler methods, original item serializer, actual SDK XMLparser/writer; controlled ordinary/library providers and cache membership. XML malformed/zero/duplicate/library/link/null/extra/fewer/error profiles. Worker HTTP poll registration, null entries inside provider item/folder lists and whole library/provider graph remain separate.',differences=diffs[:2]);(w/'inventory-descendents-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original inventory descendents response/provider/cache traces')
if __name__=='__main__':main()
