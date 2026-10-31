"""Original user-account/generic provider constructors, real migration and shared static connection."""
import argparse,hashlib,itertools,json,platform,shutil,subprocess,uuid,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 sources=[ROOT/f for f in ['OpenSim/Data/SQLite/SQLiteUserAccountData.cs','OpenSim/Data/SQLite/SQLiteGenericTableHandler.cs','OpenSim/Data/SQLite/SQLiteFramework.cs','OpenSim/Data/Migration.cs','OpenSim/Data/IUserAccountData.cs']]
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false','StartupObject':'UserAccountInitializerReference'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in sources+[ROOT/'native-port/tests/migration_parser_reference.cs',ROOT/'native-port/tests/user_account_initializer_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 resource=ROOT/'OpenSim/Data/SQLite/Resources/UserAccount.migrations';r=ET.SubElement(group,'EmbeddedResource',Include=str(resource));ET.SubElement(r,'LogicalName').text='OpenSim.Data.SQLite.Resources.UserAccount.migrations'
 for name in ['OpenMetaverseTypes','System.Data.SQLite']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'UserAccountInitializer.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 arm=platform.machine().lower() in ['arm64','aarch64'];src,dst={'Windows':('e_sqlite3.dll','e_sqlite3.dll'),'Linux':('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so','libe_sqlite3.so'),'Darwin':('libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib','libe_sqlite3.dylib')}[platform.system()];shutil.copy2(ROOT/'bin/lib64'/src,w/'app'/dst)
 schema=resource.read_text(encoding='utf-8-sig').split(':VERSION 2')[0].replace(':VERSION 1','')
 legacy="CREATE TABLE users(UUID,username,surname,created);INSERT INTO users VALUES('00000000-0000-0000-0000-000000000003','Legacy','User',12);"
 versions=lambda v:"CREATE TABLE migrations(name,version int);INSERT INTO migrations VALUES('migrations',1),('UserAccount',"+str(v)+");"
 current=schema+"ALTER TABLE UserAccounts ADD active BOOLEAN NOT NULL DEFAULT TRUE;"
 setups=['',legacy,current,schema,schema+legacy,schema+versions(1),schema+versions(2),schema+legacy+versions(1),current+versions(3),versions(3),versions(0),'CREATE TABLE migrations(wrong);',legacy.replace("'Legacy'","NULL"),legacy.replace("'00000000-0000-0000-0000-000000000003'","NULL")]
 op=lambda name,**kw:dict(op=name,**kw);new=lambda connection='Data Source=$DB;Pooling=False',slot=0,realm='UserAccounts':op('new',connection=connection,slot=slot,realm=realm);get=lambda slot=0:op('get',slot=slot);cases=[]
 for setup,second in itertools.product(setups,['Data Source=$OTHER;Pooling=False','bad','URI=invalid','Data Source=$DB;Pooling=bogus']):
  cases.append(dict(setup=setup,fail='',ops=[new(),get(),op('store'),get(),new(second,1),get(1),get(),op('store',slot=1),op('delete'),get(1)]))
 for connection in ['bad','URI=invalid','Data Source=$DB;Version=2','Data Source=$DB;Pooling=bogus','Data Source=$DB;DATA SOURCE=$DB']:
  cases.append(dict(setup='',fail='',ops=[new(connection),get(),new(),get(),new('bad',1),get(1)]))
 for realm in ['', 'Missing','UserAccounts','UserAccounts WHERE 1=1 --']:
  cases.append(dict(setup='',fail='',ops=[new(realm=realm),get(),new('bad',1),get(1),op('store',slot=1)]))
 for fail in ['info:[MIGRATIONS]: Creating migrations at version 1','info:[MIGRATIONS]: Upgrading UserAccount to latest revision 3.','info:[MIGRATIONS]: Creating UserAccount at version 1','debug:[MIGRATIONS]: UserAccount data tables already up to date at revision 3']:
  cases.append(dict(setup='',fail=fail,ops=[new(),get(),new(slot=1),get(1),get(),op('store',slot=1)]))
 cases += [dict(setup='',fail='',ops=[new(),get(),op('raw',sql="ALTER TABLE UserAccounts ADD NewField TEXT DEFAULT 'new';"),op('store'),get(),new('bad',1),get(1)])]
 zero='00000000-0000-0000-0000-000000000000';scope='00000000-0000-0000-0000-000000000002'
 seed=current+versions(3)+"INSERT INTO UserAccounts(PrincipalID,ScopeID,FirstName,LastName,Created) VALUES('00000000-0000-0000-0000-000000000001','"+zero+"','First','Last',7),('00000000-0000-0000-0000-000000000002','"+scope+"','First','Other',8);"
 for query,scope_id in itertools.product([None,'.',' ','First','Last','Fir Other','Other Las','a b First','a b c First','a b','First Last third','First  Last','\tFirst\t','😀','%%%',"a'bc"],[zero,scope]):
  cases.append(dict(setup=seed,fail='',ops=[new(),op('search',query=query,scope=scope_id),op('where',query=query,scope=scope_id)]))
 run=w/str(uuid.uuid4());run.mkdir();rows=[]
 for name,command in [('source',[a.dotnet,str(w/'app/UserAccountInitializer.dll')]),('native',[str(Path(a.native).resolve())])]:
  folder=run/name;folder.mkdir();r=subprocess.run(command+[str(folder)],input='\n'.join(json.dumps(c,separators=(',',':')).encode().hex() for c in cases)+'\n',capture_output=True,text=True,timeout=150);assert r.returncode==0,r.stderr[-3000:];rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases),(len(rows[0]),len(rows[1]),len(cases));differences=[dict(case=c,source=s[:6000],native=n[:6000]) for c,s,n in zip(cases,*rows) if s!=n]
 (w/'user-account-initializer-results.json').write_text(json.dumps(dict(passed=not differences,observations=len(cases),migrationExecuted=True,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},schemaSha256=hashlib.sha256(resource.read_bytes()).hexdigest(),scope='Whole original user-account/generic/framework/Migration with actual SQLite, v1-v3 resource and legacy users import, shared static connection/initialization, migration failures/retained state/retries, multiple instances/realms, query schema cache and logger errors. Generic methods for arbitrary DTOs, reflection/runtime/reader lifetime/concurrency/provider options and actual Unix execution pending.',differences=differences[:5]),indent=2),encoding='utf-8');assert not differences,differences[:1]
 print(f'PASS: {len(cases)} original user-account initialization/shared connection observations')
if __name__=='__main__':main()
