import argparse,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return '~'
 raw=s.encode('utf-16-le','surrogatepass');return ''.join(f'{raw[i]|raw[i+1]<<8:04x}' for i in range(0,len(raw),2)) or '-'
def call(cmd,rows=None):
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,(r.returncode,r.stdout[-1000:],r.stderr[-1000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work',type=Path,required=True);p.add_argument('--reuse-source',action='store_true');p.add_argument('--native-hash',action='store_true');p.add_argument('--provider-checks',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);rows=[]
 def add(*ops):rows.append('|'.join(ops))
 def new(cap=300,per=20,size=4096,fqdn=''):return f'N {cap} {per} {size} {enc(fqdn)}'
 uris=['http://example.com/a/b','http://a.example.com/a/b','http://localhost/','http://127.0.0.1/a']
 values=['','a','a=','=b',',',';','a=b','a=b, c=d','a=b;c=d','a=b;Secure','a=b;HttpOnly;Discard','a=b;Path=/a','a=b;Path=/other','a=b;Path="/a"','a=b;Domain=example.com','a=b;Domain=.example.com','a=b;Domain="example.com"','a=b;Domain=com','a=b;Domain=bad.com','a=b;Domain=localhost','a=b;Version=0','a=b;Version=1','a=b;Version=2','a=b;Version=-1','a=b;Version="1"','a=b;Port','a=b;Port="80,81"','a=b;Port="81"','a=b;Port=""','a=b;Port=80','a=b;Max-Age=0','a=b;Max-Age=-1','a=b;Max-Age=60','a=b;Max-Age=2147483647','a=b;Max-Age=2147483648','a=b;Expires=Sat, 01 Jan 2000 00:00:00 GMT','a=b;Expires=Fri, 31 Dec 9999 23:59:59 GMT','a=b;Expires=bad','a=b;Comment="x;y"','a=b;Comment=x,y','a=b;Version=1;Path=/a;Domain=example.com','a=b;Expires=bad;Max-Age=60','a=b;Max-Age=60;Expires=bad','a=b, invalid$=v, c=d','a=b, $bad=v, c=d','a=b;Version=-1, c=d']
 for uri,value,mode in itertools.product(uris,values,['S','RX']):add(new(),f'{mode} {enc(uri)} {enc(value)}','C',f'H {enc(uri)}',f'G {enc(uri)}')
 for uri,header in itertools.product(uris,[None]):add(new(),f'S {enc(uri)} {enc(header)}','C')
 add(new(),'S ~ '+enc('a=b'));add(new(),'H ~')
 for cap,per,size in [(0,20,4096),(-1,20,4096),(10,20,4096),(20,0,4096),(20,-1,4096),(20,20,0),(20,20,-1),(1,1,1)]:add(new(cap,per,size))
 for size in [4095,4096,4097]:add(new(),f'S {enc(uris[0])} {enc("a="+"x"*size)}','C')
 for first,second in itertools.product(['a=first','A=second','a=third;Version=1','a=third;Version=2','a=gone;Max-Age=0','a=first;Path="/a"','a=last;Path=/a'],repeat=2):add(new(),f'S {enc(uris[0])} {enc(first)}',f'S {enc(uris[0])} {enc(second)}','C',f'H {enc(uris[0])}',f'G {enc(uris[0])}')
 for per,paths in itertools.product([1,2,3,16,17,20],[False,True]):
  ops=[new(300,per,4096)]
  for i in range(24):ops.append(f'S {enc(uris[0])} {enc("c"+str(i)+"=v"+(";Path=/p"+str(i) if paths else ""))}')
  ops+=['C']+[f'H {enc("http://example.com/p"+str(i))}' for i in range(24)];add(*ops)
 for cap in [5,20,100,300]:
  ops=[new(cap,min(20,cap),4096)]
  for i in range(cap+5):ops.append(f'S {enc("http://d"+str(i)+".example.com/")} {enc("c=v")}')
  ops+=['C']+[f'H {enc("http://d"+str(i)+".example.com/")}' for i in range(cap+5)];add(*ops)
 # Every raw UTF16 unit in independently encoded cookie names and values.
 for c in map(chr,range(65536)):
  for header in ['x'+c+'y=v','x=v'+c+'z']:
   add(new(),f'S {enc(uris[0])} {enc(header)}','C',f'H {enc(uris[0])}',f'G {enc(uris[0])}')
 for domain in ['.','..','','com','.com','example.com','.example.com','..example.com','a.example.com','.a.example.com','EXAMPLE.COM','é.com','127.0.0.1','[::1]']:
  for version in [0,1,2]:
   header='x=v;Domain='+domain+';Version='+str(version)
   add(new(),f'S {enc(uris[0])} {enc(header)}','C',f'H {enc(uris[0])}')
 for date in ['Sat, 01 Jan 2000 00:00:00 GMT','Fri, 31 Dec 9999 23:59:59 GMT','2000-01-01','2000-01-01T00:00:00Z','2000-01-01T00:00:00+14:00','9999-12-31T23:59:59Z','0001-01-01T00:00:00Z','Monday, 02 Jan 2024','12:34','January 2','1/2/49','1/2/50','garbage']:
  for tail in ['', ';Max-Age=60']:
   add(new(),f'S {enc(uris[0])} {enc("a=b;Expires="+date+tail)}','C',f'H {enc(uris[0])}')
 for header in ['a=b, z='+('x'*4097),'a=b, z=bad;Version=-1','a=b, z=bad;Domain=.','a=b;Comment=x;y=z','a=b;Path=""','a=b;Version=1;Path=""']:
  add(new(),f'RX {enc(uris[0])} {enc(header)}','C',f'H {enc(uris[0])}')
 for fqdn,host in itertools.product(['','.example.com','.EXAMPLE.com','.(none)','.a','.'],['host','HOST','a.example.com','a.b.example.com','127.123.1.2','é.example.com']):
  uri='http://'+host+'/a/b';add(new(fqdn=fqdn),f'S {enc(uri)} {enc("a=b;Domain=example.com")}',f'H {enc(uri)}','C')
 rng=__import__('random').Random(19434)
 parts=['a','b','=', ';',',','"',"'",'\\',' ','\t','\r','\n','\0','é','\ud800','\udc00','Path','Domain','Version','Max-Age','Port','Secure','HttpOnly','Discard','Comment','0','1','-1','80','.example.com','/a','/other']
 for _ in range(10000):
  header=''.join(rng.choice(parts) for _ in range(rng.randrange(1,18)));add(new(),f'RX {enc(uris[0])} {enc(header)}','C',f'H {enc(uris[0])}')
 if not a.reuse_source:
  project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
  for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,key).text=value
  group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/config_http_cookies_reference.cs'));cs=w/'CookieReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);source=call([a.dotnet,str(w/'app/CookieReference.dll')]+(['--native-hash'] if a.native_hash else []),rows);
  if a.native_hash:seed=source.pop(0).split()[1];(w/'source-seed.txt').write_text(seed,encoding='utf-8')
  (w/'rows.txt').write_text('\n'.join(rows)+'\n',encoding='utf-8');(w/'source.txt').write_text('\n'.join(source)+'\n',encoding='utf-8')
 else:assert (w/'rows.txt').read_text(encoding='utf-8').splitlines()==rows;source=(w/'source.txt').read_text(encoding='utf-8').splitlines()
 native=call([a.native,'--probe']+(['--source-seed',(w/'source-seed.txt').read_text(encoding='utf-8')] if a.native_hash else []),rows);assert len(source)==len(native)==len(rows);diff=[dict(index=i,row=row,source=s,native=n) for i,(row,s,n) in enumerate(zip(rows,source,native)) if s!=n];(w/'cookie-results.json').write_text(json.dumps(dict(cases=len(rows),passed=not diff,differences=diff),indent=2)+'\n',encoding='utf-8');print('cases',len(rows),'differences',len(diff));print([(d['index'],d['row'][:90]) for d in diff[:5]]);assert not diff
 if a.provider_checks:
  import random
  rng=random.Random(770812)
  values=['', 'a', '\ud800', '\udc00', '\U0001f642']+[chr(i) for i in range(65536)]
  values+=['x'*i for i in range(1,513)]
  values+=[''.join(chr(rng.randrange(65536)) for _ in range(rng.randrange(65))) for _ in range(10000)]
  encoded=[enc(v) for v in values];actual=call([a.dotnet,str(w/'app/CookieReference.dll'),'--hash'],encoded);seed=actual.pop(0).split()[1]
  native_hash=call([a.native,'--hash',seed],encoded);assert actual==native_hash
  source_fqdn=call([a.dotnet,str(w/'app/CookieReference.dll'),'--fqdn']);native_fqdn=call([a.native,'--fqdn']);assert source_fqdn==native_fqdn
  (w/'native-provider-results.json').write_text(json.dumps(dict(passed=True,marvinCases=len(values),actualClrSeed=seed,fqdnSource=source_fqdn,fqdnNative=native_fqdn),indent=2)+'\n',encoding='utf-8')
  print('PASS',len(values),'actual CLR Marvin hashes and native FQDN snapshot')
if __name__=='__main__':main()
