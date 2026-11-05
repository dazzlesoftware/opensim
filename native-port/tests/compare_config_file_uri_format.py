import argparse,pathlib,subprocess,json,itertools,random
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work-dir',required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=pathlib.Path(a.work_dir).resolve();w.mkdir(parents=True,exist_ok=True);(w/'Program.cs').write_bytes(pathlib.Path(__file__).with_name('config_file_uri_format_reference.cs').read_bytes());(w/'reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>');subprocess.run([a.dotnet,'build',str(w/'reference.csproj'),'-c','Release','-v:q'],check=True)
values=['file:'+v for v in ['','/a','//a','///a','////a','c:/a','c|/a','c:a','//c:/a','///c:/a','////c:/a','//localhost/x','//bad@host/x','//[::1]/x','//127.1/x','/a?b#c','//a?b#c','///a?b#c','///','//','/','\\','/\\','\\/']]
values += [prefix+s for prefix in ['file:///','file://host/','C:/','C|/','\\\\server\\'] for s in [''.join(v) for n in range(5) for v in itertools.product('./\\a',repeat=n)]]
values += [prefix+s for prefix in ['file:///','C:/','\\\\server\\'] for s in ['%2e','%2e%2e','%2e/%2E/x','a%2f../x','%zz','%FF%80','%5c','a%3Fb%23c','a?b#c','a #b','é中','a\ud800x','a\0b','.../x','../x','./x','a//b','a/./b','a/../b']]
if not a.quick:
 values += [prefix+chr(c)+'/x' for c in range(65536) for prefix in ['file:///a','C:/a','\\\\server\\a']]
 r=random.Random(714);values += [r.choice(['file:///','C:/','\\\\server\\'])+''.join(r.choice('./\\%?#a\ud800\udc00\uffff\0{[') for n in range(r.randrange(100))) for i in range(10000)]
rows=[f'{format} '+s.encode('utf-16-be','surrogatepass').hex() for format in [1,2,3,32767] for s in values]
payload='\n'.join(rows)+'\n';reference=subprocess.check_output([a.dotnet,str(w/'bin/Release/net8.0/reference.dll')],input=payload,text=True).splitlines();native=subprocess.check_output([str(pathlib.Path(a.native).resolve())],input=payload,text=True).splitlines()
if reference!=native:
 for i,(x,y) in enumerate(zip(reference,native)):
  if x!=y:print('MISMATCH',i,rows[i],x,y);(w/'mismatch.json').write_text(json.dumps({'row':rows[i],'reference':x,'native':y}));break
 raise SystemExit(1)
report={'cases':len(rows),'equal':True,'quick':a.quick,'scope':'actual Uri.GetParts Path|KeepDelimiter four formats after real constructor; Windows file paths'};(w/'report.json').write_text(json.dumps(report,indent=2));print(report)
