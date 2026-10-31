"""Original whole CapabilitiesModule with real circuit DTO and controlled scene/caps/console adapters."""
import argparse,hashlib,itertools,json,subprocess
from pathlib import Path
from compare_agent_data import ROOT,build_reference
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);source=ROOT/'OpenSim/Region/CoreModules/Framework/Caps/CapabilitiesModule.cs';text=source.read_text(encoding='utf-8-sig').replace('private static readonly ILog m_log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);','private static readonly global::CapLog m_log = new();');wrapper=w/'Module.cs';wrapper.write_text(text,encoding='utf-8');assembly=build_reference(a.dotnet,w,[wrapper,ROOT/'native-port/tests/capabilities_module_reference.cs',ROOT/'OpenSim/Framework/AgentCircuitData.cs'],'CapReference',references=['Nini'])
 add={'op':'add'};seed={'op':'seed'};create={'op':'create'};inspect=[{'op':'path'},{'op':'get'},{'op':'get_children'},{'op':'child'}];starts=[[],[add],[add,seed],[add,seed,create],[add,seed,create,{'op':'seed','path':'second'}],[add,seed,create,{'op':'add','scene':'B'}],[add,seed,create,{'op':'create','code':2}],[add,seed,create,{'op':'seed','agent':2},{'op':'create','agent':2,'code':2}]]
 actions=[{'op':op,'agent':agent,'code':code} for op,agent,code in itertools.product(['create','remove','activate','get','path','drop','child','get_children','dump'],[1,2,3],[0,1,2,9])]+[{'op':op} for op in ['remove_region','lifecycle','list','user_stats']]+[{'op':'stats','count':n} for n in [0,4,5,6,7]]
 cases=[{'ops':start+[action]+inspect} for start,action in itertools.product(starts,actions)]
 cases += [{'ops':[add,{'op':'seed','path':path,'null_seeds':null},create,{'op':'activate'},create,{'op':'list'}]+inspect} for path,null in itertools.product([None,'','seed','試験 😀','/absolute/path/'],[False,True])]
 cases += [{'ops':[add,seed,create,{'op':'seed','path':'different'},create,{'op':'remove','code':code}]+inspect,'failure':failure} for failure,code in itertools.product(['module-register','module-unregister','command','factory','register-event','deregister-event','old-deregister','activate','dispose','console','info'],[1,9])]
 for mutation in ['register-event','factory','deregister-event','old-deregister','info']:cases.append({'ops':[add,seed,create,{'op':'seed','path':'other'},create,{'op':'remove','code':9},{'op':'dump'}]+inspect,'mutation':mutation})
 cases += [{'ops':[{'op':'get_children'},{'op':'mutate_saved'},{'op':'child','handle':512},{'op':'get_children'}]},{'ops':[seed,{'op':'mutate_seed'},{'op':'child','handle':512},{'op':'drop','handle':512},{'op':'child','handle':512}]},{'ops':[{'op':'set_children','null_seeds':True},{'op':'get_children'},{'op':'drop'},{'op':'child'}]},{'ops':[add,{'op':'seed','null_agent':True}]+inspect},{'ops':[add,seed,create,{'op':'select','scene':'B'},{'op':'list'},{'op':'stats','count':5}]},{'ops':[add,seed,create,{'op':'add','null_scene':True},create,{'op':'remove'}]+inspect},{'ops':[add,seed,create,{'op':'list'}],'no_server':True}]
 commands=[json.dumps(c,ensure_ascii=False,separators=(',',':')).encode().hex() for c in cases];rows=[]
 for command in [[a.dotnet,str(assembly)],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180)
  if r.returncode:raise RuntimeError(r.stdout[-1000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'capabilities-module-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Original complete class/method bodies; logger field type projection, real AgentCircuitData/Nini/SDK and controlled scene, Caps constructor/runtime, console and event adapters. Object identity/reuse, shared seeds, callbacks/failure state and console text verified. Concrete Caps/HTTP/seed handlers, module loader and general CLR/concurrency/lifetime/platform remain pending.','differences':diffs[:4]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:1],indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(cases)} capabilities module source observations')
if __name__=='__main__':main()
