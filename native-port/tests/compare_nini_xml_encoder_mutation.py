import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--reference',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
payloads=[[97,60,98],[97,38,98],[97,34,98],[97,39,98],[97,10,98],[97,0,98],[97,0xd800],[97,0xdc00],[97,0xd800,0xdc00],[97,0xd800,98],[60,97,38,98,62],[97,98,60,99,0xd800,0xdc00]]
rows=[]
for mode in [6,7,8]:
 for attr in [0,1,2]:
  for quote in [34,39]:
   for payload in payloads:
    for at in range(1,9):
     for position in range(len(payload)):
      for unit in [0,38,60,34,0xd800,0xdc00]:
       for fail in [0,at]:rows.append(f'{mode} {attr} {quote} {fail} '+''.join(f'{c:04x}' for c in payload)+f' 0 {len(payload)} {at} {position} {unit}')
data='\n'.join(rows)+'\n'
def run(command):
 r=subprocess.run(command,input=data,text=True,capture_output=True);assert r.returncode==0,r.stderr;lines=r.stdout.splitlines();assert len(lines)==len(rows),(len(lines),len(rows));return lines
expected=run([a.dotnet,a.reference]);actual=run([a.native,'--probe']);differences=[{'input':rows[i],'expected':x,'actual':y} for i,(x,y) in enumerate(zip(expected,actual)) if x!=y];report={'cases':len(rows),'differences':len(differences),'examples':differences[:20],'scope':'Actual System.Xml encoder borrowed-span and array mutation during synchronous TextWriter callback, including cached pre-call code unit and post-call surrogate reads.'};(a.work/'mutation-results.json').write_text(json.dumps(report,indent=2));print(len(rows),'actual mutation cases;',len(differences),'differences');assert not differences,report
