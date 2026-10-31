"""Original task-to-agent move flow with original permission helpers and SDK text."""
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
 scene=ROOT/'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs';model=ROOT/'OpenSim/Framework/TaskInventoryItem.cs';permissions=ROOT/'OpenSim/Framework/PermissionsUtil.cs';util=ROOT/'OpenSim/Framework/Util.cs';node=ROOT/'OpenSim/Framework/InventoryNodeBase.cs';agent=ROOT/'OpenSim/Framework/InventoryItemBase.cs';generated=w/'ExtractedTaskMove.cs'
 generated.write_text('using System;using OpenMetaverse;using OpenSim.Framework;using UUID=ControlledUuid;using PermissionMask=OpenSim.Framework.PermissionMask;\npartial class Scene{\n'+'\n'.join(extract(scene.read_text(),s) for s in ['private InventoryItemBase CreateAgentInventoryItemFromTask(','private void RemoveNonCopyTaskItemFromPrim(','public InventoryItemBase MoveTaskInventoryItem(IClientAPI remoteClient','public void ClientMoveTaskInventoryItem(','public InventoryItemBase MoveTaskInventoryItem(UUID avatarId'])+'\n}\nnamespace OpenSim.Framework{'+extract(util.read_text(),'public enum PermissionMask')+'public static class PermissionsUtil{'+extract(permissions.read_text(),'public static void ApplyFoldedPermissions(')+extract(permissions.read_text(),'public static uint FixAndFoldPermissions(')+'}}\npartial class InventoryNodeBase{'+extract(node.read_text(),'public virtual string Name')+'}\npartial class InventoryItemBase{'+extract(agent.read_text(),'public string Description')+'}\n')
 original=w/'OriginalTaskItem.cs';original.write_text('using UUID=ControlledUuid;using PermissionMask=OpenSim.Framework.PermissionMask;\n'+model.read_text())
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',NoWarn='0649').items():ET.SubElement(props,k).text=v
 ig=ET.SubElement(project,'ItemGroup')
 for f in [generated,original,ROOT/'native-port/tests/task_inventory_move_reference.cs']:ET.SubElement(ig,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(ig,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'TaskInventoryMoveReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 modes=list(range(512))+[512,1024,2048,4096,4096|1,4096|2|8|128,8192,16384,32768,65536,131072,524288,1048576,4096|1048576]
 commands=[f'{k} {m} 240 {f}' for k,m,f in itertools.product(range(5),modes,range(18))]+[f'0 {2|8|128} {p} 0' for p in range(256)];rows=[]
 for cmd in [[a.dotnet,str(w/'app/TaskInventoryMoveReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[scene,model,permissions,util,node,agent,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged original CreateAgentInventoryItemFromTask/RemoveNonCopyTaskItemFromPrim/MoveTaskInventoryItem(client and avatar)/ClientMoveTaskInventoryItem, original folded-permission helpers and original Name/Description setters/getters with actual SDK osUTF8/flags. Original full task model, controlled UUID RNG/agent creation clock and real persistence/notification boundary. Transfer checks, all folded values, ownership distinctions, no-copy/bypass/relookup, null/Unicode/NUL/whitespace metadata, offline/found presence, zero viewer item, full dependency exception ordering. Actual AddInventoryItem folder fallback/store/notification remains required injected host adapter; false return deliberately ignored by source offline method.',differences=diffs[:2]);(w/'task-inventory-move-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original task inventory move observations')
if __name__=='__main__':main()

