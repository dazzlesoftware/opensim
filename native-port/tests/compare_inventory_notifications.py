"""Original removal EQ + Start/End + actual SDK LLSD XML and AgentAlert packet."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(s,sig):
 start=s.index(sig);i=s.index('{',start)+1;d=1
 while d:
  if s[i]=='{':d+=1
  elif s[i]=='}':d-=1
  i+=1
 return s[start:i]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 client=ROOT/'OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs';events=ROOT/'OpenSim/Region/ClientStack/Linden/Caps/EventQueue/EventQueueGetHandlers.cs';gen=w/'ExtractedInventoryNotifications.cs';gen.write_text('using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenMetaverse.Packets;\npartial class Client{'+''.join(extract(client.read_text(),s) for s in ['public void SendRemoveInventoryItems(','public void SendRemoveInventoryFolders(','public void SendAgentAlertMessage('])+'}\npartial class Eq{'+extract(events.read_text(),'public osUTF8 StartEvent(string eventName)')+extract(events.read_text(),'public byte[] EndEventToBytes(')+'}\n')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 ig=ET.SubElement(project,'ItemGroup')
 for f in [gen,ROOT/'native-port/tests/inventory_notifications_reference.cs']:ET.SubElement(ig,'Compile',Include=str(f))
 for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(ig,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'NotificationReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[' '.join(map(str,c)) for c in itertools.product(range(2),range(128),[0,1,3,255,256])]+[f'2 {m} {c}' for m,c in itertools.product([0,1,4,5],range(18))];rows=[]
 for cmd in [[a.dotnet,str(w/'app/NotificationReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands);diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n]
 files=[client,events,ROOT/'bin/OpenMetaverse.dll',ROOT/'bin/OpenMetaverseTypes.dll',ROOT/'bin/OpenMetaverse.StructuredData.dll'];report=dict(passed=not diffs,cases=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Complete unchanged LLClientView SendRemoveInventoryItems/SendRemoveInventoryFolders/SendAgentAlertMessage with exact original EventQueue StartEvent/EndEventToBytes and actual SDK LLSDxmlEncode2/osUTF8/packet/ZeroEncode. Cache allocation boundary controlled only, no authored expected XML. Null/empty/duplicate/zero/255+ arrays, missing EQ, enqueue false/errors, nullable/modal alert, exact special messages, prefixes using invariant CurrentCulture incl soft hyphen/NUL, Unicode and truncation. Actual enqueue/transport delivery remains required host operation.',differences=diffs[:1]);(w/'inventory-notifications-results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,json.dumps(diffs[:1]);print(f'PASS: {len(commands)} original inventory notifications')
if __name__=='__main__':main()
