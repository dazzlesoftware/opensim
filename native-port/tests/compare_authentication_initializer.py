"""Whole original SQLite auth provider and real migrations; only TickCount is projected."""
import argparse,hashlib,itertools,json,platform,shutil,subprocess,uuid,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Data/SQLite/SQLiteAuthenticationData.cs';original=source.read_text(encoding='utf-8-sig');assert original.count('System.Environment.TickCount')==3;projected=w/'SQLiteAuthenticationData.cs';projected.write_text(original.replace('System.Environment.TickCount','AuthFixture.Tick'),encoding='utf-8')
 sources=[ROOT/f for f in ['OpenSim/Data/SQLite/SQLiteFramework.cs','OpenSim/Data/Migration.cs','OpenSim/Data/IAuthenticationData.cs']]
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false','StartupObject':'AuthenticationInitializerReference'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in sources+[projected,ROOT/'native-port/tests/migration_parser_reference.cs',ROOT/'native-port/tests/authentication_initializer_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 resource=ROOT/'OpenSim/Data/SQLite/Resources/AuthStore.migrations';r=ET.SubElement(group,'EmbeddedResource',Include=str(resource));ET.SubElement(r,'LogicalName').text='OpenSim.Data.SQLite.Resources.AuthStore.migrations'
 for name in ['OpenMetaverseTypes','System.Data.SQLite']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'AuthenticationInitializer.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 arm=platform.machine().lower() in ['arm64','aarch64'];src,dst={'Windows':('e_sqlite3.dll','e_sqlite3.dll'),'Linux':('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so','libe_sqlite3.so'),'Darwin':('libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib','libe_sqlite3.dylib')}[platform.system()];shutil.copy2(ROOT/'bin/lib64'/src,w/'app'/dst)
 schema=resource.read_text(encoding='utf-8-sig').split(':VERSION 2')[0].replace(':VERSION 1','');id='00000000-0000-0000-0000-000000000001';legacy=f"CREATE TABLE users(UUID,passwordHash,passwordSalt,webLoginKey);INSERT INTO users VALUES('{id}','oldhash','oldsalt','oldkey');"
 versions=lambda v:"CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('AuthStore',"+str(v)+");"
 op=lambda kind,**kw:dict(op=kind,**kw);new=lambda connection='Data Source=$DB;Pooling=False',slot=0,realm='auth':op('new',connection=connection,slot=slot,realm=realm);get=lambda slot=0:op('get',slot=slot);val=lambda kind,v:dict(kind=kind,value=v)
 data=[['passwordHash',val('string','hash')],['passwordSalt',val('string','salt')],['webLoginKey',val('string','key')]];store=lambda d=data,**kw:op('store',data=d,nullrow=False,**kw)
 setups=['',legacy,schema,schema+versions(1),schema+versions(2),schema+legacy+versions(1),versions(2),'CREATE TABLE migrations(wrong);',schema.replace('UUID char','uuid char')+versions(2)]
 cases=[]
 for setup,second in itertools.product(setups,['bad','Data Source=:memory:;Pooling=False']):cases.append(dict(setup=setup,fail='',ops=[new(),get(),store(),get(),new(second,1),get(1),op('item',item='passwordHash',value="'changed'"),get()]))
 for setup,d in itertools.product(['',schema+versions(2)+f"INSERT INTO auth(UUID) VALUES('{id}');"],[None,[],[['UUID',val('string','remove')]],data,data+[['UUID',val('string','remove')]],data+[['unknown',val('string','x')]],data+[['passwordHash',val('null',None)]],data+[['passwordHash',val('int',7)]],data+[['passwordHash',val('bool',True)]],data+[['passwordHash',val('double',2.5)]],data+[['passwordHash',val('blob','0102')]]]):cases.append(dict(setup=setup,fail='',ops=[new(),store(d),get(),op('store',data=data,nullrow=True)]))
 for item,v in itertools.product(['passwordHash','missing','',None], ["'text'",'NULL','7','passwordSalt',None]):cases.append(dict(setup='',fail='',ops=[new(),store(),op('item',item=item or None,value=v),get()]))
 for token,lifetime in itertools.product(['token',None,"bad'quote",'.'],[-1,0,1,2147483647]):cases.append(dict(setup='',fail='',ops=[new(),op('set',token=token,lifetime=lifetime,tick=30001),op('check',token=token,lifetime=lifetime,tick=30002)]))
 for ticks in [[0,30000,30001,60001,60002],[2147483647,-2147483648,-2147453648,-2147453647],[-1,0,30001]]:
  ops=[new()]
  for tick in ticks:ops += [op('raw',sql=f"INSERT INTO tokens VALUES('{id}','expired','1900-01-01');"),op('set',token='alive',lifetime=1,tick=tick),op('check',token='alive',lifetime=1,tick=tick)]
  cases.append(dict(setup='',fail='',ops=ops))
 for realm in ['', 'missing','auth','auth` WHERE 1=1 --']:cases.append(dict(setup='',fail='',ops=[new(realm=realm),get(),store(),get(),new(slot=1),store(slot=1),get(1)]))
 cases += [dict(setup='',fail='',ops=[new(),get(),op('raw',sql="ALTER TABLE auth ADD extra TEXT DEFAULT 'later';"),store(),get(),op('raw',sql="ALTER TABLE auth ADD extra2 TEXT DEFAULT 'hidden';"),get(),new(slot=1),get(1)])]
 cases += [dict(setup='',fail='',ops=[new(),store(),get(),op('gc'),op('raw',sql="ALTER TABLE auth ADD collected TEXT DEFAULT 'visible';"),new(slot=1),get(1)])]
 run=w/str(uuid.uuid4());run.mkdir();rows=[]
 for name,command in [('source',[a.dotnet,str(w/'app/AuthenticationInitializer.dll')]),('native',[str(Path(a.native).resolve())])]:
  folder=run/name;folder.mkdir();r=subprocess.run(command+[str(folder)],input='\n'.join(json.dumps(c,separators=(',',':')).encode().hex() for c in cases)+'\n',capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr[-3000:];rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases),(len(rows[0]),len(rows[1]),len(cases));differences=[dict(case=c,source=s[:6000],native=n[:6000]) for c,s,n in zip(cases,*rows) if s!=n]
 (w/'authentication-initializer-results.json').write_text(json.dumps(dict(passed=not differences,observations=len(cases),migrationExecuted=True,tickCountProjection=True,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,*sources]},schemaSha256=hashlib.sha256(resource.read_bytes()).hexdigest(),scope='Whole original SQLiteAuthenticationData/SQLiteFramework/Migration with actual provider and v1/v2 resource; only Environment.TickCount expression is projected. Real SQL, Get/Store mutation/types/failures, first-successful-read column cache, SetDataItem expressions, token lifetime/expiry/wrap and independent instance state. Exact exception/console text, arbitrary object/provider/date/GUID/decimal/GC/reader/concurrent/platform behavior pending.',differences=differences[:8]),indent=2),encoding='utf-8');assert not differences,differences[:2]
 print(f'PASS: {len(cases)} original authentication provider scenarios')
if __name__=='__main__':main()
