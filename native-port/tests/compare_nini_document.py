"""Actual bundled Nini document/section/item/collection public operation oracle."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def call(cmd,rows=None):
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,(r.returncode,r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def enc(s):return '-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/nini_document_reference.cs'));r=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(r,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'NiniDocumentReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[]
 def add(ops,ft=0,point=0,nth=1):rows.append(f'{ft} {point} {nth} {ops}')
 texts=['','\n',';initial\n','[a]\nx=first\nx=last\n','[a]\nx=1\n[b]\ny=2\n[a]\nz=3\n','[ ]\nx=\n','[a]\n#comment0=value\n; comment\n','[a]\n;comment\n#comment0=value\n','[a]\nx="quoted" ; comment\n','[a]\nx:python\ny=mysql\nnoassign\n','[a]\nx=one\\\n two\n','x=before\n','[a]\nx=1\n[bad','[a]\nx="bad\n','[a]\nx=foo\rbar\n','[a]\nx=a\u00a0\n','[a]\nx=\ud800\n','[a]\n#hash\n']
 for text,ft,point,nth in itertools.product(texts,[-1,0,1,2,3,4,99],range(6),[1,2,5]):add(f'load:{enc(text)},save:0,sync',ft,point,nth)
 for first,second,ft in itertools.product(texts[:10],texts,range(5)):add(f'load:{enc(first)},load:{enc(second)},save:0',ft)
 for text,ft in itertools.product(texts,range(5)):add(f'ir:{enc(text)},save:0',ft)
 payloads=[None,'','x','X','#comment0','\ud800','\x00','a\nb']
 for name,key,value,comment in itertools.product(payloads,payloads,[None,'','value'],[None,'','comment']):
  add(f'new:{enc(name)}:{enc(comment)},add:0,set:{enc(key)}:{enc(value)}:{enc(comment)},contains:{enc(key)},get:{enc(key)},keys,empty:{enc(comment)},set:{enc(key)}:{enc(value)}:-,rm:{enc(key)},save:0')
 for name,value,comment,kind in itertools.product(payloads,payloads,[None,'','comment'],[-1,0,1,2,99]):add(f'makeitem:{enc(name)}:{enc(value)}:{kind}:{enc(comment)},iv:{enc(comment)},ic:{enc(value)},it:99')
 for kind,rank,lower,length,index,empty in itertools.product(['-',0,1,2,3],[1,2],[0,1],[0,1,3],[-1,0,1,4],[False,True]):
  add(('' if empty else 'new:6100:-,add:0,new:6200:-,add:1,')+f'copy:{kind}:{rank}:{lower}:{length}:{index}')
 scripts=['new:6100:-,add:0,add:0,new:6100:.,add:1,find:-,remove:-,remove:7800,sel:-1,sel:1,sync','enum,current,next,current,new:6100:-,add:0,current,next,current,reset,next,current,remove:6100,current,next,current','new:6100:-,add:0,enum,next,current,new:6200:-,add:1,next,current,remove:6100,current,reset,next,current','new:6100:-,add:0,empty:-,item:0,it:1,iv:7800,ic:7900,keys,save:0,it:0,save:0,it:99,save:0,rm:230063006f006d006d0065006e0074003000,iv:7a00,save:0','new:6100:-,add:0,set:7800:7900:-,item:0,rm:7800,iv:7a00,set:7800:6100:.,save:0','load:-,ir:-,save-null,save:0']
 for script,ft,point,nth in itertools.product(scripts,range(5),range(6),[1,2,3]):add(script,ft,point,nth)
 base=enc(';initial\n[a]\nx=1\ny=2\n[b]\nz=3\n')
 for mutation,ft,point,nth in itertools.product(range(10),range(5),[0,1,4,5],[1,2,3,8]):add(f'load:{base},save:{mutation},save:0',ft,point,nth)
 rng=random.Random(1457)
 for _ in range(1000):
  ops=['new:6100:-','add:0']
  for _ in range(12):
   key=enc(rng.choice(payloads));value=enc(rng.choice(payloads));ops.append(rng.choice([f'set:{key}:{value}:-',f'get:{key}',f'contains:{key}',f'rm:{key}',f'empty:{value}','keys','identity','save:0']))
  add(','.join(ops),rng.randrange(5),rng.randrange(6),rng.randrange(1,5))
 finalizers=[call([a.dotnet,str(w/'app/NiniDocumentReference.dll'),'--finalizer']),call([a.native,'--finalizer'])];assert finalizers==[['2:1'],['2:1']],finalizers
 outputs=[call([a.dotnet,str(w/'app/NiniDocumentReference.dll')],rows),call([a.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows)
 diff=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(rows,*outputs)) if s!=n]
 report=dict(passed=not diff,cases=len(rows),forcedFinalizerCloses=finalizers,niniSha256=hashlib.sha256((ROOT/'bin/Nini.dll').read_bytes()).hexdigest(),scope='Actual bundled IniDocument/IniSection/IniItem/IniSectionCollection; ordered state, repeated partial Load, exact reader/writer IO and faults, live enumerator, unordered CopyTo contents/errors, raw UTF16. Save callback mutation/reentrancy. Per-row no-GC region prevents hidden finalizers contaminating active traces; explicit native retirement API separates arbitrary CLR finalization timing. Required file/byte-stream provider encoding and actual Unix execution separate.',differences=diff[:5]);(w/'nini-document-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:1]);print('PASS:',len(rows),'actual Nini document public/state/IO cases')
if __name__=='__main__':main()
