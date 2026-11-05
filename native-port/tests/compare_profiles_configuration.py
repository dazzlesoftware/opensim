"""Compare the full original profile service and connector constructors."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profiles_service_reference.cs','OpenSim/Services/UserProfilesService/UserProfilesService.cs','OpenSim/Services/Interfaces/IUserProfilesService.cs','OpenSim/Data/IProfilesData.cs','OpenSim/Framework/UserProfiles.cs','native-port/tests/profiles_configuration_reference.cs','OpenSim/Services/UserProfilesService/UserProfilesServiceBase.cs','OpenSim/Server/Handlers/Profiles/UserProfilesConnector.cs','OpenSim/Server/Handlers/Profiles/UserProfilesHandlers.cs','OpenSim/Framework/Servers/HttpServer/JsonRpcResponse.cs','OpenSim/Framework/Servers/HttpServer/JsonRPCMethod.cs']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(StartupObject='ConfigurationProgram',TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in sources:ET.SubElement(items,'Compile',Include=str(root/f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','Nini']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def encoded(text):return '-' if text is None else text.encode().hex() or '.'
configs=['','[DatabaseService]\nStorageProvider=db\nConnectionString=db-connection\n','[Profiles]\n','[UserProfilesService]\n']
for section in ['Profiles','UserProfilesService',' ','Other','試験']:
 for enabled in ['', 'true','false','yes','1','bad',' true ']:
  for properties in ['', 'StorageProvider=profiles\nConnectionString=profile-connection\nUserAccountService=account\nLocalServiceModule=service\nRealm=ignored\n','StorageProvider=\nConnectionString=\nUserAccountService= \nLocalServiceModule=\n']:
   configs.append('[DatabaseService]\nStorageProvider=db\nConnectionString=db-connection\n['+section+']\n'+('Enabled='+enabled+'\n' if enabled else '')+properties)
commands=[f'{op} {mode} {flags} {encoded(name)} {encoded(config)}' for op in ['S','C'] for mode in range(7) for flags in [0,1,2,4,6] for name in [None,'','Profiles','UserProfilesService',' ','Other','試験'] for config in configs]
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as output:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=output,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text(encoding='utf-8').splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=x,native=y) for c,x,y in zip(commands,outputs['reference'],outputs['native']) if x!=y]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,scope='Configuration constructors of original UserProfilesServiceBase/UserProfilesService/UserProfilesConnector with original handlers/DTOs, actual SDK and Nini. Controlled base ServiceBase/ServerUtils plugin factory and HTTP registration boundaries; section/default/enable/bool validation, factory ordering/arguments/failures, null config/server, duplicate registration and retained ordered names. The separate service gate verifies method bodies. Full plugin loader, concrete storage, production executable/viewer integration and whole application/macOS parity incomplete.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' profile configuration differences; see results.json';print('PASS:',len(commands),'original profile configuration scenarios')
