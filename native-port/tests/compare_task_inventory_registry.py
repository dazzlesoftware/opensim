"""Original SceneObjectPartInventory Add/Update/Remove with actual Dictionary ordering."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
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
 source=ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPartInventory.cs';model=ROOT/'OpenSim/Framework/TaskInventoryItem.cs';s=source.read_text();generated=w/'ExtractedTaskRegistry.cs';generated.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;\npartial class Registry{\n'+'\n'.join(extract(s,sig) for sig in ['private bool InventoryContainsName(','private string FindAvailableInventoryName(','public void AddInventoryItem(TaskInventoryItem item, bool allowedDrop)','protected void AddInventoryItem(string name, TaskInventoryItem item, bool allowedDrop)','public bool UpdateInventoryItem(TaskInventoryItem item, bool fireScriptEvents, bool considerChanged)','public int RemoveInventoryItem(','public TaskInventoryItem GetInventoryItem(UUID itemId)'])+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 ig=ET.SubElement(project,'ItemGroup')
 for f in [generated,model,ROOT/'native-port/tests/task_inventory_registry_reference.cs']:ET.SubElement(ig,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(ig,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'TaskRegistryReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=list(map(str,range(256)))+["256","384"];rows=[]
 for cmd in [[a.dotnet,str(w/'app/TaskRegistryReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,model,ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,traces=len(commands),operations=256*15+2*261,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged original AddInventoryItem/unique name lookup/UpdateInventoryItem/RemoveInventoryItem/GetInventoryItem methods with original complete TaskInventoryItem and actual CLR Dictionary. Controlled part/group/script event/permission hooks and lock boundary; sequential live-reference snapshots, duplicate IDs, null/empty names, script-subset stale entries, zero asset and owner update, group permissions, serial wrap, removal hooks and independent Dictionary free-slot reuse; all256 name slots exhausted, case-sensitive collision, null-name collision, and mutated live ItemID preserving original dictionary key. Concurrent lock scheduling and persistence engine remain host.',differences=diffs[:1]);(w/'task-inventory-registry-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original registry traces / {report["operations"]} operations')
if __name__=='__main__':main()

