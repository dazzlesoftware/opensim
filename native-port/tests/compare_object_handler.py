"""Whole original object HTTP handler with SDK conversions and controlled scene callbacks."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Server/Handlers/Simulation/ObjectHandlers.cs';project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [source,ROOT/'native-port/tests/object_handler_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','OpenMetaverse','OpenMetaverse.StructuredData']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 path=w/'ObjectHandler.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 seven='00000000-0000-0000-0000-000000000007';base={'destination_uuid':seven,'sog':'<object/>','extra':'extra','modified':True,'state':'scripts','new_position':[1,2,30],'destination_x':256,'destination_y':512,'destination_name':'Destination'};requests=[{},base,{'destination_uuid':seven},{'sog':'<object/>'}]
 for key,values in {'destination_uuid':[None,'bad',seven+'tail',True,{},[]],'sog':[None,'',True,1.5,{},[],['object'],'<試験/>'],'destination_x':[None,'bad','-256','2147483648',1.5,True,{},[]],'destination_y':[None,'1\0','1\0 ',[]],'destination_name':[None,'',True,1.5,{},[],{'n':'v'}],'new_position':[None,'bad','<1,2,3>','prefix<1,2,3>tail','<NaN, ∞, -∞>',[],[1,2],[1,2,3,4],['1.5',True,{}],{},True],'extra':[None,'',True,1.5,{},[]],'modified':[None,'false','FALSE','1',0,1,{},[]],'state':[None,'',True,1.5,{},[]]}.items():
  for value in values:requests.append({**base,key:value})
 profiles=[{}, {'allow':True},{'deny':True},{'null_scene':True},{'null_object':True},{'null_service':True},{'override':True},{'override':True,'deny':True}]+[{'failure':f,'allow':True} for f in ['scene','deserialize','extra','modified','allow','state','create','override']]
 cases=[{**profile,'core':core,'method':'POST','args':args} for profile,core,args in itertools.product(profiles,[False,True],requests)]
 cases += [{**profile,'method':method,'shard':shard,'args':args} for profile,method,shard,args in itertools.product([{}, {'null_service':True}],['POST','DELETE','GET','post','PUT'],[False,True],[{},base])]
 cases += [{'method':'POST','shard':shard,'body':body} for shard,body in itertools.product([False,True],['','bad','null','[]','true','{','{}','{"destination_uuid":null,"sog":"x"}'])]
 commands=[json.dumps(c,ensure_ascii=False,separators=(',',':')).encode().hex() for c in cases];rows=[]
 for command in [[a.dotnet,str(w/'app/ObjectHandler.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
  if r.returncode:raise RuntimeError(r.stdout[-1000:]+r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'input':c,'source':x,'native':y} for c,x,y in zip(cases,*rows) if x!=y];(w/'object-handler-parity-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sourceHash':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Entire original ObjectSimpleHandler including ProcessRequest, DoObjectPost and virtual CreateObject; actual bundled SDK and projected scene/service/HTTP/logger with JSON body adapter. Full scene serialization, HTTP host, CLR-null OSD and platform runtime remain partial. Gzip body helper verified separately.','differences':diffs[:5]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:2],indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(cases)} object handler source observations')
if __name__=='__main__':main()
