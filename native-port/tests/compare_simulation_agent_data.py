"""Original full AgentData update overload and generic worker with full original models."""
import argparse,hashlib,itertools,json,subprocess
from pathlib import Path
from compare_agent_data import ROOT,build_reference
from compare_presence_endpoint import extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);source=ROOT/'OpenSim/Services/Connectors/Simulation/SimulationServiceConnector.cs';text=source.read_text(encoding='utf-8-sig');methods=''.join(extract(text,pattern) for pattern in [r'public bool UpdateAgent\(GridRegion destination, AgentData data, EntityTransferContext ctx\)',r'private bool UpdateAgent\(GridRegion destination, IAgentData cAgentData, EntityTransferContext ctx, int timeout\)',r'public IScene GetScene\(']);wrapper=w/'Methods.cs';wrapper.write_text('using System;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class RemoteDataReference{'+methods+'}',encoding='utf-8');assembly=build_reference(a.dotnet,w,[wrapper,ROOT/'native-port/tests/simulation_agent_data_reference.cs'],'RemoteDataReference')
 cases=[{'profile':profile,'version':version,'replies':[reply,{'Success':True}]} for profile,version,reply in itertools.product([0,1,2,4,8,16],[0,.1,.2,.3,.8],[{'Success':True},{'Success':False},{'success':True},{},None,'throw'])];commands=[json.dumps(value,separators=(',',':')).encode().hex() for value in cases];rows=[]
 for command in [[a.dotnet,str(assembly)],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180)
  if r.returncode:raise RuntimeError(r.stdout[-1000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'simulation-agent-data-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(commands),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Complete original AgentData overload/generic worker/GetScene with actual full model dependencies; controlled transport/region projection; full HTTP/cache/scene/runtime pending','differences':diffs[:3]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:1],indent=2)[:6000]+' count='+str(len(diffs)))
 print(f'PASS: {len(commands)} full agent-update connector source observations')
if __name__=='__main__':main()
