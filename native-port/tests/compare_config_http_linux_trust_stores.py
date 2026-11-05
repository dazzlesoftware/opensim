import argparse,json,os,subprocess,xml.etree.ElementTree as ET,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(Path(__file__).resolve().with_name('config_http_linux_trust_stores_reference.cs')));ET.ElementTree(project).write(w/'Reference.csproj')
def run(command,env=None):
 r=subprocess.run(command,env=env,capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'])
reference=[a.dotnet,str(w/'app/Reference.dll')];resources=json.loads(run([*reference,'--resources']));rows=[]
def compare(label,args,env):
 source_env=env.copy();target_env=env.copy();traces={}
 if label.startswith('passwd-'):
  for kind,settings in [('source',source_env),('native',target_env)]:
   trace=w/(label+'-'+kind+'.trace');trace.unlink(missing_ok=True);settings['OPENSIM_TEST_PWTRACE']=str(trace);traces[kind]=trace
 source=run([*reference,*args],source_env)
 if target_env.get('LD_PRELOAD') and os.environ.get('OPENSIM_TEST_ASAN_LIBRARY'):target_env['LD_PRELOAD']=os.environ['OPENSIM_TEST_ASAN_LIBRARY']+':'+target_env['LD_PRELOAD']
 native=run([a.native,*args],target_env);row=dict(label=label,source=source,native=native)
 if traces:
  row['passwdTraces']={kind:path.read_text().splitlines() for kind,path in traces.items()}
  if label=='passwd-long':
   for trace in row['passwdTraces'].values():
    sizes=[int(line.split()[1]) for line in trace];assert any(sizes[i:i+4]==[256,512,1024,2048] for i in range(len(sizes)-3)),sizes
 rows.append(row)
for home in [None,'','.','./relative-home','/tmp/opensim-home','/tmp/opensim-home/','/tmp//nested///home','/tmp/home/../other','home\\windows','/tmp/é中',os.fsdecode(b'/tmp/invalid-\xff')]:
 env=os.environ.copy()
 if home is None:env.pop('HOME',None)
 else:env['HOME']=home
 compare('home-'+repr(home),['--paths','/tmp/replaced-home'],env)
shim=w/'passwd-fixture.so';run(['cc','-shared','-fPIC',str(Path(__file__).resolve().with_name('linux_passwd_fixture.c')),'-ldl','-o',str(shim)])
for mode in ['short','long','empty','missing','null','error','eintr','effective','invalidutf8']:
 env=os.environ.copy();env.update(HOME='',LD_PRELOAD=str(shim),OPENSIM_TEST_PASSWD=mode)
 compare('passwd-'+mode,['--paths','/tmp/replaced-home'],env)
for variant in ['missing','empty','file','subdirectory','invalid-file','pfx','hidden-file','directory-symlink','file-symlink','broken-symlink','invalid-name','unreadable','relative-home','symlink-dotdot-home']:
 with tempfile.TemporaryDirectory(dir=w,prefix='disallowed-') as temp:
  home=Path(temp);env_home=str(home)
  if variant=='symlink-dotdot-home':
   target=home/'external/deep';target.mkdir(parents=True);(home/'link').symlink_to(target,target_is_directory=True);env_home=str(home/'link/../canonical-home');home=home/'canonical-home'
  root=home/'.dotnet/corefx/cryptography/x509stores';root.mkdir(parents=True);disallowed=root/'disallowed'
  if variant=='file':disallowed.write_text('ordinary file')
  elif variant!='missing':
   disallowed.mkdir()
   if variant=='subdirectory':(disallowed/'nested').mkdir();(disallowed/'nested/cert.pfx').write_text('ignored')
   if variant in ['invalid-file','relative-home','symlink-dotdot-home']:(disallowed/'file.txt').write_text('invalid')
   if variant=='pfx':(disallowed/'cert.pfx').write_text('invalid')
   if variant=='hidden-file':(disallowed/'.hidden').write_text('invalid')
   if variant=='directory-symlink':(root/'target').mkdir();(disallowed/'link').symlink_to(root/'target',target_is_directory=True)
   if variant=='file-symlink':(root/'target').write_text('invalid');(disallowed/'link').symlink_to(root/'target')
   if variant=='broken-symlink':(disallowed/'link').symlink_to(root/'missing')
   if variant=='invalid-name':
    with open(os.fsencode(disallowed)+b'/invalid-\xff','wb') as file:file.write(b'invalid')
   if variant=='unreadable':disallowed.chmod(0)
  env=os.environ.copy();env['HOME']=os.path.relpath(home) if variant=='relative-home' else env_home
  try:compare('disallowed-'+variant,['--disallowed'],env)
  finally:
   if variant=='unreadable':disallowed.chmod(0o700)
diff=[row for row in rows if row['source']!=row['native']]
(w/'results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),resources=resources,differences=diff,observations=rows,scope='Actual pinned Linux PersistedFiles path/root caching and X509Store Disallowed opening versus native; isolates HOME, compares complete UTF16 paths and exception chains. Unset/empty HOME uses effective-user passwd. Disallowed missing/empty/file/subdirectories/symlinks/arbitrary files/invalid names/permission failures included. Combined cached-chain parity is a separate matrix.'),indent=2)+'\n')
assert not diff,f'{len(diff)} differences: {w}/results.json'
print(f'PASS: {len(rows)} Linux trust-store path/disallowed scenarios')
