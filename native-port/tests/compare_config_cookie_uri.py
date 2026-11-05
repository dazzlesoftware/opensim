from pathlib import Path
import argparse,itertools,subprocess,json,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();lab=Path(__file__).resolve().parent;w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
def enc(s):return '~'if s is None else s.encode('utf-16-be','surrogatepass').hex()or'-'
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');g=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(g,k).text=v
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(lab/'config_http_cookies_reference.cs'));cs=w/'Reference.csproj';ET.ElementTree(project).write(cs);subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'-v:q'],check=True,capture_output=True)
schemes=['http','https','ws','wss','ftp','gopher','nntp','mailto','uuid','telnet','ldap','net.tcp','net.pipe','vsmacros','a+b','news'];bodies=['//example.com/a/b','//example.com:81/a/b','//EXAMPLE.com/a?x#f','//localhost/','//127.0.0.1/a','body','/a/b'];headers=['a=b','a=b;Secure','a=b;Path=/','a=b;Path=/a','a=b;Domain=example.com','a=b;Domain=.example.com','a=b;Port','a=b;Port="21"','a=b;Version=1','a=b;Version=1;Port="21"','a=b;Port="81,80,443"','a=b;HttpOnly','a=b;Path=""','a=b;Expires=Wed, 01 Jan 2030 00:00:00 GMT','a=b;Expires=Wed, 01 Jan 2020 00:00:00 GMT','bad']
rows=[];new='N 300 20 4096 -'
for scheme,body,header in itertools.product(schemes,bodies,headers):
 uri=enc(scheme+':'+body);rows.append('|'.join([new,'S '+uri+' '+enc(header),'H '+uri,'G '+uri,'C']))
for one,two,header in itertools.product(schemes,schemes,['a=b;Path=/','a=b;Secure;Path=/','a=b;Version=1;Port="81"']):
 source=enc(one+'://example.com:81/a/b');target=enc(two+'://example.com:81/a/b');rows.append('|'.join([new,'RX '+source+' '+enc(header),'H '+target,'G '+target,'C']))
payload='\n'.join(rows)+'\n';(w/'inputs.txt').write_text(payload);out={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[str(Path(a.native).resolve()),'--probe'])]:
 r=subprocess.run(cmd,input=payload,text=True,capture_output=True);(w/(name+'.txt')).write_text(r.stdout);(w/(name+'-error.txt')).write_text(r.stderr);assert r.returncode==0,(name,r.returncode,r.stderr);out[name]=r.stdout.splitlines();assert len(out[name])==len(rows)
diffs=[dict(row=row,expected=e,actual=n)for row,e,n in zip(rows,out['reference'],out['native'])if e!=n];(w/'all-differences.json').write_text(json.dumps(diffs,indent=2));report={'cases':len(rows),'differences':len(diffs),'examples':diffs[:10],'applicationParityVerified':False,'scope':'Actual CookieContainer across built-in/V1 URI schemes, attributes and cross-scheme retrieval. Protocol transport/OS policies remain incomplete.'};(w/'results.json').write_text(json.dumps(report,indent=2));print(json.dumps({'cases':len(rows),'differences':len(diffs)}));raise SystemExit(bool(diffs))
