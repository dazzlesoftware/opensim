"""Original classified-detail workflow composed with actual SDK, profile/group models and cache."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profile_requests_reference.cs','native-port/tests/profile_picks_reference.cs','native-port/tests/profile_classifieds_reference.cs','native-port/tests/profile_classified_info_reference.cs','OpenSim/Region/CoreModules/Avatar/UserProfiles/UserProfileModule.cs','OpenSim/Framework/UserProfiles.cs','OpenSim/Framework/GroupData.cs','OpenSim/Framework/ExpiringCacheOS.cs']
def extract(path,sig):
 s=(root/path).read_bytes().decode('utf-8-sig');start=s.index(sig);i=s.index('{',start)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
sigs=['public void NotesRequest(','public void NotesUpdate(','public void UserPreferencesRequest(','public void UpdateUserPreferences(','bool GetUserProfileServerURI(','ScenePresence FindPresence(','public void PicksRequest(','public Dictionary<UUID, string> GetPicks(','public void ClassifiedsRequest(','public void ClassifiedInfoRequest(','public void ClassifiedDelete(','void cacheForeignImage(']
(w/'Extracted.cs').write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using ScenePresence=Presence;partial class Original{'+''.join(extract(sources[4],sig) for sig in sigs)+'}namespace OpenSim.Framework{'+extract(sources[6],'public class GroupMembershipData')+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',StartupObject='InfoFixture',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [root/sources[0],root/sources[1],root/sources[2],root/sources[3],root/sources[5],root/sources[7],w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def enc(s):return '-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
uris=['http://profile/',' ','http://profile/','http://profile/'];positions=['<1,2,3>','bad',None,'<NaN,-0,∞>'];assets=['http://assets/','http://assets/',' ',''];names=[None,'name 😀<&','a\x00b','a\ud800q'];query='00000000-0000-0000-0000-000000000003'
flows=['Q Q M Q C Q','B Q Q Q A Q','K B Q Q Q Z Q','I Q J Q T Q','A Q C m1 Q M Q','D Q E Q S Q','L Q Q Q 4 0','F Q t300001 P Q','A K Q X Q','A K X Q X','S X B X Q','Q X m0 X Q']
flags=[0,1,16,16|32,16|64,128,16|256,8192,1048576,524288,4194304,16|4194304,16|67108864,16|134217728,268435456,65536]
commands=[f'{mode} {f} {enc(uri)} {enc(query)} {enc(name)} {enc(pos)} {enc(asset)} '+flow for mode in range(15) for f in flags for uri,pos,asset,name in zip(uris,positions,assets,names) for flow in flows]
commands += [f'{mode} {f} {enc("http://profile/")} {enc(q)} {enc("name")} {enc(pos)} {enc(asset)} '+flow for mode in [1,3,8,10,11,12,13] for f in [0,16] for q in ['00000000-0000-0000-0000-000000000000',query] for pos in ['<0,0,0>','bad','1,2,3','<1,2,3 >tail'] for asset in [None,'http://asset/'] for flow in flows[:3]]
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as out:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=out,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text().splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:8],applicationParityVerified=False,scope='Unchanged ClassifiedInfoRequest/cacheForeignImage plus original routing and list interoperation, actual SDK OSD/UUID, original UserProfiles/GroupMembershipData and full ExpiringCacheOS. Clock/timer and RPC/scene/client boundaries controlled. Repeated cache/list identity and callback reentry/mutation; interest consumption/removal/underflow, target owner routing, shared ad object caching, invalid positions, typed RPC replacement/null/casts/errors and foreign asset ordering; expiry/purge, cached null/empty/sentinel entries, source group defaults and cache reference preservation, failure/malformed/duplicate responses, local/HG/NPC/system/child rules and exact errors/send order. Actual runtime controller integration and full application/macOS pending.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' classified detail differences; see results.json';print('PASS:',len(commands),'original classified detail flows')
