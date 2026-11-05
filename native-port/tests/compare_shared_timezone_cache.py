import argparse,hashlib,json,subprocess,re,itertools,os,datetime,shutil,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
enc=lambda s:'-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
def call(cmd,rows=None):
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=240)
 assert r.returncode==0,(cmd,r.returncode,r.stdout[-3000:],r.stderr[-3000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work=a.work.resolve();a.work.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/shared_timezone_cache_reference.cs'));project_file=a.work/'SharedTimezoneCacheReference.csproj';ET.ElementTree(project).write(project_file,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(project_file),'-o',str(a.work/'app'),'--nologo','-v','quiet'])
 oracle=str(a.work/'app/SharedTimezoneCacheReference.dll');metadata=call([a.dotnet,oracle,'--meta'])[0];assert metadata.split('|')[0]=='8.0.31',metadata
 rows=[]
 def add(*ops):rows.append(','.join(ops))
 names=['UTC','utc','Etc/UTC','GMT','America/New_York','america/new_york','Eastern Standard Time','eastern standard time','America/Los_Angeles','Pacific Standard Time','Europe/Dublin','Africa/Casablanca','Australia/Lord_Howe','Pacific/Apia']
 for name in names:
  add('C','F:0:'+enc(name),'I:0','F:1:'+enc(name),'R:0:1','C','F:2:'+enc(name),'R:0:2','I:2')
 for x,y in [('UTC','utc'),('UTC','Etc/UTC'),('America/New_York','america/new_york'),('Eastern Standard Time','eastern standard time'),('America/New_York','Eastern Standard Time')]:
  for first,second in [(x,y),(y,x)]:add('C','F:0:'+enc(first),'F:1:'+enc(second),'F:2:'+enc(first),'F:3:'+enc(second),'R:0:2','R:1:3','R:0:1','I:0','I:1')
 ticks=[0,1,621355968000000000,638409600000000000,638566848000000000,3155378975999999998,3155378975999999999]
 for t in ticks:
  add('C','L:0','L:1','R:0:1','U:-1:'+str(t),'Q:0:'+str(t),'C','L:2','R:0:2','Q:0:'+str(t),'Q:2:'+str(t))
  add('C','N:0','L:0:0','L:1:0','R:0:1','C','L:2:0','R:0:2','PC:0','L:3:0','R:0:3','Q:0:'+str(t),'Q:3:'+str(t))
 add('C','F:0:'+enc('America/New_York'),'N:0','PC:0','F:1:'+enc('America/New_York'),'R:0:1')
 for first,second in [('America/New_York','Etc/UTC'),('Etc/UTC','America/New_York')]:
  e=lambda v:'E:'+enc('TZ')+':'+enc(v)
  add(e(first),'C','L:0',e(second),'U:-1:638409600000000000','L:1','R:0:1','C','L:2','R:0:2','Q:0:638409600000000000','Q:2:638409600000000000')
  add(e(first),'C','U:-1:638409600000000000',e(second),'L:0','D:0:1719792000','C','L:1','D:1:1719792000')
  add(e(first),'C','N:0','L:0:0',e(second),'L:1','C','L:2:0','R:0:2','PC:0','L:3:0','R:0:3','Q:0:638409600000000000','Q:1:638409600000000000','Q:3:638409600000000000')
 if os.name!='nt':
  left=a.work/'dir-left';right=a.work/'dir-right';left.mkdir(exist_ok=True);right.mkdir(exist_ok=True);shutil.copyfile('/usr/share/zoneinfo/America/New_York',left/'Owned');shutil.copyfile('/usr/share/zoneinfo/Etc/UTC',right/'Owned')
  e=lambda v:'E:'+enc('TZDIR')+':'+enc(str(v))
  add(e(left),'C','F:0:'+enc('Owned'),'D:0:1719792000',e(right),'F:1:'+enc('Owned'),'R:0:1','D:1:1719792000','C','F:2:'+enc('Owned'),'R:0:2','D:2:1719792000','D:0:1719792000')
  add(e(left),'C','F:0:'+enc('owned'),'F:1:'+enc('Owned'),'F:2:'+enc('OWNED'),'R:1:2','C','F:3:'+enc('OWNED'))
 # Dense actual UTC daylight/offset/ambiguity samples; integer ticks, no FP.
 origin=datetime.datetime(1,1,1);start=datetime.datetime(2024,1,1);samples=set(ticks)
 for hour in range(366*24):
  t=((start+datetime.timedelta(hours=hour))-origin).days*864000000000+(hour%24)*36000000000
  samples.update([t-1,t,t+1])
 for year in [1,2,1883,1900,1918,1945,1970,2006,2007,2011,2023,2025,2026,2038,9998,9999]:
  for month,day in itertools.product([1,3,4,7,10,11,12],[1,15,28]):samples.add((datetime.datetime(year,month,day)-origin).days*864000000000)
 samples=sorted(t for t in samples if 0<=t<=3155378975999999999)
 selected=[None] if os.name=='nt' else ['America/New_York','Europe/Dublin','Africa/Casablanca','Australia/Lord_Howe','Pacific/Apia','Pacific/Chatham','Etc/UTC']
 for name in selected:
  prefix=[] if name is None else ['E:'+enc('TZDIR')+':-','E:'+enc('TZ')+':'+enc(name)]
  for at in range(0,len(samples),192):add(*prefix,'C','L:0',*['Q:0:'+str(t) for t in samples[at:at+192]])
 reference=[a.dotnet,oracle];source=call(reference+['--probe'],rows);native=call([a.native,'--probe'],rows)
 assert len(source)==len(native)==len(rows),(len(source),len(native),len(rows))
 normal=lambda s:re.sub(r'EX:[^|]*','EX',s)
 diff=[dict(index=i,row=row,source=s,native=n) for i,(row,s,n) in enumerate(zip(rows,source,native)) if normal(s)!=normal(n)]
 report=dict(executedPlatform=os.name,scope='Actual BCL TimeZoneInfo/private CachedData epoch and reference identity, retained objects, default and independent owners; full UTC offset/daylight/ambiguity query samples. Named errors compared as failure only (source metadata retained); full named loader math unchanged.',denseUtcQueries=len(samples)*len(selected),cases=len(rows),passed=not diff,runtime=metadata,nativeSha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),differences=diff,observations=[dict(row=r,source=s,native=n) for r,s,n in zip(rows,source,native)])
 (a.work/'shared-timezone-cache-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(dict(cases=len(rows),failures=len(diff))));assert not diff,json.dumps(diff[:3],indent=2)
if __name__=='__main__':main()
