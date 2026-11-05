import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return '~'
 b=s.encode('utf-16-le','surrogatepass');return ''.join(f'{b[i]|b[i+1]<<8:04x}' for i in range(0,len(b),2)) or '-'
def run(command,rows):
 r=subprocess.run(command,input='\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,(r.returncode,r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work',type=Path,required=True);p.add_argument('--reuse-source',action='store_true');a=p.parse_args();w=a.work;w.mkdir(parents=True,exist_ok=True);rows=[]
 def add(s):rows.append('P '+enc(s))
 def relative(base,value):rows.append(('RS ' if value is not None and value.startswith('\\\\') else 'R ')+enc(base)+' '+enc(value))
 for c in map(chr,range(65536)):
  add('http://example/a'+c+'b?x='+c+'#'+c)
  if ord(c)%17==0:add('http://a'+c+'.com/')
 for c in map(chr,range(65536)):
  add('http://u'+c+'p@example/a')
  add('http://a'+c+'.com/')
 for host in ['example','EXAMPLE','loopback','LOOPBACK','localhost','127.1','0x7f.1','0177.01','127.0.0.1','2130706433','0x7f000001','é','É','ÉA','faß.DE','xn--fa-hia.de','a。b','a．b','a｡b','[::]','[::1]','[::ffff:127.0.0.1]','[fe80::1234%25zone]','[fe80::1234%scope/64]','[2001:db8::1]','[0:0:0:0:0:0:127.0.0.1]']:
  for port,path in itertools.product(['',':',':80',':00080',':81',':0',':65535'],['','/a','/a/../b','/a/%2E%2e/b','/a/%2f/b','/a/%5c/b','/a\\b','/é?x=é#é']):add('http://'+host+port+path)
 for s in [None,'',' ','http:','http:/','http://','http:///x','http://a:abc/','http://a:65536/','http://a:-1/','1http://a/','://a/','http://-a/','http://a b/','http://a\\b/','http://a..b/','http://[::g]/','http://_é/','http://-é/','http://é_/','http://a..é/','http://a%00b/','http://a%20b/'] :add(s)
 fragments=['','a','a/b','..','../x','../../x','./a','./','a/..','a/.','a//b','%2e','%2E%2e','%2f','%5C','%25','%zz','%41%7e','%c3%a9','%ff','é','\ud800','\U00010400','?x=1','#f','?','#','  x  ','\\x','/x','//other/x']
 for base,ref in itertools.product(['http://example','http://u:p@example:81/a/b?x#f','https://[fe80::1%zone]/a/b','http://loopback/a'],fragments):relative(base,ref)
 for byte in range(256):
  for token in [f'%{byte:02x}',f'%{byte:02X}']:
   for tail in ['',' ','\t']:
    for prefix in ['http://example/','http://example/a?','http://example/a#','http://example/é/','http://example/é?']:add(prefix+token+tail)
 for base,ref in itertools.product(['http://example/é/a?é#é','http://example/%41/%c3%a9/%2f?%7e#f',' HTTP://EXAMPLE:080/a/b  ','http://u%41:p%22@example/a'],fragments):relative(base,ref)
 for ref in [None,'http:x','http:/x','HTTP:../x','https://other/a','http://other/a','  ?x  ','\\\\host/x','/\\host/x']:relative('http://example/a/b?x#f',ref)
 # The shared core now supplies built-in non-HTTP getter behavior as well.
 for scheme,body in itertools.product(['ws','wss','ftp','file','gopher','nntp','news','mailto','uuid','telnet','ldap','net.tcp','net.pipe','vsmacros','a+b'],['','body','/a/b','//host/a/b','//user:pass@HOST:81/a?b#c','//[fe80::1%25zone]:81/a','///C:/a b','\u00e9?\u4e2d#\u03a9']):
  add(scheme+':'+body)
 for base,ref in itertools.product(['ftp://host/a/b','file:///tmp/a/b','a+b://host/a/b'],fragments):relative(base,ref)
 # Malformed escapes, surrogate pair boundaries, constructor limits and relative ambiguity.
 for first,second in itertools.product([0,0x7f,0x80,0x9f,0xa0,0xbf,0xc0,0xc1,0xc2,0xdf,0xe0,0xed,0xef,0xf0,0xf4,0xf5,0xff],repeat=2):
  token=f'%{first:02x}%{second:02X}'
  for prefix in ['http://example/','http://example/a?','http://example/a#','http://example/é/']:add(prefix+token)
 for value in ['\U00010000','\U00010400','\U0001f600','\U00020000','\U000effff','\U000f0000','\U0010fffd','\U0010ffff','\ud800\ud800','\udc00\ud800','\udfff','\udbff']:
  for prefix in ['http://example/','http://example/a?','http://example/a#','http://u']:
   add(prefix+value+('@example/' if prefix=='http://u' else ' '))
 for host in ['a','-a','a..b','a_','_a','1','0x','09','[::1]','[::g]']:
  for port in [':-1',':+1',':65536',':00065535',':00065536',':abc',':1a',': 1']:
   add('http://'+host+port+'/')
 for n in [1,62,63,64,255,256,1023,65518,65519,65520,65521]:
  add('http://'+('a'*n)+'/');add('http://example/'+('a'*n));add('http://example/'+('%41'*(n//3)));add('http://example/'+('\ud800'*(n//9)))
 for prefix in ['http','https','HTTP']:
  for rest in ['',':','x','/x','//other/x','\\x','../a','?q','#f','  x  ']:relative('http://example/a/b?x#f',prefix+':'+rest)
 for base,ref in itertools.product(['http://example/a b/%22/%3c/%7f/%20/','http://é.example/a/','http://example/a?é#x'],['','x','../x','?x','#x','  ','/x']):relative(base,ref)
 for ref in ['c:x','c:/x','c:\\x','a:b','a:','x|/a','\\/other/x','/\\other/x',' \\\\host/x ','1:x','1foo:x']:
  rows.append('RS '+enc('http://example/a/b')+' '+enc(ref))
 for info in ['',':','u:','u:p',':p','%41','%22','[x]']:
  for authority in ['example','EXAMPLE','[::1]']:
   add('http://'+info+'@'+authority+'/a');relative('http://'+info+'@'+authority+'/a','b')
 for host in ['[1::]','[1:2::]','[2001:db8::]','[::ffff:0.0.0.0]','[::ffff:0:0]']:
  add('http://'+host+'/')
 ipv6_rng=random.Random(1203)
 for _ in range(500):
  groups=[ipv6_rng.randrange(65536) for _ in range(8)];start=ipv6_rng.randrange(8);length=ipv6_rng.randrange(1,9-start);groups[start:start+length]=[0]*length
  add('http://['+':'.join(f'{g:x}' for g in groups)+']/')
 rng=random.Random(912)
 alphabet='abcABC./\\?%#:_0123 !éß\u200d\ud800\udc00'
 for _ in range(10000):
  tail=''.join(rng.choice(alphabet) for _ in range(rng.randrange(24)));add('http://example/'+tail)
 for base in ['http://example/a/b','https://user:pass@example:444/é']:
  for ref in ['\\\\','\r\nhttp://example/a\t','   http://example/a   ']:relative(base,ref)
 for scheme,host,suffix in itertools.product(['http','https'],['[::]','[::1]','[2001:db8::1]','[::ffff:127.0.0.1]'],[']','].',']:#%GG','tail',']?q#f',']\U0001f600',']:80/a']):
  add(scheme+'://'+host+suffix+'/')
 assert rows
 source_path=w/'source.txt'
 if a.reuse_source:source=source_path.read_text(encoding='utf-8').splitlines();assert (w/'rows.txt').read_text(encoding='utf-8').splitlines()==rows
 else:
  project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
  for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
  group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/config_uri_components_reference.cs'));cs=w.resolve()/'UriComponentsReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');built=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w.resolve()/'app'),'--nologo','-v','quiet'],capture_output=True,text=True,encoding='utf-8',timeout=120);assert built.returncode==0,(built.stdout,built.stderr)
  source=run([a.dotnet,str(w.resolve()/'app/UriComponentsReference.dll')],rows);source_path.write_text('\n'.join(source)+'\n',encoding='utf-8');(w/'rows.txt').write_text('\n'.join(rows)+'\n',encoding='utf-8')
 native=run([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows),(len(source),len(native),len(rows));diff=[dict(index=i,row=row,source=s,native=n) for i,(row,s,n) in enumerate(zip(rows,source,native)) if s!=n];(w/'components-results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),differenceCount=len(diff),differences=diff,referenceSha256=hashlib.sha256((ROOT/'native-port/tests/config_uri_components_reference.cs').read_bytes()).hexdigest(),scope='Shared source-backed built-in URI component/relative-resolution views including nonHTTP getters. Explicit selected-ICU IDNA/normalization; custom UriParser registration/full application and macOS remain incomplete.'),indent=2)+'\n',encoding='utf-8');print('cases',len(rows),'differences',len(diff));print([(d['index'],d['row'][:100]) for d in diff[:5]]);assert not diff
if __name__=='__main__':main()
