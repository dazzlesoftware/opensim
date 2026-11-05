import argparse,json,os,subprocess,xml.etree.ElementTree as ET,hashlib,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(Path(__file__).resolve().with_name('config_http_linux_system_store_reference.cs')));ET.ElementTree(project).write(w/'Reference.csproj')
def run(command,env=None):
 r=subprocess.run(command,env=env,capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'])
rows=json.loads(run([a.dotnet,str(w/'app/Reference.dll'),str(w/'fixtures')]))
invalid_path=os.fsencode(w/'fixtures/certs')+b'/invalid-\xff.crt'
with open(invalid_path,'wb') as file:file.write((w/'fixtures/caseintermediate').read_bytes())
rows.append(dict(label='invalid-utf8-environment-path',file=os.fsdecode(invalid_path),dirs=''))
diff=[];outputs=[]
for row in rows:
 env=os.environ.copy()
 for key,value in [('SSL_CERT_FILE',row['file']),('SSL_CERT_DIR',row['dirs'])]:
  if value is None:env.pop(key,None)
  else:env[key]=value
 source=run([a.dotnet,str(w/'app/Reference.dll')],env);native=run([a.native],env)
 outputs.append(dict(label=row['label'],source=source,native=native))
 if source!=native:diff.append(outputs[-1])
cache=w/'cache.pem';directory=w/'cache-dir';directory.mkdir(exist_ok=True);fixtures=w/'fixtures'
for file in directory.iterdir():file.unlink()
shutil.copyfile(fixtures/'root',cache)
env=os.environ.copy();env.update(SSL_CERT_FILE=str(cache),SSL_CERT_DIR=str(directory))
source_process=subprocess.Popen([a.dotnet,str(w/'app/Reference.dll'),'--cache'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
native_process=subprocess.Popen([a.native,'--cache'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
cache_rows=[]
def observe(label,milliseconds,expected):
 values=[]
 for process in [source_process,native_process]:
  process.stdin.write(str(milliseconds)+'\n');process.stdin.flush();values.append(process.stdout.readline().rstrip('\n'))
 assert values[0].startswith(expected),(label,values[0][:100],expected)
 row=dict(label=label,milliseconds=milliseconds,source=values[0],native=values[1]);cache_rows.append(row)
 if values[0]!=values[1]:diff.append(row)
def write(file):
 shutil.copyfile(fixtures/file,cache);os.utime(cache,ns=(1577836800000000000,1577836800000000000))
try:
 observe('initial',0,'N r');write('bundle')
 observe('exact5seconds',5000,'S r');observe('over5seconds',5001,'N r')
 write('root')
 observe('exact5minutes',305001,'S r');observe('over5minutes',305002,'N r')
 cache.unlink();observe('deleted-exact5seconds',310002,'S r');observe('deleted-over5seconds',310003,'N')
 write('bundle');observe('restored',315004,'N r')
 observe('unchanged-check',320005,'S r');write('root');os.utime(cache,ns=(1577836801000000000,1577836801000000000))
 observe('unchanged-check-did-not-reset-clock',320006,'N r')
 shutil.copyfile(fixtures/'intermediate',directory/'one.pem');os.utime(directory,ns=(1577836802000000000,1577836802000000000))
 observe('directory-changed-exact5seconds',325006,'S r');observe('directory-changed-over5seconds',325007,'N r')
 (directory/'one.pem').unlink();directory.rmdir();observe('directory-deleted',330008,'N r')
finally:
 for process in [source_process,native_process]:
  process.stdin.close();process.wait(timeout=30);assert process.returncode==0,process.stderr.read()
(w/'results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),cacheCases=len(cache_rows),differences=diff,observations=outputs,cacheObservations=cache_rows,scope='Actual Linux runtime private system store loader; isolated SSL_CERT_FILE/SSL_CERT_DIR, exact ordered root/intermediate DER and cache snapshot reuse. File/dir/dedup/encoding/formatted Subject==Issuer and PEM/DER fallback semantics; private runtime stopwatch versus native injected clock at exact and exceeded5s/5min, mtime invalidation, deletion, restoration and unchanged-check timer retention. No machine trust mutation; default-path fallback/TLS integration remain separate checks.'),indent=2)+'\n')
assert not diff,f'{len(diff)} differences: {w}/results.json'
print(f'PASS: {len(rows)} Linux system-store loading scenarios')
