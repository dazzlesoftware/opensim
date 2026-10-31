"""Actual Scene RezScript/existing-inventory and group transfer control flow."""
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
 scene=ROOT/'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs';group=scene.parent/'SceneObjectGroup.Inventory.cs';generated=w/'ExtractedInventoryScriptRez.cs';generated.write_text('using System;using OpenMetaverse;using UUID=ControlledUuid;\npartial class Scene{\n'+extract(scene.read_text(),'public void RezScript(')+'\n'+extract(scene.read_text(),'public SceneObjectPart RezScriptFromAgentInventory(')+'\n}\npartial class SceneObjectGroup{\n'+extract(group.read_text(),'public bool AddInventoryItem(')+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',NoWarn='0649').items():ET.SubElement(props,k).text=v
 ig=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/inventory_script_rez_reference.cs']:ET.SubElement(ig,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(ig,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'InventoryScriptRezReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 modes=list(range(256))+[256,512,768,1024,2048,4096,4096|8,128|64|256|512,128|64|1024|2048];commands=[' '.join(map(str,c)) for c in itertools.product(range(3),modes,range(18))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/InventoryScriptRezReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[scene,group,ROOT/'OpenSim/Framework/TaskInventoryItem.cs',ROOT/'OpenSim/Framework/InventoryItemBase.cs',ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged extracted Scene.RezScript, RezScriptFromAgentInventory and SceneObjectGroup.AddInventoryItem. Controlled UUID.Random through type-compatible SDK wrapper; actual SDK UUID creator parsing and InventoryItemFlags/PrimFlags constants. Controlled services, task constructor clock, part insertion/engine/event boundaries. Item ID/asset/owner/creator/masks/flags/dates/group defaults, short circuit/exception ordering, library fallback, no-copy retention, group insertion false ignored and new-script branch delegation. PartInventory.AddInventoryItem name/serial/permission-cache mutations and actual engine asset-load stay injected host work.',differences=diffs[:3]);(w/'inventory-script-rez-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original inventory script rez observations')
if __name__=='__main__':main()
