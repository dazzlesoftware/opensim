"""Unchanged original migration resource selection/parser with real regex, sort and StreamReader."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');source=ROOT/'OpenSim/Data/Migration.cs'
 for f in [source,ROOT/'native-port/tests/migration_parser_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 cs=w/'MigrationParser.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 cases=[]
 def add(resources,after=0,type='AssetStore',sub='',fail=''):cases.append(dict(resources=[dict(name=n,data=None if d is None else ((d if isinstance(d,bytes) else d.encode()).hex() or ".")) for n,d in resources],after=after,type=type,sub=sub,fail=fail))
 versions=['',':VERSION 1\nSELECT 1;',':VERSION 1\r\nA\r\n:GO\nB\n:VERSION 3\nC',':VERSION 3\nfirst\n:VERSION 3\nsecond\n:VERSION 1\nlast',':VERSION 0\nx\n:VERSION -1\ny\n:VERSION 2\nz',':VERSION 1\nA\n:VERSION bad\nB',':VERSION 1#comment\n#comment\n\n \nSQL\n',':version +2\rSQL\r :gO \rTAIL\r',':VERSION 2147483648\nx',':VERSION ١\nx',':VERSION \u00a02\u00a0\nx',':VERSION 2\0\nx',':VER\u00adSION 2\nx',':VERSION 2\n:\u200bGO\nx','preamble\n:GO\n:VERSION 1\nx','\u200b#hidden\n:VERSION 1\nx']
 modern=['R.AssetStore.migrations','R.AssetStore.migrations.006','R.AssetStore.migrations.6','R.AssetStore.migrations.6.sql','R.AssetStore.migrations.x','R.AssetStore.migrations.٠٠٦','R.AssetStore.migrations.2147483648','R.AssetStore.migrationssuffix']
 for n,v,after in itertools.product(modern,versions,[-1,0,1,2,3,6]):add([(n,v),('R.000_AssetStore.sql','zero'),('R.002_AssetStore.sql','old2'),('R.007_AssetStore.sql','old7')],after)
 for after in [-2147483648,-1,0,1,999,2147483647]:
  add([],after);add([('unused','x')],after);add([('R.001_AssetStore.sql','old\r\nSQL\rTAIL')],after);add([('R.١٢٣_AssetStore.sql','unicode')],after)
 for type,sub in itertools.product(['AssetStore','Asset.Store','Asset(Store)','(Asset)Store','[','',None],['','SQLite',None]):
  add([('R.SQLite.001_AssetStore.sql','old'),('R.AssetStore.SQLite.migrations',':VERSION 2\nnew'),('R.AssetStore.migrations',':VERSION 1\nnormal')],type=type,sub=sub)
 for encoding in ['utf-8-sig','utf-16','utf-16-be','utf-32','utf-32-be']:
  body=':VERSION 2\r\n世界😀\n:GO\rnext'.encode(encoding)
  if encoding=='utf-16-be':body=b'\xfe\xff'+body
  if encoding=='utf-32-be':body=b'\0\0\xfe\xff'+body
  add([('R.AssetStore.migrations',body)])
 for raw in [b'\xff',b'\xff\xfea',b'\xff\xfe\0\0a',b':VERSION 1\n\xed\xa0\x80',b':VERSION 1\n\xf0\x9f',b':VERSION 1\n\0tail']:add([('R.AssetStore.migrations',raw)])
 for fail in ['names','read:R.AssetStore.migrations','read:R.001_AssetStore.sql']:
  add([('R.AssetStore.migrations',':VERSION 1\nx'),('R.001_AssetStore.sql','old')],fail=fail)
 for names in [['a.AssetStore.migrations','A.AssetStore.migrations'],['é.AssetStore.migrations','e\u0301.AssetStore.migrations'],['R.AssetStore.migrations.9','R.AssetStore.migrations.10']]:add([(name,f':VERSION {i+1}\n{i}') for i,name in enumerate(names)])
 rng=random.Random(28117)
 for _ in range(150):add([(rng.choice(modern),rng.choice(versions)),('R.003_AssetStore.sql','old')],rng.randrange(-1,8))
 commands='\n'.join(json.dumps(c,ensure_ascii=False,separators=(',',':')).encode().hex() for c in cases)+'\n';rows=[]
 for command in [[a.dotnet,str(w/'app/MigrationParser.dll')],[a.native]]:
  r=subprocess.run(command,input=commands,capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr[-2000:];rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 differences=[dict(case=c,source=s,native=n) for c,s,n in zip(cases,*rows) if s!=n]
 (w/'migration-parser-results.json').write_text(json.dumps(dict(passed=not differences,observations=len(cases),sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),scope='Whole original Migration class, actual .NET regex, invariant sorting and StreamReader decoding; controlled resource assembly and logger. Resource selection, merged/legacy version filtering, GO batches, invalid markers, BOM/replacement cases. General regex dialect/culture/deep resource/stream lifetime/runtime/platform semantics remain pending.',differences=differences[:5]),indent=2),encoding='utf-8')
 assert not differences,differences[:1]
 print(f'PASS: {len(cases)} migration resource parser source observations')
if __name__=='__main__':main()
