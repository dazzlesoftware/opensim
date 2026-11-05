"""Staged actual System.Xml encoder differential; no production CLR dependency."""
import argparse,json,random,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--reference',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
rows=[]
def row(mode,attr,quote,fail,s):rows.append(f'{mode} {attr} {quote} {fail} '+(''.join(f'{c:04x}' for c in s) or '-'))
for c in range(65536):
 for mode in [0,1,2,6,7]:
  for attr in [0,2]:row(mode,attr,34,0,[c])
for attr in [0,1,2]:
 for quote in [0,34,39,65]:
  for s in [[],[60,62,38,34,39,9,10,13],[97,0xd800],[97,0xd800,98],[97,0xdc00],[0xd800,0xdc00],[0xdfff,0xdbff],[0,0xfffe,0xffff]]:
   for mode in [0,1,3,6,7]:
    for fail in range(12):row(mode,attr,quote,fail,s)
for h in [0,65,0xd7ff,0xd800,0xdbff,0xdc00,0xdfff,0xffff]:
 for l in [0,65,0xd7ff,0xd800,0xdbff,0xdc00,0xdfff,0xffff]:
  for mode in [4,5]:
   for fail in range(5):row(mode,2,34,fail,[l,h])
rng=random.Random(776523)
for mode in [6,7]:
 for payload in ['null','-','0061003cd800dc000062']:
  for offset in [-2147483648,-1,0,1,2,4,5,6,2147483647]:
   for count in [-2147483648,-1,0,1,2,4,5,6,2147483647]:rows.append(f'{mode} 2 34 0 {payload} {offset} {count}')
for _ in range(10000):row(rng.choice([0,1,6,7]),rng.randrange(3),rng.choice([34,39,65]),rng.randrange(10),[rng.randrange(65536) for _ in range(rng.randrange(30))])
payload='\n'.join(rows)+'\n'
def run(cmd):
 r=subprocess.run(cmd,input=payload,capture_output=True,text=True);assert r.returncode==0,r.stderr;out=r.stdout.splitlines();assert len(out)==len(rows),(len(out),len(rows));return out
expected=run([a.dotnet,a.reference]);actual=run([a.native,"--probe"]);diff=[{'input':rows[i],'expected':e,'actual':n} for i,(e,n) in enumerate(zip(expected,actual)) if e!=n];report={'cases':len(rows),'differences':len(diff),'examples':diff[:20],'scope':'Native span/raw/entity and array encoder; full XmlTextWriter remains incomplete.'};(a.work/'encoder-results.json').write_text(json.dumps(report,indent=2));assert not diff,report;print(f'PASS {len(rows)} actual XmlTextEncoder traces')
