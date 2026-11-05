"""Unchanged advanced-profile configuration and region lifecycle methods."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/advanced_profile_reference.cs','OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs','OpenSim/Framework/GridInfo.cs']
def extract(sig,path=None):
 s=(root/(path or sources[1])).read_bytes().decode('utf-8-sig');start=s.index(sig);i=s.index('{',start)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
sigs=['public void Initialise(','public void AddRegion(','public void RemoveRegion(','public void RegionLoaded(','public void Close(','private void HandleShowStatus(','public Scene Scene','public IConfigSource Config','public string ProfileServerUri','public bool Enabled']
(w/'Extracted.cs').write_text('using System;using System.Net;using Nini.Config;partial class Original{'+''.join(extract(sig) for sig in sigs)+'}namespace Real{'+extract('public enum OSHTTPURIFlags',sources[2])+extract('public struct OSHTTPURI:',sources[2])+'static class Util{public static System.Net.IPAddress GetHostFromDNS(string host)=>System.Net.IPAddress.TryParse(host,out var ip)?ip:null;}}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [root/sources[0],w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(f))
for name in ['OpenMetaverse','OpenMetaverseTypes','Nini']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def enc(s):return s.encode().hex() or '.'
configs=['','[Other]\nx=y\n','[UserProfiles]\n','[UserProfiles]\nProfileServiceURL=\n']
configs += ['[UserProfiles]\nProfileServiceURL='+url+'\n'+setting for url in ['http://host/path','https://HOST:9000/a?b=é',' ','not uri'] for setting in ['', 'AllowUserProfileWebURLs=true\n','AllowUserProfileWebURLs=false\n','AllowUserProfileWebURLs=bad\n']]
valid=enc('[UserProfiles]\nProfileServiceURL=http://host/a\nAllowUserProfileWebURLs=false\n')
flags=[0]+[1<<i for i in range(23)]+[3,129,258,2056,18432]
commands=[]
for f in flags:
 for config in configs:
  token='I'+enc(config)
  commands += [f'{f} '+flow for flow in [f'{token} A L E D S X B Z F T R C S W U',f'I{valid} A L {token} B L E F R C A X',f'{token} N L S C I- B Z R',f'I{valid} A B A C B Z D T R S {token} L']]
for url in ['http://127.0.0.1/a/b?query=x#frag','HTTPS://127.0.0.1:9000/a/../b','http://[::1]/p','http://127.0.0.1/é','http://127.0.0.1:65536/path','not uri',' ']:
 token='I'+enc('[UserProfiles]\nProfileServiceURL='+url+'\n')
 commands.append('8388608 '+token+' A L S R C '+token+' B Z S')
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as out:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=out,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text().splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:8],applicationParityVerified=False,scope='Complete unchanged Initialise/AddRegion/RemoveRegion/RegionLoaded/Close/HandleShowStatus and four properties; actual Nini. Real original OSHTTPURI normalization/numeric resolution is composed with lifecycle for7 additional flows; other OSHTTPURI construction/resolution, console/log, scene/module/event, cache and queue boundaries. Native lifecycle is an abstract base requiring real request/cache controller; advanced module production binding and remaining methods/macOS pending.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' lifecycle differences; see results.json';print('PASS:',len(commands),'original advanced profile lifecycle flows')
