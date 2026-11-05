"""Unchanged SQLite profile provider, actual managed SQLite and full migrations."""
from pathlib import Path
import argparse,subprocess,json,hashlib,platform,shutil,datetime,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/sqlite_profiles_reference.cs','OpenSim/Data/SQLite/SQLiteUserProfilesData.cs','OpenSim/Data/IProfilesData.cs','OpenSim/Framework/UserProfiles.cs','OpenSim/Data/Migration.cs']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in sources:ET.SubElement(items,'Compile',Include=str(root/f))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','System.Data.SQLite']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
resource='OpenSim/Data/SQLite/Resources/UserProfiles.migrations';r=ET.SubElement(items,'EmbeddedResource',Include=str(root/resource));ET.SubElement(r,'LogicalName').text='OpenSim.Data.SQLite.Resources.UserProfiles.migrations'
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
arm=platform.machine().lower() in ['arm64','aarch64'];source,target={'Windows':('e_sqlite3.dll','e_sqlite3.dll'),'Linux':('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so','libe_sqlite3.so'),'Darwin':('libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib','libe_sqlite3.dylib')}[platform.system()];shutil.copy2(root/'bin/lib64'/source,w/'app'/target)
def m(method,values={}):return f'M {method} '+('-' if values is None else json.dumps(values,ensure_ascii=True,separators=(',',':')).encode().hex())
def sql(text,read=False):return ('Q ' if read else 'X ')+text.encode().hex()
zero='00000000-0000-0000-0000-000000000000';other='01234567-89ab-cdef-0123-456789abcdef'
commands=[sql('select name,version from migrations order by name',True),sql("select name,sql from sqlite_master where type='table' order by name",True)]
commands += [m(i) for i in range(18)]
for text in ['',"quote' <&> é 😀",'line\ntext','nul\x00end']:
 for flags in [0,1,2,4,64,255]:
  ad=dict(Name=text,Description=text,ParcelName=text,Flags=flags,Price=123,CreatorId=zero,SnapshotId=other)
  commands += [m(1,ad),m(0),m(2),sql('select expirationdate-creationdate from classifieds',True),m(3),m(0)]
for top in [True,False]:
 for enabled in [True,False]:
  for text in ['','pick é','nul\x00end']:
   pick=dict(Name=text,Desc=text,ParcelName=text,OriginalName=text,SimName=text,TopPick=top,Enabled=enabled,SortOrder=-12,SnapshotId=other)
   commands += [m(6,pick),m(4),m(5),sql('select toppick,enabled,sortorder from userpicks',True),m(15),m(7),m(4)]
for text in ['',None,'notes é','nul\x00end',' ']:commands += [m(9,dict(Notes=text)),m(8),sql('select useruuid,targetuuid,quote(notes) from usernotes',True)]
commands += [m(10),m(11,dict(WebUrl='http://profile.test/',AboutText='about',FirstLifeText='first',ImageId=other,FirstLifeImageId=other,PublishProfile=True)),m(10),m(12,dict(WantToMask=-7,SkillsMask=2147483647,WantToText='wants',SkillsText='skills',Language='en')),m(10),m(15)]
commands += [sql(f"insert into usersettings values('{zero}',1,0,'original')"),m(13,dict(IMViaEmail=True,Visible=True,EMail='new')),m(14),sql('select quote(imviaemail),quote(visible),email from usersettings',True)]
commands += [m(16),sql("insert into userdata values('','tag','old','oldval')"),m(16,dict(TagId='tag')),m(17,dict(TagId='tag',DataKey='key',DataVal='value')),m(16,dict(TagId='tag')),sql('select * from userdata',True)]
for method in range(18):commands.append(m(method,None))
strings=['Name','Description','ParcelName','SimName','GlobalPos','Gatekeeper','Desc','OriginalName','WebUrl','AboutText','FirstLifeText','WantToText','SkillsText','Language','EMail','Notes','UserId','TagId','DataKey','DataVal']
for method in [1,6,9,11,12,13,16,17]:
 for field in strings:commands.append(m(method,dict(NullFields=[field])))
# Nonzero query identities exercise list, delete and pick read binding.
commands += [m(1,dict(ClassifiedId=other,CreatorId=other,Name='other')),m(0,dict(QueryId=other)),m(2,dict(ClassifiedId=other)),m(6,dict(PickId=other,CreatorId=other,Name='other')),m(4,dict(QueryId=other)),m(5,dict(QueryId=other,PickId=other)),m(7,dict(QueryId=other)),m(15,dict(QueryId=other))]
# Corrupt storage contents retain source partial mutation and cast/error precedence.
for field,values in [('priceforlisting',['32767','32768','-32769']),('classifiedflags',['-1','4294967296']),('creatoruuid',["'bad'",'123']),('creationdate',["'bad'",'2147483648'])]:
 for value in values:commands += [sql(f'update classifieds set {field}={value}'),m(2)]
for table in ['classifieds','userpicks','usernotes','userprofile','usersettings','userdata']:
 commands.append(sql(f'create trigger fail_{table} before update on {table} begin select raise(abort,\'fixture denied\'); end'))
commands += [m(i) for i in [1,6,9,11,12,13,17]]
commands += ['G']
for table in ['classifieds','userpicks','usernotes','userprofile','usersettings','userdata']:
 commands.append('G');commands.append(sql('drop table '+table));commands += [m(i) for i in range(18)]
(w/'commands.txt').write_text('\n'.join(commands)+'\n')
outputs={};time_bounds={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 start=(datetime.datetime.now()-datetime.datetime(1970,1,1)).total_seconds();r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120);end=(datetime.datetime.now()-datetime.datetime(1970,1,1)).total_seconds();assert r.returncode==0,(name,r.returncode,r.stderr);outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands));(w/(name+'.txt')).write_text(r.stdout,encoding='utf-8');time_bounds[name]=(start,end)
def normalized(row,name,command):
 parts=row.split('|')
 if len(parts)==4 and parts[2] not in ['N','-']:
  value=json.loads(bytes.fromhex(parts[2]))
  if isinstance(value,dict) and 'CreationDate' in value:
   created=value['CreationDate'];expires=value['ExpirationDate']
   if command.startswith('M 1 '):
    lower,upper=time_bounds[name];assert lower-5<=created<=upper+5,(name,command,created,lower,upper)
    expected=(7 if value['Flags']==2 else 365)*86400;assert expires-created==expected,(created,expires,expected)
   value['CreationDate']=value['ExpirationDate']='<clock>';parts[2]=json.dumps(value,sort_keys=True)
 return '|'.join(parts)
diffs=[dict(command=c,source=x,native=y) for c,x,y in zip(commands,outputs['reference'],outputs['native']) if normalized(x,'reference',c)!=normalized(y,'native',c)]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:15],applicationParityVerified=False,scope='Full original SQLiteUserProfilesData, Migration and original migration resource with actual System.Data.SQLite/SDK; native real SQLite provider. Stateful CRUD for all18 operations, parameter mistakes/ADO errors, null DTO/string fields, Unicode/NUL, query identities, schema/version and storage mutation, corrupt values and missing tables. Ad clock samples bounded to actual local wall clock with exact7/365day duration, then normalized for independent processes; arbitrary clock/timezone/overflow/concurrency/provider/connection APIs remain partial. Main/viewer/MySQL/PGSQL/application/macOS incomplete.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources+[resource]},managedProviderHash=hashlib.sha256((root/'bin/System.Data.SQLite.dll').read_bytes()).hexdigest())
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' SQLite profile differences; see results.json';print('PASS:',len(commands),'original SQLite profile observations')
