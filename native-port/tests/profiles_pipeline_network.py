"""Original configuration/service/handlers versus their composed native HTTP path."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,http.client
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profiles_service_reference.cs','native-port/tests/profiles_configuration_reference.cs','native-port/tests/profiles_pipeline_reference.cs','OpenSim/Services/UserProfilesService/UserProfilesService.cs','OpenSim/Services/Interfaces/IUserProfilesService.cs','OpenSim/Data/IProfilesData.cs','OpenSim/Framework/UserProfiles.cs','OpenSim/Services/UserProfilesService/UserProfilesServiceBase.cs','OpenSim/Server/Handlers/Profiles/UserProfilesConnector.cs','OpenSim/Server/Handlers/Profiles/UserProfilesHandlers.cs','OpenSim/Framework/Servers/HttpServer/JsonRpcResponse.cs','OpenSim/Framework/Servers/HttpServer/JsonRPCMethod.cs']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(StartupObject='ProfilesPipelineProgram',TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in sources:ET.SubElement(items,'Compile',Include=str(root/f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','Nini']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
names=['avatarclassifiedsrequest','classified_update','classifieds_info_query','classified_delete','avatarpicksrequest','pickinforequest','picks_update','picks_delete','avatarnotesrequest','avatar_notes_update','avatar_properties_request','avatar_properties_update','avatar_interests_update','user_preferences_update','user_preferences_request','image_assets_request','user_data_request','user_data_update']
ident='01234567-89ab-cdef-0123-456789abcdef'
params=dict(creatorId=ident,classifiedId=ident,avatarId=ident,pickId=ident,PickId=ident,CreatorId=ident,UserId=ident,Flags=1,Name='incoming',EMail='',Notes='incoming',DataKey='key',DataVal='value')
cases=[]
for name in names:
 for mode in range(10):
  for account in [0,1,2,3,4,5,6,7]:
   request=dict(id='wireid',jsonrpc='2.0',method=name,params=params)
   cases.append((mode,account,json.dumps(request,separators=(',',':')).encode()))
 for value in [None,[],False,{},dict(EMail='incoming@example.test'),dict(EMail=' ')]:cases.append((0,6,json.dumps(dict(id='wireid',jsonrpc='2.0',method=name,params=value),separators=(',',':')).encode()))
commands=[f'{mode} {account} '+body.hex() for mode,account,body in cases]
r=subprocess.run([a.dotnet,str(w/'app/Reference.dll')],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr;expected=r.stdout.splitlines();assert len(expected)==len(cases);(w/'reference.txt').write_text(r.stdout,encoding='utf-8')
process=subprocess.Popen([a.native,'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);connection=None
try:
 ready=process.stdout.readline().strip();assert ready.startswith('LISTENING '),ready;port=int(ready.split()[1])
 for i,((mode,account,body),source) in enumerate(zip(cases,expected)):
  if i%50==0:
   if connection:connection.close()
   connection=http.client.HTTPConnection('127.0.0.1',port,timeout=15)
  connection.request('POST' if i%2==0 else 'GET',f'/{mode}/{account}',body,{'Content-Type':'application/json-rpc'})
  response=connection.getresponse();actual=response.read();assert response.status==200 and response.getheader('Content-Type')=='text/html;charset=UTF-8',(i,response.status,response.getheaders(),actual)
  assert actual.hex()==source,(i,commands[i],source,actual)
 connection.close();connection=None;process.stdin.write('stop\n');process.stdin.flush();stdout,stderr=process.communicate(timeout=20);assert process.returncode==0,(stdout,stderr)
finally:
 if connection:connection.close()
 if process.poll() is None:process.kill();process.communicate()
report=dict(passed=True,requests=len(cases),applicationParityVerified=False,scope='Original service/base constructors, connector, handlers, DTOs and SDK composed behind controlled plugin/storage/account boundaries versus native configuration factory, service, registered handler, dispatcher and real HTTP listener. All eighteen methods, storage failures/throws/ref/null mutations, eight account identity/result modes, email fallback and invalid parameters; exact source-derived wire bytes and POST/GET batches up to50. CLR plugin loading, concrete SQLite/MySQL/PGSQL provider and production executable/viewer binding, connection limit and whole application/macOS parity incomplete.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));print('PASS:',len(cases),'composed profile service HTTP requests')
