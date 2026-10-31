"""Compile the complete original FriendsService and FriendInfo interface model."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');sources=['OpenSim/Services/Friends/FriendsService.cs','OpenSim/Services/Interfaces/IFriendsService.cs','OpenSim/Data/IFriendsData.cs','native-port/tests/friends_reference.cs']
 for f in sources:ET.SubElement(group,'Compile',Include=str(ROOT/f))
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverse');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverse.dll')
 path=w/'Friends.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 commands=[f'{m} {s}' for m in range(6) for s in range(64)];rows=[]
 for command in [[a.dotnet,str(w/'app/Friends.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=45)
  if r.returncode:raise RuntimeError(r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 differences=[{'input':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y];(w/'friends-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':len(commands),'sourceHashes':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources[:3]},'differences':differences},indent=2),encoding='utf-8')
 if differences:raise AssertionError(str(differences[:5]))
 print(f'PASS: {len(commands)} friends source observations')
if __name__=='__main__':main()
