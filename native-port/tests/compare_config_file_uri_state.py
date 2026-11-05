import argparse,pathlib,subprocess,json,itertools,random
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work-dir',required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=pathlib.Path(a.work_dir).resolve();w.mkdir(parents=True,exist_ok=True);(w/'Program.cs').write_bytes(pathlib.Path(__file__).with_name('config_file_uri_state_reference.cs').read_bytes());(w/'reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>');subprocess.run([a.dotnet,'build',str(w/'reference.csproj'),'-c','Release','-v:q'],check=True)
values=['','a','/a','//a','///a','////a','c:/a','c|/a','c:a','//c:/a','///c:/a','////c:/a','//localhost/x','//bad@host/x','//[::1]/x','//[::1]:80/x','//127.1/x','/a?b#c','//a?b#c','///a?b#c','\\\\a\\b','///','//','/','\\','/\\','\\/']
values += [''.join(s) for n in range(7) for s in itertools.product('/\\',repeat=n)]
values += [prefix+suffix for prefix in ['','/','//','///','////','\\\\','/\\/','\\/\\/'] for suffix in ['c:','c|','c:/','c|\\','c:/a','c:foo','c|foo','a/../x','a.b','a..b','%zz','?','#','a:8/x','[::]/x']]
if not a.quick:
 values += [prefix+chr(c)+suffix for c in range(65536) for prefix,suffix in [('///a','/x'),('//a','/x'),('///a?','x'),('///a#','x')]]
 r=random.Random(987);values += [''.join(r.choice('a.b:_-123/\\?#\0\ud800\uffff') for n in range(r.randrange(80))) for i in range(10000)]
rows=[]
for extra in [0,0x200000000,0x600000000]:
 for value in values:
  text='file:'+value;rows.append(f'{5|extra} '+text.encode('utf-16-be','surrogatepass').hex())
 for text,flags in [('c:/x',0x28100000),('C|\\x',0x28100000),('\\\\server\\x',0x30100002),('//server/x',0x30100002),('  \\\\server\\x',0x30100004)]:rows.append(f'{flags|extra} '+text.encode('utf-16-be').hex())
payload='\n'.join(rows)+'\n';reference=subprocess.check_output([a.dotnet,str(w/'bin/Release/net8.0/reference.dll')],input=payload,text=True).splitlines();native=subprocess.check_output([str(pathlib.Path(a.native).resolve())],input=payload,text=True).splitlines()
if reference!=native:
 for i,(x,y) in enumerate(zip(reference,native)):
  if x!=y:print('MISMATCH',i,rows[i],x,y);(w/'mismatch.json').write_text(json.dumps({'row':rows[i],'reference':x,'native':y}));break
 raise SystemExit(1)
report={'cases':len(rows),'equal':True,'quick':a.quick,'scope':'actual file CreateUriInfo/ParseRemaining flags/string/offsets/errors; Windows platform branch'};(w/'report.json').write_text(json.dumps(report,indent=2));print(report)
