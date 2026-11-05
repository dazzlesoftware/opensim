"""Pinned .NET8 Unix source-provider simulation. This is NOT actual Unix execution."""
import argparse,hashlib,itertools,json,random,struct,time,zipfile
from pathlib import Path
from compare_invariant_datetime_context import Peer,compile_oracle,queries
ROOT=Path(__file__).resolve().parents[2]
def tzif(types,transitions,indices,future='',version=2):
 abbreviations=b'STD\0DST\0'
 def block(width,ts,ix):
  header=b'TZif'+(str(version).encode() if version!=1 else b'\0')+bytes(15)+struct.pack('>6I',0,0,0,len(ts),len(types),len(abbreviations))
  return header+b''.join(struct.pack('>q' if width==8 else '>i',v) for v in ts)+bytes(ix)+b''.join(struct.pack('>iBB',offset,dst,4 if dst else 0)for offset,dst in types)+abbreviations
 return block(4,transitions if version in [1,4] else [],indices if version in [1,4] else [])+(block(8,transitions,indices)+b'\n'+future.encode('utf-8','surrogatepass')+b'\n' if version in [2,3] else b'')
def synthetic(quick):
 rows=[]
 for version,offset in itertools.product([1,2,3,4],[-86399,-50401,-50400,-3600,-1,0,1,3600,50400,50401,86399]):rows.append((f'fixed-{version}-{offset}',tzif([(offset,False)],[],[],version=version)))
 footers=['STD596524','STD256204778','STD256204779','STD-256204778','STD10675199:02:48:05','STD-10675199:02:48:05','STD0DST,M3.2.0/596524,M11.1.0/256204778','','UTC0','UTC-1','UTC+1','STD0DST,M3.2.0/2,M11.1.0/2','STD0DST-0:30,M3.2.0/2,M11.1.0/2','STD0DST1,M3.2.0/2,M11.1.0/2','STD0DST,J60/26,J300/-2','STD0DST,0/0,58/24','STD0DST,59,300','<+01>-1<+02>,M3.5.0,M10.5.0','STD0DST,M3.5.0/144,M10.5.0/25:0:0','STD0DST,M3.5.0/--1,M10.5.0/+-1','STD0DST,J2147483649,J300','STD0DST,J1junk,J300junk']
 for base,delta,future,version in itertools.product([-54000,-50400,-1,0,1,1800,50400,54000],[-3600,0,1800,3600],footers,[1,2,3]):rows.append((f'rules-{base}-{delta}-{version}-{future}',tzif([(base,False),(base+delta,True)],[-2000000000,-1000000000,0,1000000000],[0,1,0,1],future,version)))
 rng=random.Random(206018)
 for i in range(60 if quick else 1000):
  base=rng.randrange(-86400,86401);delta=rng.choice([-86400,-43200,-3600,0,1,3600,43200,86400]);trans=sorted(rng.sample(range(-62135596800,253402300799),5));future=rng.choice(footers);rows.append((f'random-{i}',tzif([(base,False),(base+delta,True)],trans,[0,1,0,1,0],future)))
 valid=tzif([(0,False),(3600,True)],[-1000000000,0,1000000000],[0,1,0],'STD0DST,M3.2.0,M11.1.0')
 for end in range(1,len(valid)):rows.append((f'truncated-{end}',valid[:end]))
 for pos in range(len(valid)):
  for value in [0,1,255]:
   altered=bytearray(valid);altered[pos]=value
   # Keep allocation counts small; enormous array-allocation failures are not a safe corpus.
   if any(24<=pos%88<44 and pos%88 not in [27,31,35,39,43] for _ in [0]):continue
   if pos in range(44+24,44+44) and pos not in [71,75,79,83,87]:continue
   rows.append((f'mutated-{pos}-{value}',bytes(altered)))
 for count_offset in [20,24,28]:
  for count in [0,1,2,255,0x20000000,0x40000000,0xffffffff]:
   altered=bytearray(valid);altered[count_offset:count_offset+4]=struct.pack('>I',count);rows.append((f'cursor-wrap-{count_offset}-{count}',bytes(altered)))
 return rows

def read_model(peer,query):
 peer.send(query);model=[]
 while (line:=peer.read())!='END':
  model.append(line)
  if line.startswith('EX:'):break
 return model

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--tzdata-wheel',type=Path);p.add_argument('--quick',action='store_true');p.add_argument('--offsets',action='store_true');p.add_argument('--local-data-fallback',action='store_true');a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True);rows=synthetic(a.quick);wheel=None
 if a.tzdata_wheel:
  wheel=dict(path=str(a.tzdata_wheel.resolve()),sha256=hashlib.sha256(a.tzdata_wheel.read_bytes()).hexdigest())
  with zipfile.ZipFile(a.tzdata_wheel)as z:
   for name in z.namelist():
    data=z.read(name)
    if data.startswith(b'TZif'):rows.append((name,data))
 s=Peer(compile_oracle(a.dotnet,work));n=Peer([a.native,'--probe']);start=time.monotonic();differences=[];models=0;offsets=0;rejects={}
 try:
  metadata=s.ask('META')
  for name,data in rows:
   q=('TZIF_LOCAL ' if a.local_data_fallback else 'TZIF ')+data.hex();expected=read_model(s,q);actual=read_model(n,q)
   if expected[0].startswith('EX:'):
    rejects[expected[0]]=rejects.get(expected[0],0)+1
    if not actual[0].startswith('EX:'):differences.append(dict(name=name,source=expected[:3],native=actual[:3]))
   elif expected!=actual:differences.append(dict(name=name,source=expected[:3],native=actual[:3]))
   elif a.offsets:
    _,base,count=expected[0].split();rules=[list(map(int,line.split()))for line in expected[1:]]
    for query in queries(int(base),rules,a.quick):
     source=s.ask(query);native=n.ask(query);offsets+=1
     if source!=native:differences.append(dict(name=name,query=query,source=source,native=native));break
   models+=1
   if differences:break
  report=dict(passed=not differences and models==len(rows),models=models,plannedModels=len(rows),offsetObservations=offsets,rejectionsBySourceCategory=rejects,completeCorpus=not differences and models==len(rows),metadata=metadata,tzdataWheel=wheel,elapsedSeconds=time.monotonic()-start,localDataFallback=a.local_data_fallback,scope='Pinned .NET8.0.31 unchanged Unix TZif/POSIX ingestion and AdjustmentRule source methods compiled on Windows with explicit framework dependency shims. Complete successful source/native rule models compared, then optional actual Windows BCL shared offset evaluator over those models. Rejected malformed models compare rejection decision only, NOT exact exception category/message. NOT actual Linux/macOS execution or native Unix local-zone environment/file selection.',differences=differences)
  (work/'invariant-datetime-context-unix-results.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],json.dumps(differences[:3]);print(f'PASS: {models} Unix source models and {offsets} offset observations')
 finally:s.close();n.close()
if __name__=='__main__':main()
