import argparse,pathlib,subprocess,json,itertools,random
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work-dir',required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=pathlib.Path(a.work_dir).resolve();w.mkdir(parents=True,exist_ok=True);(w/'Program.cs').write_bytes(pathlib.Path(__file__).with_name('config_uri_escape_reference.cs').read_bytes());(w/'reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>');subprocess.run([a.dotnet,'build',str(w/'reference.csproj'),'-c','Release','-v:q'],check=True)
rows=[]
def add(s,e,text):rows.append(f'{s} {e} '+('~' if text is None else text.encode('utf-16-be','surrogatepass').hex() or '-'))
for s,e in itertools.product(range(4),range(2)):
 for text in [None,'','abc','%41','%4a','%zz','%','%A','\ud800','\ud800\ud800\udc00']:add(s,e,text)
 for c in range(256 if a.quick else 65536):add(s,e,chr(c))
 for x in range(256):
  for y in range(16 if a.quick else 256):add(s,e,'%'+chr(x)+chr(y))
if not a.quick:
 for c in range(65536,1114112):add(0,1,chr(c))
 r=random.Random(7881)
 for i in range(10000):add(r.randrange(4),r.randrange(2),''.join(r.choice('a%20?#/\\\0\ud800\udc00\uffff') for n in range(r.randrange(100))))
payload='\n'.join(rows)+'\n';reference=subprocess.check_output([a.dotnet,str(w/'bin/Release/net8.0/reference.dll')],input=payload,text=True).splitlines();native=subprocess.check_output([str(pathlib.Path(a.native).resolve())],input=payload,text=True).splitlines()
if reference!=native:
 for i,(x,y) in enumerate(zip(reference,native)):
  if x!=y:print('MISMATCH',i,rows[i],x,y);(w/'mismatch.json').write_text(json.dumps({'row':rows[i],'reference':x,'native':y}));break
 raise SystemExit(1)
report={'cases':len(rows),'equal':True,'quick':a.quick,'scope':'actual UriHelper.EscapeString string overload text/identity/errors; four exact source character sets'};(w/'report.json').write_text(json.dumps(report,indent=2));print(report)
