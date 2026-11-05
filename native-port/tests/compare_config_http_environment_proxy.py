import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):return '~' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '-'
def run(cmd,rows=None):
 p=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',text=True,capture_output=True,encoding='utf-8',timeout=300);assert p.returncode==0,(p.returncode,p.stderr[-2000:]);return p.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--full',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(ROOT/'native-port/tests/config_http_environment_proxy_reference.cs'));cs=w/'Reference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'-v','quiet'])
 rows=[]
 values=['socks4://proxy','socks4a://proxy:1080','socks5://u:p@proxy:1080','socks5://[::1]:1080','SOCKS5://proxy','socks5://:@proxy','socks5://:p@proxy',None,'',' ','host','host:','host:0','host:80junk','host:65535junk','host:65536','host:+1','host:-1','host:1.2','host:00080','host/path','host?query','host#fragment','host:80/path','http://host','HTTP://host','https://host','https://host:80','ftp://host','user:pass@host',':@host',':p@host','u@host','u@x@host','u%40v:p%3Aq@host','domain\\user:password@host','[::1]','[::1]:8080','::1','127.1:8080','host:1'+chr(0x661)+'x','h'+chr(0xe9)+'.test']
 rows+=['U '+enc(v) for v in values]
 creds=[None,'',' ',':','%3A','user','user:password','domain\\user:password','u%3Aname:p%3Aa','%00:%00','a:b:c','\\user:p','domain\\:p','user:','%GG:pass','u'+chr(0xd800)+':p']
 rows+=['C '+enc(v) for v in creds]
 uris=[None,'','relative','HTTP://X:80/a#f','http://x/a#other','http://u:p@x/a','http://different:p@x/a','http://x:81/a','https://x/a','http://x/a?q','http://x/a%20b','http://x/a b','http://x/%2f','http://x/%2F','http://x/%GG','http://x/%25GG','http://x/'+chr(0xe9),'http://x/%C3%A9','http://[fe80::1%one]/','http://[fe80::1%two]/']
 rows += ['Q '+enc(x)+' '+enc(y) for x,y in itertools.product(uris,repeat=2)]
 proxies=[None,'http://u:p@x/','http://v:q@x/','http://:@x/','http://x/','https://u:p@x/','http://domain%5Cu:p@x/','http://x:81/a']
 rows += ['K '+enc(x)+' '+enc(y)+' '+enc(z) for x,y,z in itertools.product(proxies,proxies,[None,'http://x/','http://different@x/#f','https://x/','http://x:81/a','relative'])]
 bypass=[None,'','.example.com','example.com','*','*.example.com','example.com:80','target',' target , , .example.com ','.','127.0.0.1','[::1]',chr(0x2000)+'target'+chr(0x2000)]
 requests=[None,'relative','http://target/','http://example.com/','http://sub.example.com/','https://example.com/','http://127.1/','http://[::1]/']
 rows += ['P '+enc(x)+' '+enc(y)+' '+enc(b)+' '+enc(z) for x,y,b,z in itertools.product(['proxy:8080',None],['https://other:443',None],bypass,requests)]
 envs=[[None]*9]
 for index in range(9):
  for value in ['', 'http://user:pass@lower:8080', 'upper:8081', 'socks5://user:pass@proxy:1080', 'invalid:65536']:
   env=[None]*9;env[index]=value;envs.append(env)
 envs += [['lower:80','upper:81','securelower:82','secureupper:83','alllower:84','allupper:85',None,None,cgi]for cgi in [None,'','CGI/1.1']]
 envs += [[None,None,None,None,'user:pass@alllower:84','allupper:85',bypass,None,None]for bypass in [None,'target','.example.com','*']]
 rows += ['E '+' '.join(enc(v)for v in env)+' '+enc(request)for env,request in itertools.product(envs,['http://target/','https://target/','ws://target/','wss://target/'])]
 if a.full:
  for c in map(chr,range(65536)):
   rows.append('G '+enc(c));rows.append('C '+enc('u'+c+':p'))
   for value in ['h'+c+'.test','host:1'+c+'suffix','u'+c+':p@host','host/a'+c+'b','host#a'+c+'b']:rows.append('U '+enc(value))
   rows.append('U '+enc('http'+c+'://host'));rows.append('U '+enc('http'+c+'s://host'))
  rng=random.Random(8073);parts=['a','%20','%3A','%5C','%40','%GG',chr(0xe9),chr(0xd800),chr(0xdc00),'@',':','/','?','#',' ','.','[',']']
  for _ in range(10000):
   value=''.join(rng.choice(parts) for _ in range(rng.randrange(15)));rows.append('U '+enc(value));rows.append('C '+enc(value))
 source=run([a.dotnet,str(w/'app/Reference.dll')],rows);native=run([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows),(len(source),len(native),len(rows));diff=[dict(index=i,row=r,source=s,native=n) for i,(r,s,n) in enumerate(zip(rows,source,native)) if s!=n]
 for name,data in [('inputs.txt',rows),('actual-bcl.txt',source),('native.txt',native)]:(w/name).write_text('\n'.join(data)+'\n',encoding='utf-8')
 result=dict(cases=len(rows),passed=not diff,differences=diff,hashes={name:hashlib.sha256((w/name).read_bytes()).hexdigest() for name in ['inputs.txt','actual-bcl.txt','native.txt']});(w/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(len(rows),len(diff));print(diff[:3]);assert not diff
if __name__=='__main__':main()
