"""Original query getter/dictionary builder with actual Uri and HttpUtility."""
import argparse,hashlib,json,subprocess,urllib.parse,xml.etree.ElementTree as ET
from pathlib import Path
from compare_presence_endpoint import ROOT,extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 low=ROOT/'OpenSim/Framework/Servers/HttpServer/OSHttpServer/HttpRequest.cs';high=ROOT/'OpenSim/Framework/Servers/HttpServer/OSHttpRequest.cs';getter=extract(low.read_text(encoding='utf-8-sig'),r'public NameValueCollection QueryString');builder=extract(high.read_text(encoding='utf-8-sig'),r'private void BuildQueryDictionary\(')
 wrapper=w/'Program.cs';wrapper.write_text('using System;using System.Text;using System.Web;using System.Collections.Generic;using System.Collections.Specialized;class Reference{Uri m_uri;NameValueCollection m_queryString;Dictionary<string,string> _queryAsDictionay;HashSet<string> m_queryFlags;Reference m_request;'+getter+builder+'''static void Main(){string line;while((line=Console.ReadLine())!=null){try{var r=new Reference();r.m_request=r;r.m_uri=new Uri("http://localhost"+Encoding.UTF8.GetString(Convert.FromHexString(line)));r.BuildQueryDictionary();r._queryAsDictionay.TryGetValue("auth",out var auth);Console.WriteLine(auth==null?"-":Convert.ToHexString(Encoding.UTF8.GetBytes(auth)).ToLowerInvariant());}catch{Console.WriteLine("EX");}}}}''',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(wrapper));path=w/'Query.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 queries=['','?','?auth','?auth=','?auth=x','?Auth=x','?AUTH=x&auth=y','?auth=x&Auth=y','?auth=x&auth=y','?auth=a+b%2Bc%20d','?auth=a=b=c','?auth=x&&auth=y&','?other=x&auth=y','?auth=x#ignored&auth=y','?auth=%u0041','?auth=%zz%2','?auth=%00%ff%c0%af','?auth=試験','?auth=x\r\ny\tz','?a\ruth=x','?auth=last ']
 queries += ['?auth=x'+chr(c) for c in [*range(33),0x85,0xa0,0x1680,0x2000,0x2028,0x202f,0x3000]]
 queries += ['?auth='+urllib.parse.quote(value,safe='') for value in ['\0','試験','😀','a&b=c','\r\n','\t','%u0000','%20','a+b','\ufffd']]
 for key in ['auth','Auth','AUTH','aUtH','a%75th','%61uth','%u0061uth','auth[]',' auth','auth ','au\0th','a\u00aduth','ａｕｔｈ','áuth']:
  queries += ['?'+key+'=first&auth=second','?auth=first&'+key+'=second']
 for pos in range(4):
  for c in range(0x10000):
   if 0xd800<=c<=0xdfff:continue
   key='auth'[:pos]+chr(c)+'auth'[pos+1:];queries.append('?'+urllib.parse.quote(key,safe='')+'=one&auth=two')
 commands=[('/agent/00000000-0000-0000-0000-000000000007/00000000-0000-0000-0000-000000000008/'+q).encode().hex() for q in queries];rows=[]
 for command in [[a.dotnet,str(w/'app/Query.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
  if r.returncode:raise RuntimeError(r.stderr)
  rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands)
 diffs=[{'query':q,'source':x,'native':y} for q,x,y in zip(queries,*rows) if x!=y];(w/'agent-delete-query-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(commands),'sourceHashes':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [low,high]},'scope':'Auth lookup only, complete original getter/dictionary builder, actual .NET Uri and ParseQueryString; general query dictionary/URI/host parity remains pending','differences':diffs[:8]},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:8],ensure_ascii=False,indent=2)+' count='+str(len(diffs)))
 print(f'PASS: {len(commands)} agent DELETE auth query observations')
if __name__=='__main__':main()
