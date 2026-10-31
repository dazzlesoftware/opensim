"""Compile the entire original generic table/friends/framework classes against real SQLite."""
import argparse,hashlib,itertools,json,platform,shutil,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');sources=[ROOT/f for f in ['OpenSim/Data/SQLite/SQLiteFriendsData.cs','OpenSim/Data/SQLite/SQLiteGenericTableHandler.cs','OpenSim/Data/SQLite/SQLiteFramework.cs','OpenSim/Data/IFriendsData.cs']]
 for f in sources+[ROOT/'native-port/tests/sqlite_generic_table_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 for name in ['OpenMetaverseTypes','System.Data.SQLite']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/f'{name}.dll')
 cs=w/'GenericTable.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 arm=platform.machine().lower() in ['arm64','aarch64'];src,dst={'Windows':('e_sqlite3.dll','e_sqlite3.dll'),'Linux':('libe_sqlite3-arm64.so' if arm else 'libe_sqlite3.so','libe_sqlite3.so'),'Darwin':('libe_sqlite3_OSX_arm64.dylib' if arm else 'libe_sqlite3_OSX_x64.dylib','libe_sqlite3.dylib')}[platform.system()];shutil.copy2(ROOT/'bin/lib64'/src,w/'app'/dst)
 op=lambda kind,**kw:dict(op=kind,**kw)
 typed="CREATE TABLE Records(PrincipalID TEXT,Active INTEGER,Count INTEGER,Name TEXT,Extra);INSERT INTO Records VALUES('00000000-0000-0000-0000-000000000001',1,7,'first','extra');"
 friends="CREATE TABLE Friends(PrincipalID VARCHAR,Friend VARCHAR,Flags VARCHAR,Offered VARCHAR DEFAULT '0',Extra,PRIMARY KEY(PrincipalID,Friend));INSERT INTO Friends VALUES('a','b','7','0','one'),('b','a','5','123','two');"
 cases=[]
 arrays=[([],[]),(['PrincipalID'],[]),(None,[]),([],None),(['Name'],['first']),(['PrincipalID'],[None]),([None],['a']),([''],['a']),(['PrincipalID','PrincipalID'],['a','b']),(['PrincipalID','principalid'],['a','b']),(['PrincipalID','Friend'],['a','b']),(['Flags'],['7']),(['Flags x'],['7']),(['bad`'],['7']),(['Flags\0x'],['7']),(['Name'],['first\0tail'])]
 for type,setup,(fields,keys) in itertools.product(['typed','friends'],[None],arrays):
  setup=typed if type=='typed' else friends
  cases.append(dict(type=type,setup=setup,ops=[op('fields',fields=fields,keys=keys),op('where',where='1'),op('delete',fields=fields,keys=keys),op('where',where='1')]))
 for type,where in itertools.product(['typed','friends'],[None,'.','0','1','Name=\'first\'','PrincipalID=\'a\'','1 ORDER BY PrincipalID','1 LIMIT 1','1; DELETE FROM Friends','1;SELECT 2','bad syntax','1 -- trailing comment']):
  cases.append(dict(type=type,setup=typed if type=='typed' else friends,ops=[op('where',where=where),op('field',field='PrincipalID',key='a'),op('singledelete',field='PrincipalID',key='a'),op('where',where='1')]))
 for type,values in itertools.product(['typed','friends'],[None,[],[['Flags','3']],[['Extra','x']],[['PrincipalID','changed']],[['Friend','other']],[['Name',None]],[['Active','0']],[['Count','100']],[['PrincipalID','a'],['principalid','b']],[['Unused','x']],[['Flags',None]]]):
  for name,nullrow in [('written',False),(None,False),('.',False),('written',True)]:
   cases.append(dict(type=type,setup=typed if type=='typed' else friends,ops=[op('store',name=name,nullrow=nullrow,data=values),op('where',where='1')]))
 declarations=['','INT','INTEGER','INT32','REAL','DOUBLE','TEXT','VARCHAR(32)','BOOLEAN','BLOB','TINYINT','SMALLINT','INT8','INTEGER8','INTEGER16','INTEGER32','INTEGER64','BIGINT','BIGUINT','UINT','UINT64','SMALLUINT','TINYSINT','SINGLE','LOGICAL','MEDIUMINT','MEDIUMUINT']
 literals=['NULL','0','1','-1','0.5','1.5','2.5','2147483648',"'7'","'bad'","'True'","'f'","x'0102'",'1e-8','1e18','0.1','0.500000001','-129','65536','-2147483649']
 for field,decl,value in itertools.product(['Active','Count','Extra'],declarations,literals):
  definitions=dict(Active='INTEGER',Count='INTEGER',Extra='');definitions[field]=decl;values=dict(Active='1',Count='7',Extra="'extra'");values[field]=value
  setup=f"CREATE TABLE Records(PrincipalID TEXT,Active {definitions['Active']},Count {definitions['Count']},Name TEXT,Extra {definitions['Extra']});INSERT INTO Records VALUES('00000000-0000-0000-0000-000000000001',{values['Active']},{values['Count']},'first',{values['Extra']});"
  cases.append(dict(type='typed',setup=setup,ops=[op('where',where='1')]))
 for type in ['typed','friends']:
  setup=typed if type=='typed' else friends
  ops=[op('where',where='0')]
  if type=='friends':ops=[op('friends',principal='a')]
  ops += [op('raw',sql='ALTER TABLE '+('Records' if type=='typed' else 'Friends')+" ADD NewColumn TEXT DEFAULT 'new';"),op('where',where='1')]
  if type=='friends':ops += [op('friends',principal='a')]
  cases.append(dict(type=type,setup=setup,ops=ops))
 for sql in ["SELECT PrincipalID,Active,Count,Name,Extra FROM Records","SELECT PrincipalID AS principalid,Active,Count,Name,Extra FROM Records","SELECT PrincipalID,Active,Count,NULL AS Name,Extra FROM Records","SELECT PrincipalID,Active,Count,Name,x'0102' AS Blob FROM Records","SELECT PrincipalID,Active,Count,Name,1.25 AS Number FROM Records","SELECT PrincipalID,Active,Count,Name FROM Records WHERE 0","SELECT Name FROM Records","SELECT PrincipalID,Active,Count,Name,'lower' AS name,Extra FROM Records","SELECT 'lower' AS name,PrincipalID,Active,Count,Name,Extra FROM Records"]:
  cases.append(dict(type='typed',setup=typed,ops=[op('query',sql=sql),op('where',where='1')]))
 commands='\n'.join(json.dumps(c,separators=(',',':')).encode().hex() for c in cases)+'\n';rows=[]
 for command in [[a.dotnet,str(w/'app/GenericTable.dll')],[str(Path(a.native).resolve())]]:
  r=subprocess.run(command,input=commands,capture_output=True,text=True,timeout=150);assert r.returncode==0,r.stderr[-3000:];rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases),(len(rows[0]),len(rows[1]),len(cases));assert not any('FIXTURE_EX' in v for row in rows for v in row)
 differences=[dict(case=c,source=s[:5000],native=n[:5000]) for c,s,n in zip(cases,*rows) if s!=n]
 (w/'sqlite-generic-table-results.json').write_text(json.dumps(dict(passed=not differences,observations=len(cases),migrationExecuted=False,sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},scope='Complete original generic-table/friends/framework methods and actual managed SQLite. Native C++ explicit field binding covers string/UUID/bool/int and Data; selected declared database types, named parameters, query/delete overloads, REPLACE collisions, NULLs, schema cache and virtual friends Delete semantics. Source migration is controlled separately; arbitrary DTO/reflection, remaining provider/decimal/date/GUID types, culture/GC/reader/locking/concurrency/flags and Unix execution pending.',differences=differences[:10]),indent=2),encoding='utf-8');assert not differences,differences[:2]
 print(f'PASS: {len(cases)} original generic SQLite table scenarios')
if __name__=='__main__':main()
