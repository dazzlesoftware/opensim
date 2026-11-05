"""Original picks workflow composed with actual SDK, profile/group models and cache."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profile_requests_reference.cs','native-port/tests/profile_picks_reference.cs','OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs','OpenSim/Framework/UserProfiles.cs','OpenSim/Framework/GroupData.cs','OpenSim/Framework/ExpiringCacheOS.cs']
def extract(path,sig):
 s=(root/path).read_bytes().decode('utf-8-sig');start=s.index(sig);i=s.index('{',start)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
sigs=['public void NotesRequest(','public void NotesUpdate(','public void UserPreferencesRequest(','public void UpdateUserPreferences(','bool GetUserProfileServerURI(','ScenePresence FindPresence(','public void PicksRequest(','public Dictionary<UUID, string> GetPicks(']
(w/'Extracted.cs').write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;partial class Original{'+''.join(extract(sources[2],sig) for sig in sigs)+'}namespace OpenSim.Framework{'+extract(sources[4],'public class GroupMembershipData')+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',StartupObject='PicksFixture',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [root/sources[0],root/sources[1],root/sources[3],root/sources[5],w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def enc(s):return '-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
uris=[None,'',' \t\u2000','http://profile/'];args=[None,'bad','00000000-0000-0000-0000-000000000003','11111111-1111-0000-0000-000100bba000'];texts=[None,'','name 😀<&','a\x00b','a\ud800q']
flows=['R R G M R G W G R','G G M G R C G','N R E G O R G M R','K R G D G R','O G t300000 P R t300001 P G','L R G t360000 P m1 G R','R t400000 G P R C R','G m1 G m12 D G R']
flags=[0,1,2,4,8,16,16|32,16|64,128,16|256,512,512|1024,2048,4096,16384,1048576,16777216,33554432,33554432|16777216]
commands=[]
for mode in list(range(15))+[17]:
 for f in flags:
  for uri,arg,text in zip(uris,args,texts):
   for flow in flows:commands.append(f'{mode} {f} {enc(uri)} {enc(arg)} {enc(text)} '+flow)
commands += [f'{mode} {f} {enc(uri)} {enc(arg)} {enc(text)} '+flow for mode in [1,8,12,13,14] for f in [0,16,16777216] for uri in ['http://profile/'] for arg in args[2:] for text in texts for flow in flows]
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as out:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=out,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text().splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:8],applicationParityVerified=False,scope='Unchanged PicksRequest/GetPicks plus original routing/presence helpers, actual SDK OSD/UUID, original UserProfiles/GroupMembershipData and full ExpiringCacheOS. Clock/timer and RPC/scene/client boundaries controlled. Repeated actual cache/list/object identity, callback reentry and mutation, expiry/purge, cached null/empty/sentinel entries, source group defaults and cache reference preservation, failure/malformed/duplicate responses, local/HG/NPC/system/child rules and exact errors/send order. Actual runtime controller integration and full application/macOS pending.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' picks differences; see results.json';print('PASS:',len(commands),'original cached picks flows')
