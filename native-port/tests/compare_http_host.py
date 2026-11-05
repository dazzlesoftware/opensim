"""Actual BCL CheckHostName and unchanged OSHHTPHost state/DNS/comparison.

Host constructor text uses the current native UTF8 projection; classification
accepts raw UTF16. Original app invariant culture and DNS results are controlled.
"""
from pathlib import Path
import argparse,subprocess,json,hashlib,itertools
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--classification-only',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
def extract(s,signature):
 start=s.index(signature);i=s.index('{',start)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
source='OpenSim/Framework/GridInfo.cs';text=(root/source).read_bytes().decode('utf-8-sig');generated=w/'Host.cs';generated.write_text('using System;using System.Net;namespace OpenSim.Framework{'+extract(text,'public enum OSHTTPURIFlags')+extract(text,'public struct OSHHTPHost')+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [generated,root/'native-port/tests/http_host_reference.cs']:ET.SubElement(items,'Compile',Include=str(f))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
names=[None,'','host','HOST.','_host','host_','-host','host-','a..b','a/b','a\\b','a:80','a#b','a?b','a\0','a\u00a0b','a\u200db','\u00e9.example','\u03a3.example','\U0001f600.example','[::1]','::1','[fe80::1%12]','fe80::1%25Eth0','[::ffff:127.0.0.1]','::ffff:127.0.0.1','[::/0]','::/128','[::1','::1]']
for count in range(1,5):
 for pieces in itertools.product(['0','1','255','256','0177','0x7f'],repeat=count):names.append('.'.join(pieces))
for n in [1,2,29,30,58,59,60,63,64,127,128,254,255,256,1023]:
 for c in ['a','\u00e9','\u03a3','\U0001f600']:names.append(c*n+'.example')
for n in [127,128,1023,16382,16383,16384]:names.append('a.'*n+'a')
names=list(dict.fromkeys(names));classified=list(names)
for cp in range(65536):
 c=chr(cp);classified.extend([c,c+'.example','ab'+c+'_cd.example'])
rows=[(6,s,None) for s in classified]
if not a.classification_only:
 values=list(names)
 for prefix in ['','http://','https://','HTTP://','httpXxx','httpsXxx','http\u00e9x','http\U0001f600','httpab\U0001f600','h\u00adttp','http\0ab']:
  for host in ['host','bad.invalid','\u00c9XAMPLE.test','\u03a3igma.example','a.'*128+'a']:
   for port in ['',':0',':65535',':65536',':-0',':+80',':-1',': 80 ',':\u00a080',':80\0',':80\0\0',':80/']:
    values.append(prefix+host+port)
 values=list(dict.fromkeys(values));rows.extend((mode,s,None) for mode in [0,1,2,4,5] for s in values)
 compare=['a-b.example','ab.example','a_b.example','a.b.example','\u00e4.example','ae.example','\u00e9.example','z.example','\u00e6.example','a.example','1.example','10.example','2.example','i.example','\u0131.example','i\u0307.example','\u03c3.example','\u03c2.example']
 rows.extend((3,x,y) for x in compare for y in compare)
def h(s):return 'N' if s is None else s.encode('utf-16le','surrogatepass').hex() or '.'
commands=[f'{mode} {h(x)} {h(y)}' for mode,x,y in rows];data='\n'.join(commands)+'\n';outputs=[]
for cmd in [[a.dotnet,str(w/'app/Reference.dll')],[a.native]]:
 r=subprocess.run(cmd,input=data,capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,r.stdout[-1000:]+r.stderr;out=r.stdout.splitlines();assert len(out)==len(rows),(len(out),len(rows));outputs.append(out)
diffs=[dict(index=i,input=rows[i],source=bytes.fromhex(s).decode(errors="replace"),native=bytes.fromhex(n).decode(errors="replace")) for i,(s,n) in enumerate(zip(*outputs)) if s!=n]
report=dict(passed=not diffs,cases=len(rows),classificationCases=len(classified),hostCases=len(rows)-len(classified),differenceCount=len(diffs),differences=diffs[:12],scope=__doc__,applicationParityVerified=False,sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in [source,'native-port/tests/http_host_reference.cs']})
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],str(len(diffs))+' host differences; see '+str(w/'results.json');print(f'PASS: {len(rows)} hostname and host source cases')
