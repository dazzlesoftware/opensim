"""Original DoAgentPut/default UpdateAgent with full original agent models and controlled service."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from compare_agent_data import build_reference,ROOT
from compare_presence_endpoint import extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);source=ROOT/'OpenSim/Server/Handlers/Simulation/AgentHandlers.cs';text=source.read_text(encoding='utf-8-sig');text=text[text.index('public class AgentSimpleHandler'):];methods=extract(text,r'protected void DoAgentPut\(').replace('protected void','public void')+extract(text,r'protected virtual bool UpdateAgent\(');wrapper=w/'Methods.cs';wrapper.write_text('using System;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class PutReference{'+methods+'}',encoding='utf-8');assembly=build_reference(a.dotnet,w,[wrapper,ROOT/'native-port/tests/agent_put_reference.cs'],'PutReference')
 seven='00000000-0000-0000-0000-000000000007';base={'agent_uuid':seven,'position':'<1, 2, 30>','destination_uuid':seven[:-1]+'8','destination_x':'25600','destination_y':'51200','destination_name':'Destination','context':{'InboundVersion':.3,'OutboundVersion':.4,'WearablesCount':2}};requests=[{},base]
 for message in [None,'','AgentData','AgentPosition','agentdata','unknown',True,{},[]]:requests.append({**base,'message_type':message})
 for key,values in {'destination_x':[None,'bad','-256','2147483648',1.5,True,{}],'destination_y':[None,'1\0','1\0 ',[]],'destination_uuid':[None,'bad',seven,seven+'tail'],'destination_name':[None,'',True,1.5,{},[],{'name':'value'}],'context':[None,{},[],{'InboundVersion':.1,'OutboundVersion':.8,'WearablesCount':'bad'}],'animations':[[3],[{}]],'god_data':[None,{},True],'children_seeds':[[{'handle':'3'},{'handle':'3'}]],'throttles':[{},[1,2,3]],'attach_objects':[[{}],[]],'controllers':[[3],[{}]]}.items():
  for value in values:
   for message in ['AgentData','AgentPosition']:requests.append({**base,'message_type':message,key:value})
 profiles=[0,1,2,4,8,16,32,33,34,36,40,48,96];commands=[str(profile)+' '+json.dumps(value,separators=(',',':'),ensure_ascii=False).encode().hex() for profile in profiles for value in requests];rows=[]
 for command in [[a.dotnet,str(assembly)],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180)
  if r.returncode:raise RuntimeError(r.stdout[-1000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y];(w/'agent-put-parity-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(commands),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Complete original DoAgentPut/default UpdateAgent, original AgentData/Position/appearance dependencies; controlled scene/service/virtual hook and destination field projection; full HTTP host/scene runtime pending','differences':diffs[:3]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:1],indent=2)[:6000]+' count='+str(len(diffs)))
 print(f'PASS: {len(commands)} inbound agent PUT source observations')
if __name__=='__main__':main()
