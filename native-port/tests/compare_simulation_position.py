"""Original position queue/worker plus actual SDK failure cache with controlled transport."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT,extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs';text=source.read_text(encoding='utf-8-sig');methods=extract(text,r'public bool UpdateAgent\(GridRegion destination, AgentPosition data\)')+extract(text,r'private bool UpdateAgent\(GridRegion destination, IAgentData cAgentData, EntityTransferContext ctx, int timeout\)');wrapper=w/'Methods.cs';wrapper.write_text('using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class PositionConnectorReference{'+methods+'}',encoding='utf-8')
 model=ROOT/'OpenSim/Framework/ChildAgentDataUpdate.cs';text=model.read_text(encoding='utf-8-sig');classes=''.join(extract(text,p) for p in [r'public class ChildAgentDataUpdate',r'public interface IAgentData',r'public class AgentPosition']);models=w/'Models.cs';models.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;namespace OpenSim.Framework{'+classes+'}',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [wrapper,models,ROOT/'native-port/tests/simulation_position_reference.cs',ROOT/'OpenSim/Framework/EntityTransferContext.cs',ROOT/'OpenSim/Framework/Culture.cs',ROOT/'OpenSim/Framework/VersionInfo.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 path=w/'PositionConnector.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 replies=[{'Success':True},{'Success':False},{'success':True},{},{'Success':'false'},{'Success':'1'},{'Success':[]},{'Success':[1]},None,True,'throw']
 cases=[]
 for mode,reply,custom in itertools.product(['single','repeat','coalesce','same','failure_queued','other_agent'],replies,[False,True]):cases.append({'mode':mode,'replies':[reply,{'Success':True}],'custom':custom})
 for mode in ['cooldown','clock_back','purge_order']:
  cases.append({'mode':mode,'replies':[{'Success':False}]+([{'Success':False}] if mode=='purge_order' else [])+[{'Success':True}]})
 for mode,throw in itertools.product(['single','repeat','coalesce'],[1,2,3]):cases.append({'mode':mode,'replies':[{'Success':True}],'throw_path':throw})
 for flag in ['bad_pack','null_destination','null_data']:cases.append({'mode':'repeat','replies':[{'Success':True}],flag:True})
 for version,reply,flag in itertools.product([0,.1,.199,.2,.299,.3,.8],replies,['','null_context','null_data','null_destination','bad_pack']):cases.append({'mode':'worker','version':version,'timeout':200000 if version==.1 else 10000,'replies':[reply,{'Success':True}],flag:True} if flag else {'mode':'worker','version':version,'timeout':10000,'replies':[reply,{'Success':True}]})
 for reply in [{'Success':True},{'Success':False}]:
  for repeat in range(8):cases.append({'mode':'parallel','replies':[reply,{'Success':True}]})
 commands=[json.dumps(value,separators=(',',':'),ensure_ascii=False).encode().hex() for value in cases];rows=[]
 for command in [[a.dotnet,str(w/'app/PositionConnector.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
  if r.returncode:raise RuntimeError(r.stdout[-2000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'input':value,'source':x,'native':y} for value,x,y in zip(cases,*rows) if x!=y];(w/'simulation-position-parity-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(commands),'sourceHashes':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,model,ROOT/'bin/OpenMetaverseTypes.dll']},'scope':'Complete original public position UpdateAgent and private worker with actual AgentPosition/context/SDK ExpiringCache; timer disabled and actual SDK expiration fields shifted for clock scenarios, transport and ServerURI projected; full cache/host/runtime/scene semantics pending','differences':diffs[:4]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:2],indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(commands)} simulation position queue/worker source observations')
if __name__=='__main__':main()
