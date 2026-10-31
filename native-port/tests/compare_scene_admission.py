"""Original whole NewUserConnection and VerifyUserPresence bodies with actual DTO/circuit manager."""
import argparse,hashlib,itertools,json,subprocess
from pathlib import Path
from compare_agent_data import ROOT,build_reference
from compare_presence_endpoint import extract
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);source=ROOT/'OpenSim/Region/Framework/Scenes/Scene.cs';text=source.read_text(encoding='utf-8-sig');method=extract(text,r'public bool NewUserConnection\(AgentCircuitData acd, uint teleportFlags, GridRegion source, out string reason, bool requirePresenceLookup\)').replace('Thread.Sleep(pollInterval)','Sleep(pollInterval)');verify=extract(text,r'public virtual bool VerifyUserPresence\(');constants=extract((ROOT/'OpenSim/Framework/Constants.cs').read_text(encoding='utf-8-sig'),r'public enum TeleportFlags');wrapper=w/'Methods.cs';wrapper.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using TPFlags=OpenSim.Framework.Constants.TeleportFlags;using GridRegion=OpenSim.Services.Interfaces.GridRegion;public partial class AdmissionReference{'+method+verify+'}namespace OpenSim.Framework{public static class Constants{'+constants+'}}',encoding='utf-8');assembly=build_reference(a.dotnet,w,[wrapper,ROOT/'native-port/tests/scene_admission_reference.cs',*[ROOT/'OpenSim/Framework'/s for s in ['AgentCircuitData.cs','AgentCircuitManager.cs','AuthenticateResponse.cs','Login.cs']]],'AdmissionReference')
 presences=['none','child','root','deleted','preremove','removing_child','removing_root','removed'];flags=[0,1,4,8,16,32,128,1073741824,160]
 cases=[{'presence':presence,'flags':flag,'require_presence':require,'transit':transit} for presence,flag,require,transit in itertools.product(presences,flags,[False,True],[False,True])]
 profiles=[{}, {'enabled':False},{'null_agent':True},{'presence_service':False},{'missing_grid':True},{'wrong_grid':True},{'auth_deny':True},{'land_deny':True},{'caps':False},{'cache':False},{'see':False},{'landmarks':True},{'client':False},{'old_circuit':True},{'reset_caps':True},{'after':1},{'after':10},{'after':11},{'after':2,'delete_after':True}]+[{'failure':f} for f in ['viewer','incoming','presence','state','transit','not-close','sleep','land','remove','close','cache-module','cache-remove','add','presence-module','grid-presence','authorize','seeds','create-caps','adjust','name','activate']]
 cases += [{**profile,'presence':presence,'flags':flag,'transit':True} for profile,presence,flag in itertools.product(profiles,['none','child','root','preremove','removing_child'],[0,128])]
 for viewer,allowed,banned in itertools.product(['Viewer 1','VIEWER 1','Other','','  Viewer 1  ','試験 1','😀 1'],[[],['viewer'],['Viewer'],['viewer','other'],[''],[None],['試験'],['😀']], [[],['viewer'],[''],[None]]):cases.append({'viewer':viewer,'allowed':allowed,'banned':banned})
 for position,flag in itertools.product([[-1,-2,30],[256,512,30],[0,0,0],['NaN','Infinity',0]],[0,128]):cases.append({'position':position,'flags':flag})
 for profile in profiles:cases.append({**profile,'verify_only':True})
 commands=[json.dumps(c,ensure_ascii=False,separators=(',',':')).encode().hex() for c in cases];rows=[]
 for command in [[a.dotnet,str(assembly)],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
  if r.returncode:raise RuntimeError(r.stdout[-1000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'scene-admission-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Complete original five-argument NewUserConnection and VerifyUserPresence; Thread.Sleep replaced with controlled scheduler. Actual AgentCircuitData/Manager/Login/SDK and controlled scene presence, caps/cache/presence/auth/land/viewer/logging dependencies. Full viewer-name/locale lowering, concrete scene/caps/actor lifecycle, scheduling and platform semantics remain pending.','differences':diffs[:5]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:2],indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(cases)} scene admission source observations')
if __name__=='__main__':main()
