import argparse,json,itertools,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
source=(root/'OpenSim/Framework/WebUtil.cs').read_bytes().decode('utf-8-sig')
def extract(signature):
 at=source.index(signature);start=source.index('{',at);depth=1;i=start+1
 while depth:
  if source[i]=='{':depth+=1
  if source[i]=='}':depth-=1
  i+=1
 return source[at:i]
methods='\n'.join(extract(s) for s in ['public static void SetupHTTPClients(','public static HttpClient GetNewGlobalHttpClient(','public static HttpClient GetGlobalNoRedirHttpClient('])
(w/'Extracted.cs').write_text('using System;using System.Net;using System.Net.Http;using System.Net.Security;using System.Security.Authentication;using System.Security.Cryptography.X509Certificates;static partial class WebUtil{'+methods+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false',NoWarn='SYSLIB0039').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for path in [root/'native-port/tests/web_http_clients_reference.cs',w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(path))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
rows=[];next_client=0
def get(kind,timeout,buffer):
 global next_client
 rows.append(f'G {kind} {timeout} {buffer}')
 if buffer>0:next_client+=1
for kind,t,b in itertools.product([0,1],[-2147483648,-1,0,1,2147483647],[-2147483648,-1,0,1,262144000,2147483647]):rows.append(f'G {kind} {t} {b}')
for flags,proxy,maximum in itertools.product(range(4),range(3),[-2147483648,-1,0,1,32,2147483647]):
 rows.append(f'S {flags} {proxy} {maximum}')
 if maximum>0:
  for error in list(range(16))+[-2147483648,-1,2147483647]:rows.append(f'P {error}')
  for kind,t,b in itertools.product([0,1],[-2147483648,-1,0,1,2147483647],[-2147483648,-1,0,1,262144000,2147483647]):get(kind,t,b)
  rows.extend([f'D {next_client}',f'U {next_client}'])
 else:rows.extend(['P 0','P 7'])
payload='\n'.join(rows)+'\n';outputs={}
for label,command in [('source',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,(label,r.stdout[-1000:],r.stderr);outputs[label]=r.stdout.splitlines();assert len(outputs[label])==len(rows),(label,len(outputs[label]),len(rows))
diffs=[dict(index=i,input=rows[i],source=s,native=n) for i,(s,n) in enumerate(zip(outputs['source'],outputs['native'])) if s!=n]
report=dict(passed=not diffs,cases=len(rows),differences=diffs[:30],differenceCount=len(diffs),scope='Unchanged original WebUtil.SetupHTTPClients/GetNewGlobalHttpClient/GetGlobalNoRedirHttpClient with actual .NET8.0.31 handler/client settings, identities, replacements, null/invalid arguments, timeout/buffer rules, disposal and certificate callback bits. Live I/O and native TLS tested separately.',applicationParityVerified=False)
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,f'{len(diffs)} differences; see {w}/results.json';print(f'PASS: {len(rows)} original global client configuration cases')
