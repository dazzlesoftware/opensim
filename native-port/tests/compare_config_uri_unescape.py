import argparse,pathlib,subprocess,json,random,itertools
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work-dir',required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=pathlib.Path(a.work_dir).resolve();w.mkdir(parents=True,exist_ok=True);(w/'Program.cs').write_bytes(pathlib.Path(__file__).with_name('config_uri_unescape_reference.cs').read_bytes());(w/'reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>');subprocess.run([a.dotnet,'build',str(w/'reference.csproj'),'-c','Release','-v:q'],check=True)
rows=[]
def add(mode,syntax,query,reserved,text):rows.append(f'{mode} {syntax} {int(query)} {reserved} '+text.encode('utf-16-be','surrogatepass').hex())
for mode,syntax,query,reserved in itertools.product(range(32),range(3),range(2),range(4)):
 for c in range(256):
  add(mode,syntax,query,reserved,chr(c));add(mode,syntax,query,reserved,'%'+format(c,'02X'))
 for text in ['','%','%x','%xx','%2','%2e','%FF%80','%e1%80%41','%c0%80','%f4%90%80%80','%ed%a0%80','%f0%90%80%80','%E2%80%8e','a%zzb%2Ec%80d','%E1%80%FF%2F','%F0%90%80%41']:
  add(mode,syntax,query,reserved,text)
if not a.quick:
 for c in range(65536):
  for mode in [3,7,10,26]:add(mode,1,False,0,chr(c))
 for x in range(128,256):
  for y in range(128,256):
   for mode,syntax in [(3,0),(3,1),(10,1),(26,1)]:add(mode,syntax,False,0,f'%{x:02X}%{y:02X}%80%80%2f')
 r=random.Random(3811)
 for i in range(10000):
  text=''.join(r.choice(['%','%zz','%2F','%FF','%C2','%E1','%80','%F4','%90','a','\0','\ud800','\uffff']) for n in range(r.randrange(32)))
  add(r.randrange(32),r.randrange(2),r.randrange(2),r.randrange(4),text)
payload='\n'.join(rows)+'\n';reference=subprocess.check_output([a.dotnet,str(w/'bin/Release/net8.0/reference.dll')],input=payload,text=True).splitlines();native=subprocess.check_output([str(pathlib.Path(a.native).resolve())],input=payload,text=True).splitlines()
if reference!=native:
 for i,(x,y) in enumerate(zip(reference,native)):
  if x!=y:print('MISMATCH',i,rows[i],x,y);(w/'mismatch.json').write_text(json.dumps({'row':rows[i],'reference':x,'native':y}));break
 raise SystemExit(1)
report={'cases':len(rows),'equal':True,'quick':a.quick,'scope':'actual UriHelper.UnescapeString string/char[] overload result/errors with syntax/null/flags/reservations; source UTF8 percent helper'};(w/'report.json').write_text(json.dumps(report,indent=2));print(report)

