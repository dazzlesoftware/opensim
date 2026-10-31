"""Unchanged original Scene task-script transaction overloads and CreateAsset."""
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
 source=ROOT/'OpenSim/Region/Framework/Scenes/Scene.Inventory.cs';s=source.read_text();generated=w/'ExtractedScriptTaskUpdate.cs';generated.write_text('using System;using System.Collections;using OpenMetaverse;\npartial class Scene{\n'+'\n'.join(extract(s,sig) for sig in ['public ArrayList CapsUpdateTaskInventoryScriptAsset(IClientAPI remoteClient','public ArrayList CapsUpdateTaskInventoryScriptAsset(UUID avatarId','public AssetBase CreateAsset('])+'\n}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',NoWarn='0649').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/script_task_update_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'ScriptTaskUpdateReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 modes=list(range(256))+[256,512|32,1024,2048,4096,4096|16|128];commands=[' '.join(map(str,c)) for c in itertools.product(modes,range(20),range(2))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/ScriptTaskUpdateReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[source,ROOT/'bin/OpenMetaverseTypes.dll']
 report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged original Scene.CapsUpdateTaskInventoryScriptAsset client and avatar overloads plus CreateAsset; controlled scene/provider/permission/inventory/asset/compiler/event interfaces. UUID.Random is actually called by source, nondeterministic value normalized at controlled AssetBase constructor to native injected ID; production native uses real AssetBase. Dependency failure ordering, live running/asset mutations, compiler null/errors, creator/caller identity and null/empty byte data. Full asset service/inventory/engine internals remain separately integrated.',differences=diffs[:3]);(w/'script-task-update-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original task-script transaction observations')
if __name__=='__main__':main()
