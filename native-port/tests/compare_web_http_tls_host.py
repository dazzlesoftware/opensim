import argparse,json,subprocess,xml.etree.ElementTree as ET,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--strict-idn',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[2];w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
source=(root/'OpenSim/Framework/WebUtil.cs').read_bytes().decode('utf-8-sig')
def extract(signature):
 at=source.index(signature);start=source.index('{',at);i=start+1;depth=1
 while depth:
  if source[i]=='{':depth+=1
  if source[i]=='}':depth-=1
  i+=1
 return source[at:i]
methods=''.join(extract(sig) for sig in ['public static void SetupHTTPClients(','public static HttpClient GetNewGlobalHttpClient(','public static HttpClient GetGlobalNoRedirHttpClient('])
(w/'Extracted.cs').write_text('using System;using System.Net;using System.Net.Http;using System.Net.Security;using System.Security.Authentication;using System.Security.Cryptography.X509Certificates;static partial class WebUtil{'+methods+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false',NoWarn='SYSLIB0039').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for file in [root/'native-port/tests/web_http_network_reference.cs',w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(file))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def encode(s):return '-' if s is None else s.encode('utf-16-be',errors='surrogatepass').hex() or '_'
rows=[]
def n(s):rows.append('N '+encode(s))
def h(*values):rows.append('H '+' '.join(encode(s) for s in values))
base=['','a','ABC.Example','a.','a..','.',':','[::1]','[::1]:443','127.0.0.1','a:443','a:0','a:65535','a:65536','a:','a:b','a#x','a?x','u@a','a/b','a,b',' a ','\ta\t','-a','a-','a_b','xn--bcher-kva','b\u00fccher','\u212a','\u017f','\ud800','\udfff','a\0','a\0b']
n(None)
for s in base:
 n(s);h(s)
 for t in ['', '.', '..']:n(s+t)
for size in [1,62,63,64,65,126,127,128,252,253,254,255,256,511,512,1024]:
 for s in ['a'*size,('a.'*size)+'B',('-'*size)+'.Example']:
  n(s);h(s)
for code in range(256):
 c=chr(code)
 for s in [c,'a'+c+'b','a'+c+'.Example',c+'a.', 'a.'+c]:n(s);h(s)
for code in range(256,65536):
 c=chr(code);n(c);n('a'+c+'.Example');h('a'+c+'.Example')
for first in base:
 for second in ['good.example','bad/name','','[::1]:80']:h(first,second);h(second,first)
if a.strict_idn:
 rows=['I '+encode(s) for s in base]
 for code in range(65536):
  for s in [chr(code),'a'+chr(code)+'.Example']:rows.append('I '+encode(s))
 for size in [62,63,64,65,126,127,128,252,253,254,255,256,511,512,1024]:
  for s in ['a'*size,'a.'*size,('-'*size)+'.Example']:rows.append('I '+encode(s))
payload='\n'.join(rows)+'\n';outputs={}
for label,command in [('source',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(command+['--host-probe'],input=payload,capture_output=True,text=True,timeout=120);assert r.returncode==0,(label,r.stdout[-1000:],r.stderr);outputs[label]=r.stdout.splitlines();assert len(outputs[label])==len(rows),(label,len(outputs[label]),len(rows))
differences=[dict(case=i,input=rows[i],source=s,native=t) for i,(s,t) in enumerate(zip(outputs['source'],outputs['native'])) if s!=t]
report=dict(passed=not differences,cases=len(rows),differences=differences,scope='Actual runtime8.0.31 TargetHostNameHelper.NormalizeHostName, HttpRequestHeaders.Host and HttpConnectionPoolManager.ParseHostNameFromHeader; raw UTF16 input/output and complete exception type/HResult/message. Selected default ICU mode; not full SslStream, NLS/invariant or simulator parity.',applicationParityVerified=False)
if a.strict_idn:report['scope']='Actual runtime8.0.31 default ICU IdnMapping.GetAscii used by Unix certificate validation, without TargetHostNameHelper ASCII fallback or trailing-dot trimming. Raw UTF16 outputs and complete exception type/HResult/message; selected inputs and all BMP code units in two contexts, not all possible strings or other globalization modes.'
report['inputSHA256']=hashlib.sha256(payload.encode()).hexdigest()
report['outputSHA256']={label:hashlib.sha256(('\n'.join(lines)+'\n').encode()).hexdigest() for label,lines in outputs.items()}
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert not differences,f'{len(differences)} differences: {w}/results.json';print(f'PASS: {len(rows)} original TLS host/header operations')
