"""Actual encoder state/cache/partial-write/reentry flow differential."""
import argparse,json,random,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--reference',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
rows=[]
def flow(ops,fail=0,reenter=0,callback='E'):rows.append(f'{fail},{reenter};{callback};'+';'.join(ops))
payloads=['~','-','0061','003c0026003e002200270009000a000d','d800','dc00','0061d8000062','d800dc00','ffff0000fffe']
callbacks=['A','B','E','Q,0027','W,003c','N,~','R,d800','V,~,0,0','S,dc00d800']
for payload in payloads:
 for op in ['W','R','N','V','T']:
  command=op+','+payload+(',0,0' if op in ['V','T'] else '')
  for callback in callbacks:
   for trigger in range(8):
    flow(['A',command,'N,0071','B','W,0022','A','W,0027','E','R,0061'],fail=trigger)
    flow(['A',command,'N,0071','B','W,0022','A','W,0027','E','R,0061'],reenter=trigger,callback=callback)
chars=['0000','000a','0022','0027','003c','0061','d7ff','d800','dbff','dc00','dfff','ffff']
rng=random.Random(413379)
def instruction():
 op=rng.choice(['A','B','E','Q','W','R','C','N','S','D','V','T'])
 if op in ['A','B','E']:return op
 if op in ['Q','C']:return op+','+rng.choice(chars)
 if op in ['S','D']:return op+','+rng.choice(chars)+rng.choice(chars)
 payload=rng.choice(payloads) if rng.randrange(3) else ''.join(f'{rng.randrange(65536):04x}' for _ in range(rng.randrange(1,16)))
 if op in ['V','T']:
  size=0 if payload in ['~','-'] else len(payload)//4
  return f'{op},{payload},{rng.choice([-1,0,1,size,size+1,2147483647])},{rng.choice([-1,0,1,size,size+1,2147483647])}'
 return op+','+payload
for _ in range(40000):flow([instruction() for _ in range(rng.randrange(1,28))],rng.randrange(18),rng.randrange(18),instruction())
data='\n'.join(rows)+'\n'
def run(command):
 r=subprocess.run(command,input=data,text=True,capture_output=True);assert r.returncode==0,r.stderr;lines=r.stdout.splitlines();assert len(lines)==len(rows),(len(lines),len(rows));return lines
expected=run([a.dotnet,a.reference]);actual=run([a.native]);differences=[{'input':rows[i],'expected':e,'actual':n} for i,(e,n) in enumerate(zip(expected,actual)) if e!=n];report={'flows':len(rows),'differences':len(differences),'examples':differences[:20],'scope':'Actual internal XmlTextEncoder state, raw UTF16, overload dispatch, structured errors, attribute caching, partial failures and synchronous writer reentry.'};(a.work/'encoder-flow-results.json').write_text(json.dumps(report,indent=2));assert not differences,report;print(f'PASS {len(rows)} actual XmlTextEncoder state/reentry flows')
