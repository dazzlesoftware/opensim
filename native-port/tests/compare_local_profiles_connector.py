"""Compare the unchanged local profile connector lifecycle and repeated initialization."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profiles_service_reference.cs','OpenSim/Services/UserProfilesService/UserProfilesService.cs','OpenSim/Services/Interfaces/IUserProfilesService.cs','OpenSim/Data/IProfilesData.cs','OpenSim/Framework/UserProfiles.cs','native-port/tests/profiles_configuration_reference.cs','OpenSim/Services/UserProfilesService/UserProfilesServiceBase.cs','OpenSim/Server/Handlers/Profiles/UserProfilesConnector.cs','OpenSim/Server/Handlers/Profiles/UserProfilesHandlers.cs','OpenSim/Framework/Servers/HttpServer/JsonRpcResponse.cs','OpenSim/Framework/Servers/HttpServer/JsonRPCMethod.cs','native-port/tests/local_profiles_connector_reference.cs','OpenSim/Region/CoreModules/ServiceConnectorsIn/UserProfiles/LocalUserProfilesServiceConnector.cs']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(StartupObject='LocalProfilesProgram',TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in sources:ET.SubElement(items,'Compile',Include=str(root/f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','Nini']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def encoded(text):return text.encode().hex() or '.'
configs=['','[Modules]\nUserProfilesServices=Other\n','[Modules]\nUserProfilesServices=LocalUserProfilesServicesConnector\n']
for selected in ['', 'Other','LocalUserProfilesServicesConnector']:
 for enabled in ['', 'true','false','yes','1','bad']:
  for dll in ['', 'profile',' ']:
   configs.append('[Modules]\nUserProfilesServices='+selected+'\n[UserProfilesService]\n'+('Enabled='+enabled+'\n' if enabled else '')+'LocalServiceModule='+dll+'\n')
valid=encoded('[Modules]\nUserProfilesServices=LocalUserProfilesServicesConnector\n[UserProfilesService]\nEnabled=true\nLocalServiceModule=profile\n')
disabled=encoded('[UserProfilesService]\nEnabled=false\n')
commands=[]
for mode in range(3):
 for flags in range(8):
  for op in ['I','S']:
   for config in configs:
    token=op+encoded(config)
    for flow in [f'{token} A B D N Z R A T U C P L',f'S{valid} A {token} D B R S{disabled} R A S{valid} B C',f'{token} S. A I- S- B']:
     commands.append(f'{mode} {flags} '+flow)
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as output:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=output,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text(encoding='utf-8').splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=x,native=y) for c,x,y in zip(commands,outputs['reference'],outputs['native']) if x!=y]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,scope='Unchanged full LocalUserProfilesServicesConnector, actual SDK/Nini and original profile handlers/DTOs. Controlled plugin, MainServer getter and consumed Scene.RegionInfo.RegionID boundaries. Repeated initialize/module selection/service initialization, enabled/stale service retention, errors/logging, server getter order/null/throws, region identities/duplicate/remove/null/no-op lifecycle and registered delegate names. Native concrete scene/viewer integration and full application/macOS incomplete.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' local profile connector differences; see results.json';print('PASS:',len(commands),'original local profile connector scenarios')
