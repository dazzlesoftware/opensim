"""Whole original TaskInventoryItem class with controlled epoch and UUID RNG."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Framework/TaskInventoryItem.cs';generated=w/'OriginalTaskInventoryItem.cs';generated.write_text('using UUID=ControlledUuid;\n'+source.read_text())
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 ig=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/task_inventory_item_reference.cs']:ET.SubElement(ig,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(ig,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'TaskInventoryItemReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 modes=[0,1,2,4,8,16,32,64,64|128,64|256,64|256|128,64|256|512,511];commands=[' '.join(map(str,c)) for c in itertools.product(range(23),modes,[-2147483648,-1,0,1,2147483647])]+[f'{c} {m} 42' for c,m in itertools.product([0,2,5,7],range(512))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/TaskInventoryItemReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Whole unchanged original TaskInventoryItem class with UUID alias only for controlled random generation and actual SDK TryParse/string formatting; controlled Util.UnixTimeSinceEpoch. Every public field/default/property, null/empty creator/name/description, compound/plain/invalid creator identification, stale creator-data semantics, all permissions, independent clone assignment and constructor-count, ResetIDs ordering including RNG failure and repeated resets. Native strings model immutable CLR string value semantics.',differences=diffs[:2]);(w/'task-inventory-item-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original task inventory item observations')
if __name__=='__main__':main()
