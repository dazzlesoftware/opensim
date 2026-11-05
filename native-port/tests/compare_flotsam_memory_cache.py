"""Original Flotsam file-disabled cache methods and full original expiration containers."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs';text=source.read_text(encoding='utf-8');generated=w/'ExtractedFlotsamMemory.cs';signatures=['private void UpdateWeakReference(', 'private void UpdateMemoryCache(', 'public void Cache(', 'public void CacheNegative(', 'private AssetBase GetFromWeakReference(', 'private AssetBase GetFromMemoryCache(', 'private bool CheckFromMemoryCache(', 'public bool Get(string id, out AssetBase asset)', 'public bool GetFromMemory(', 'public bool Check(', 'public AssetBase GetCached(', 'public void Expire(', 'public void Clear(', 'private List<string> GenerateCacheHitReport(']
 generated.write_text('using System;using System.IO;using System.Collections.Generic;using System.Runtime.InteropServices;using OpenMetaverse;using OpenSim.Framework;partial class FlotsamAssetCache{'+''.join(extract(text,s) for s in signatures)+'}',encoding='utf-8')
 slutil=ROOT/'OpenSim/Framework/SLUtil.cs';conversion=w/'SLUtil.cs';s=slutil.read_text(encoding='utf-8-sig');conversion.write_text(s[:s.index('static readonly char[] seps')]+'\n}}\n',encoding='utf-8')
 originals=[ROOT/'OpenSim/Framework'/n for n in ['AssetBase.cs','NrbfReader.cs','ExpiringCacheOS.cs','ExpiringKey.cs']];project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',AllowUnsafeBlocks='true',NoWarn='0436;0649;0169;0414').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,conversion,ROOT/'native-port/tests/flotsam_memory_cache_reference.cs']+originals:ET.SubElement(group,'Compile',Include=str(f))
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll');cs=w/'FlotsamMemoryReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True,encoding='utf-8');assert r.returncode==0,r.stdout+r.stderr
 commands=[];rng=random.Random(732901)
 for memory,negative,seconds,negms in itertools.product(range(2),range(2),[-1,0,.016,.5,2.9,57.6,5000000],[-1,120,3000]):
  commands += [f'init {memory} {negative} {seconds} {negms}','cache 0','negative 0','get 0','cached 0','from 0','check 0','drop 0','advance 2000','get 0','cached 0','advance 1','get 0','advance 2000','get 0','cache 7','get 7','cached 7','check 7','mutate 7','cached 7','clear','cached 7','report 0']
  for _ in range(100):
   op=rng.choice(['cache','null','drop','mutate','negative','get','from','cached','check','expire','clear','advance','report','memoryclear','negativeclear']);commands.append(op+' '+str(rng.choice([0,1,499,500,501,1999,2000,2001,10000,2147483647]) if op=='advance' else rng.randrange(8)))
 for reset,age in itertools.product(['clear','memoryclear 0','negativeclear 0'],[2147483000,2147483647,2147483648,4294967296]):
  commands += ['init 1 1 .5 120',f'advance {age}',reset,'cache 1','negative 2','advance 4000','drop 1','get 1','get 2','report 0']
 outputs=[]
 for cmd in [[a.dotnet,str(w/'app/FlotsamMemoryReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110);assert r.returncode==0,r.stderr;outputs.append(r.stdout.splitlines())
 rates=[]
 for request,hit,memory in itertools.product([0,1,2,3,7,8,16,32,64,128,256,512,1024,999999,2**53-1,2**53+1,2**64-1],[0,1,3,7,15,31,63,100,999,2**53+1,2**64-1],range(2)):
  rates.append(f'{request} {hit} {hit} {hit} {2**64-1} 3 2 {memory}')
 for _ in range(12000):
  values=[rng.randrange(2**64) for _ in range(5)];rates.append(' '.join(map(str,values+[3,2,rng.randrange(2)])))
 rate_outputs=[]
 for cmd in [[a.dotnet,str(w/'app/FlotsamMemoryReference.dll'),'--report-probe'],[a.native,'--report-probe']]:
  r=subprocess.run(cmd,input='\n'.join(rates)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110);assert r.returncode==0,r.stderr;rate_outputs.append(r.stdout.splitlines())
 assert len(rate_outputs[0])==len(rate_outputs[1])==len(rates)
 rate_diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(rates,*rate_outputs) if s!=n]
 assert not rate_diffs,json.dumps(rate_diffs[:3])
 assert len(outputs[0])==len(outputs[1])==len(commands);diffs=[dict(index=i,command=c,source=s,native=n,context=commands[max(0,i-6):i]) for i,(c,s,n) in enumerate(zip(commands,*outputs)) if s!=n];files=[source,slutil]+originals+[ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diffs,cases=len(commands),rateCases=len(rates),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Unchanged original Flotsam memory/weak/negative cache methods with FileCacheEnabled=false and full original ExpiringCacheOS/ExpiringKey, actual AssetBase/SDK. Clock/timer callbacks controlled; forced GC at operation boundaries makes weak ownership reproducible. Cache identity, shared mutation, all getters, negative bypass/zero UUID differences, no read-time purge, strict periodic expiry, refresh, minimum checks, zero/negative durations, milliseconds/seconds and Int32 wrap/cast behavior. Native real worker expiry separately checked. File cache/config/module/scene scanning, finalization timing and concurrent schedules remain pending.',differences=diffs[:4]);(w/'flotsam-memory-cache-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {len(commands)} original Flotsam memory cache observations')
if __name__=='__main__':main()
