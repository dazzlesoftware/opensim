"""Unchanged Flotsam cleanup methods with controlled filesystem/gather/clock/weak hooks."""
import argparse,hashlib,itertools,json,os,random,stat,subprocess,tempfile,xml.etree.ElementTree as ET
from pathlib import Path
from compare_inventory_asset_caps import extract
ROOT=Path(__file__).resolve().parents[2]
def invoke(cmd,commands=None):
 r=subprocess.run(cmd,input=None if commands is None else '\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110)
 assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
 return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Region/CoreModules/Asset/FlotsamAssetCache.cs';text=source.read_text(encoding='utf-8');generated=w/'ExtractedCleanup.cs';generated.write_text('using System;using System.IO;using System.Timers;using System.Collections.Generic;using OpenMetaverse;using DateTime=ProbeDate;using Directory=FakeDirectory;using File=FakeFile;using Path=FakePath;using Thread=FakeThread;using GC=FakeGC;partial class FlotsamAssetCache{'+''.join(extract(text,s) for s in ['private void CleanupExpiredFiles(', 'private void DoCleanExpiredFiles(', 'private int CleanExpiredFiles('])+'}',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/flotsam_cache_cleanup_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll');cs=w/'CleanupReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);managed=[a.dotnet,str(w/'app/CleanupReference.dll')]
 commands=[];now=638712864000000000
 def row(op='tree',profile=0,running=1,timer=0,warn=3,cool=0,fault=0,nth=1,kind=0,cancel=0,cancel_nth=1,expiration=0,clock=now,purge=now,date_kind=2,heap=0):return ' '.join(map(str,[op,profile,running,timer,warn,cool,fault,nth,kind,cancel,cancel_nth,expiration,clock,purge,date_kind,heap]))
 for op,profile,running,timer,warn,cool in itertools.product(['tree','all','elapsed','retry'],range(8),range(2),range(2),[-1,0,1,3,30000],[-2147483648,-1,0,9,10,11,19,20,2147483647]):commands.append(row(op,profile,running,timer,warn,cool))
 for op,profile,point,nth,kind in itertools.product(['tree','all','elapsed','retry'],[0,1,2,3,4,5],range(1,17),[1,2],range(5)):commands.append(row(op,profile,warn=0,fault=point,nth=nth,kind=kind))
 for op,profile,point,nth in itertools.product(['tree','all','elapsed','retry'],[0,1,2,3,4],range(1,17),[1,2,3]):commands.append(row(op,profile,warn=0,cancel=point,cancel_nth=nth))
 for clock,expiration,kind,heap in itertools.product([0,1,now,3155378975999999999],[-9223372036854775808,-1,0,1,1728000000000,9223372036854775807],range(3),[-9223372036854775808,-1048577,-1048575,0,1048577,9223372036854775807]):commands.append(row('retry',5,0,1,expiration=expiration,clock=clock,date_kind=kind,heap=heap))
 rng=random.Random(710355)
 for _ in range(10000):commands.append(row(rng.choice(['tree','all','elapsed','retry']),rng.randrange(8),rng.randrange(2),rng.randrange(2),rng.choice([-1,0,1,3,30000]),rng.choice([-2147483648,-1,0,9,10,19,2147483647]),rng.randrange(17),rng.randrange(1,4),rng.randrange(5),rng.randrange(17),rng.randrange(1,4),rng.choice([-1,0,1,1728000000000]),heap=rng.choice([-1048577,0,1048577])))
 outputs=[invoke(managed,commands),invoke([a.native,'--probe'],commands)];assert len(outputs[0])==len(outputs[1])==len(commands),(len(outputs[0]),len(outputs[1]),len(commands))
 diffs=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(commands,*outputs)) if s!=n]
 actual_root=Path(tempfile.mkdtemp(prefix='actual-',dir=w));time_observations=[]
 for seconds in [-11644473600,-2208988800,0,1000000000,1748736000,253402300799]:
  path=actual_root/('time-'+str(seconds));path.write_bytes(b'time');os.utime(path,ns=(seconds*1000000000,1000000000000000000));encoded=str(path).encode().hex();rows=[invoke(managed+['--time',encoded]),invoke([a.native,'--time',encoded])];time_observations.append(dict(seconds=seconds,observations=rows));assert rows[0]==rows[1],time_observations[-1]
 path=actual_root/'missing';rows=[invoke(managed+['--time',str(path).encode().hex()]),invoke([a.native,'--time',str(path).encode().hex()])];time_observations.append(dict(missing=True,observations=rows));assert rows[0]==rows[1],time_observations[-1]
 filesystem=[]
 for mode in ['basic','readonly','deep']:
  observed=[]
  for index,cmd in enumerate([managed,[a.native]]):
   root=actual_root/(mode+'-'+str(index));(root/'a'/'nested').mkdir(parents=True);(root/'b').mkdir();names=['old','edge','new','default','11111111-1111-1111-1111-111111111111','é漢😀']
   if mode=='readonly':names.append('readonly')
   if mode=='deep':(root/'a'/'nested'/'deeper').mkdir()
   for name in names:
    path=root/'a'/name;path.write_bytes(name.encode());seconds=1748736000+(86400 if name=='new' else 0 if name=='edge' else -86400);os.utime(path,ns=(seconds*1000000000,1000000000000000000))
    if name=='readonly':path.chmod(stat.S_IREAD)
   (root/'root-marker').write_bytes(b'root')
   cutoff=invoke(managed+['--time',str(root/'a'/'edge').encode().hex()])[0];reply=invoke(cmd+['--fs',str(root).encode().hex(),cutoff]);tree=sorted(str(p.relative_to(root)).replace('\\','/')+('/' if p.is_dir() else '') for p in root.rglob('*'));observed.append(dict(trace=[line.replace(str(root),'ROOT') for line in reply],tree=tree))
  filesystem.append(dict(mode=mode,observations=observed));assert observed[0]==observed[1],filesystem[-1]
 deletions=[]
 for readonly in [False,True]:
  observations=[]
  for index,cmd in enumerate([managed,[a.native]]):
   root=actual_root/('delete-'+str(readonly)+'-'+str(index));(root/'nested').mkdir(parents=True);(root/'z-empty').mkdir()
   for name in ['a-file','b-file','nested/c-file','nested/d-file']:
    path=root/name;path.write_bytes(b'file')
    if readonly and name in ['a-file','nested/d-file']:path.chmod(stat.S_IREAD)
   result=invoke(cmd+['--remove-tree',str(root).encode().hex()]);tree=sorted(str(p.relative_to(root)).replace('\\','/')+('/' if p.is_dir() else '') for p in root.rglob('*')) if root.exists() else [];observations.append(dict(result=result,rootExists=root.exists(),tree=tree))
  deletions.append(dict(readonly=readonly,observations=observations));assert observations[0]==observations[1],deletions[-1]
 report=dict(passed=not diffs,cases=len(commands),actualDeletionCases=len(deletions),deletionObservations=deletions,actualTimeCases=len(time_observations),actualFilesystemCases=len(filesystem),actualDirectory=str(actual_root),timeObservations=time_observations,filesystemObservations=filesystem,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,ROOT/'bin/OpenMetaverseTypes.dll']},scope='Unchanged CleanupExpiredFiles/DoCleanExpiredFiles/CleanExpiredFiles with actual SDK UUID parsing, BCL DateTime/TimeSpan arithmetic and controlled filesystem/gather/heap/clock/timer/weak operations. Exact traces and states cover default/reference protection, strict access-time boundary, unchecked cooldown arithmetic and sleep thresholds, failed-delete counts, empty-parent retention, cancellation at every dependency, per-tree catches and escaped sweep failures, weak-only reset/hit counter behavior, timer rearm/cleanup flag ordering and date-range/heap delta behavior. Actual original/native Windows filesystem trees, read-only file and recursive deletion, root-file retention, Unicode names and access-time conversion across epochs/missing sentinel are compared; native unit separately verifies actual weak-only memory/negative preservation. Complete scene graph gatherer, native heap metric integration, all filesystem/permission/timezone/concurrent/GC schedules and production file-backed hosting remain pending; no empty gatherer is silently provided.',differences=diffs[:5]);(w/'flotsam-cache-cleanup-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:3],indent=2)
 print(f'PASS: {len(commands)} original protected cleanup traces and actual SDK/date arithmetic')
if __name__=='__main__':main()
