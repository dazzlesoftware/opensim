"""Complete original group/controller constructors and pack/unpack with bundled SDK."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT,extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Framework/ChildAgentDataUpdate.cs';text=source.read_text(encoding='utf-8-sig');classes=''.join(extract(text,r'public class '+name) for name in ['AgentGroupData','ControllerData']);wrapper=w/'Models.cs';wrapper.write_text('using System;using OpenMetaverse;using OpenMetaverse.StructuredData;namespace OpenSim.Framework{'+classes+'}',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [wrapper,ROOT/'native-port/tests/agent_update_records_reference.cs',ROOT/'OpenSim/Framework/Animation.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 path=w/'Records.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 seven='00000000-0000-0000-0000-000000000007';values=[None,True,False,0,1,-1,1.5,2147483647,2147483648,4294967295,'','bad','-0','-1',' 1 ','1\0','1\0 ','18446744073709551615','18446744073709551616','1,000','1.5',{},[],[1,2,3],[255,255,255,255],seven,seven+'tail'];cases=[]
 for model,fields in [('group',['group_id','group_powers','accept_notices']),('controller',['object','item','ignore','event']),('animation',['animation','object_id','seq_num'])]:
  requests=[{},None,[],{'group_id':seven,'object':seven,'item':seven,'group_powers':'99','ignore':-1,'event':2147483648,'accept_notices':True}]
  for field,value in itertools.product(fields,values):requests.append({field:value})
  for profile,data in itertools.product(range(8),requests):cases.append({'model':model,'profile':profile,'data':data,'next':{'group_powers':'bad','ignore':1.5,'event':[1,2,3,4],'seq_num':-2147483648}})
  for profile in range(4):cases.append({'model':model,'profile':profile,'data':{},'null_map':True})
  for profile,field,typed in itertools.product(range(4),fields,['binary','unsigned','long','uuid','date','uri','unknown']):cases.append({'model':model,'profile':profile,'data':{},'field':field,'typed':typed})
 for model,raw in [('group',[[str(n),True] for n in [0,1,2**32,2**64-1]]),('animation',[[str(x),'0'] for x in [-2147483648,-1,0,2147483647]]),('controller',[[str(x),str(y)] for x,y in itertools.product([0,1,2**31-1,2**31,2**32-1],repeat=2)])]:
  for value in raw:cases.append({'model':model,'profile':8,'data':{},'raw':value})
 commands=[json.dumps(value,separators=(',',':'),ensure_ascii=False).encode().hex() for value in cases];rows=[]
 for command in [[a.dotnet,str(w/'app/Records.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
  if r.returncode:raise RuntimeError(r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'input':value,'source':x,'native':y} for value,x,y in zip(cases,*rows) if x!=y];(w/'agent-update-records-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(commands),'sourceHashes':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,ROOT/'OpenSim/Framework/Animation.cs']},'scope':'Complete original AgentGroupData/ControllerData and Animation constructors/pack/unpack, Animation equality/string/hash stability, JSON and selected typed SDK inputs; numeric CLR identity hash, generic CLR-object equality, null OSD slots/runtime/scene/platform semantics remain pending','differences':diffs[:4]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:2],indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(commands)} agent transfer record source observations')
if __name__=='__main__':main()
