"""Unchanged XMLRPCRequester algorithm; explicitly controlled I/O/log/XML boundaries."""
from pathlib import Path
import argparse,subprocess,json,xml.etree.ElementTree as ET,itertools,hashlib
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
s=(root/'OpenSim/Framework/WebUtil.cs').read_bytes().decode('utf-8-sig');start=s.index('public static class XMLRPCRequester');i=s.index('{',start)+1;depth=1
while depth:
 if s[i]=='{':depth+=1
 elif s[i]=='}':depth-=1
 i+=1
(w/'Extracted.cs').write_text('using System;using System.Collections;using System.Reflection;using System.Xml.Linq;using log4net;using XmlRpcRequest=Fixture.Request;using XmlRpcResponse=Fixture.Response;using HttpClient=Fixture.Client;using XElement=Fixture.Element;namespace OpenSim.Framework{'+s[start:i]+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [root/'native-port/tests/xml_rpc_requester_reference.cs',w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(f))
ref=ET.SubElement(items,'Reference',Include='XMLRPC');ET.SubElement(ref,'HintPath').text=str(root/'bin/XMLRPC.dll');ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
enc=lambda s:s.encode('utf-16-le','surrogatepass').hex()
def run(cmd,rows):
 r=subprocess.run(cmd,input='\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8');assert r.returncode==0,r.stdout+r.stderr
 result=r.stdout.splitlines();assert len(result)==len(rows),(len(result),len(rows),r.stdout[-1000:]);return result
source=[a.dotnet,str(w/'app/Reference.dll')]
rows=[' '.join(map(str,[level,num,elapsed,kind,'-',mutate])) for level,num,elapsed,kind,mutate in itertools.product([0,3,4,5,6],[-2147483648,-1,0,2147483647],[0,3000,3001],range(6),[0,3,5])]
base=run(source,rows);expanded=list(rows)
for row,out in zip(rows,base):
 events=bytes.fromhex(out.rsplit('|',1)[1]).decode('utf-16-le','surrogatepass').split('\n')
 for stage in dict.fromkeys(events):
  fields=row.split();fields[4]=enc(stage);expanded.append(' '.join(fields))
expected=run(source,expanded);actual=run([a.native],expanded)
for i,(e,n) in enumerate(zip(expected,actual)):
 if e!=n:
  (w/'failure.json').write_text(json.dumps(dict(index=i,input=expanded[i],expected=e,actual=n),indent=2));raise AssertionError(f'mismatch {i}: {expanded[i]}\nexpected {e}\nactual {n}')
(w/'results.json').write_text(json.dumps(dict(passed=True,cases=len(expanded),scope='Unchanged source helper with controlled client/clock/logger/serializer/LINQ boundaries; no global pool or XML formatting parity claim',originalSHA256=hashlib.sha256((root/'OpenSim/Framework/WebUtil.cs').read_bytes()).hexdigest()),indent=2));print(f'XMLRPCRequester {len(expanded)} source orchestration flows passed')
