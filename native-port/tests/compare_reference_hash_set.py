"""Bit-exact raw UTF16 SDK Vector3.TryParse comparison."""
from pathlib import Path
import random
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,itertools
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/'native-port/tests/reference_hash_set_reference.cs'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
meta=subprocess.run([a.dotnet,str(w/'app/Reference.dll'),'--metadata'],capture_output=True,text=True,check=True);runtime=json.loads(meta.stdout);assert runtime['runtime']=='8.0.31',runtime
commands=[]
actors=[0,1,2,3,7]
initials=['','A:0','A:1 A:2 A:3','A:0 A:1 A:2 A:3 R:1',' '.join('A:'+str(i) for i in range(40))]
for initial in initials:
 for operations in itertools.product(['A:0','A:1','A:4','R:0','R:1','R:4','C','X:1','H:1','N','S'],repeat=3):
  commands.append((initial+' E:0 B:0 M:0 G:0 K:1 '+' '.join(operations)+' G:0 B:0 M:0 B:0 M:1 Z:0 M:0 D:0 M:0').strip())
 for action in ['A','R','V','C','M','S']:
  for id in actors:
   commands.append((initial+f' E:0 M:0 F:{action}:{id} G:0 B:0 M:0 Z:0 M:0').strip())
commands += ['E:0 M:0 B:0 A:1 G:0 B:0 M:0','A:1 E:0 M:0 C B:0 G:0 M:0','A:1 A:2 E:0 M:0 M:0 C B:0 G:0 M:0','A:1 E:0 N M:0 G:0 S A:2 E:1 M:1 M:0','A:1 A:2 A:3 E:0 M:0 X:1 A:1 G:0 M:0','A:1 A:2 R:1 R:2 A:3 A:4 E:0 M:0 M:0 M:0','A:1 A:2 E:0 M:0 R:1 G:0 B:0 M:0 G:0','A:1 E:0 M:0 A:1 M:0 R:1 M:0','N A:1 X:1 S A:1','M:0 G:0 B:0 Z:0 D:0 K:1']
rng=random.Random(92831)
for scenario in range(4096):
 flow=[]
 for step in range(80):
  op=rng.choice(['A','A','R','R','H','X','C','E','M','G','B','Z','D','K','N','S','F'])
  if op in ['A','R','H','X']:flow.append(op+':'+str(rng.randrange(16)))
  elif op in ['E','M','G','B','Z','D','K']:flow.append(op+':'+str(rng.randrange(2)))
  elif op=='F':flow.append('F:'+rng.choice(['A','R','V','C','M','S'])+':'+str(rng.randrange(16)))
  else:flow.append(op)
 commands.append(' '.join(flow))
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=300);assert r.returncode==0,r.stderr;outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(input=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n];report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,runtime=runtime,scope='Actual pinned .NET HashSet reference clients with default Object reference equality versus native ReferenceHashSet. Add/remove/contains/clear including null and replaced identities, slot reuse/order/resizing, typed/boxed enumerator current, MoveNext/reset/dispose/copy, mutation invalidation and retained client/collection references, detached collection and foreach callback mutations. No authored expected set outputs. Current project client implementations use default reference equality; arbitrary custom equality/comparers and unused HashSet APIs, CLR GC timing/full application/macOS remain unproved.')
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' HashSet reference differences';print('PASS:',len(commands),'actual CLR reference-set observations')
