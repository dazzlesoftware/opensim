import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s): return 'null' if s is None else ''.join(f'{ord(c):04x}' for c in s) or '-'
def call(cmd,rows=None):
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180)
 assert r.returncode==0,(r.returncode,r.stdout[-3000:],r.stderr[-3000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/nini_config_reference.cs'));r=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(r,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'NiniConfigReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[]
 def add(*ops):rows.append('|'.join(ops))
 texts=[None,'','k','K','other','0','true','false','42','-42','2147483648']
 for ini,name,key,value,op in itertools.product([0,1],[None,'','section'],texts,[None,'','v','42'],['ADD','ST','REM','NAME']):
  prefix=['S 0 '+str(ini),f'C 0 0 {enc(name)} {ini}']
  add(*prefix,f'{op} 0 {enc(key)}'+(f' {enc(value)}' if op in ['ADD','ST'] else ''))
 for ini,key,value,mode in itertools.product([0,1],texts,texts,range(17)):
  add('S 0 '+str(ini),f'C 0 0 {enc("s")} {ini}',f'ADD 0 {enc(key)} {enc(value)}',f'GET 0 {enc(key)} {mode} 7')
 for ini,autosave,ks,kr,op in itertools.product([0,1],[0,1],[0,1,2,-1],[0,1,-1],['ST','REM']):
  cmds=[f'S 0 {ini}',f'AN 0 0 {enc("s")}',f'ADD 0 {enc("k")} {enc("old")}',f'AS 0 {autosave}']
  if ks>=0:cmds.append(f'KS 0 1 {ks}')
  if kr>=0:cmds.append(f'KR 0 2 {kr}')
  add(*cmds,f'{op} 0 {enc("k")}' +(f' {enc("new")}' if op=='ST' else ''))
 for ini in [0,1]:
  for action in ['MERGE 0 1','MERGE 0 0','MERGE 0 -1','A 0 1','A 0 0','REMOVE 0 1','CLEAR 0',f'NAME 0 {enc("b")}']:
   add(f'S 0 {ini}',f'S 1 {ini}',f'AN 0 0 {enc("a")}',f'AN 1 1 {enc("b")}',f'ST 0 {enc("x")} {enc("first")}',f'ST 1 {enc("y")} {enc("second")}',action)
 for doc,cs,action in itertools.product(['','[a]\nx=1\ny=2\n','[a]\n;comment\nx=1\n','[a]\nx=first\nx=second\n','[a]\nx=1\n[b]\ny=2\n','[bad','x=1\n'],[0,1],['TEXT 0','SAVE 0','RELOAD 0','SW 0','DOCNULL 0']):add('S 0 1',f'CS 0 {cs}',f'LOAD 0 {enc(doc)}',action)
 for value in ['${x}','${missing}','${b|x}','prefix ${x} suffix','${b|x|ignored}','${unclosed']:
  add('S 0 0',f'AN 0 0 {enc("a")}',f'AN 0 1 {enc("b")}',f'ADD 0 {enc("x")} {enc("value")}',f'ADD 1 {enc("x")} {enc("other")}',f'ADD 0 {enc("k")} {enc(value)}',f'EXP 0 0 {enc("k")}')
 for scope,alias,key in itertools.product([0,-1],texts,texts):add('S 0 0',f'AN 0 0 {enc("a")}',f'AI {scope} {enc(key)} {enc(alias)} 99',f'ADD 0 {enc(key)} {enc(alias)}',f'GET 0 {enc(key)} 3 0')
 for first,second in itertools.product(texts,texts):add('S 0 0',f'AN 0 0 {enc("a")}',f'BOX 0 {enc(first)} {enc(second)}',f'SET 0 {enc("x")} 0')
 for initial,mutation,tail in itertools.product([False,True],['CLEAR 0','REMOVEAT 0 0',f'AN 0 1 {enc("b")}','COLSET 0 -99 -1','INSERT 0 0 -1'],['CURRENT 0','NEXT 0','RESET 0','NEXT 0|CURRENT 0']):
  cmds=['S 0 0']
  if initial:cmds += [f'AN 0 0 {enc("a")}']
  add(*cmds,'EN 0 0','NEXT 0',mutation,tail)
 for index in [-1,0,1,2147483647]:
  add('S 0 0',f'COPYNULL 0 {index}')
 for obj in [-1,0]:add('S 0 0',f'BOX 0 {enc("x")} {enc("y")}',f'LADD 0 {obj}')
 # Real file-backed source reload preserves existing config objects while mutating keys.
 file_path=str(w/'configuration-fixture.ini')
 docs=['','[a]\nx=1\ny=2\n','[a]\n;comment\nx=3\n','[b]\ny=2\n[c]\nz=4\n','[a]\nx=first\nx=last\n','[bad','x=before\n']
 for first,second,cs,op in itertools.product(docs,docs,[0,1],['RELOAD','SAVE']):
  add('S 0 1',f'CS 0 {cs}',f'FILE {enc(file_path)} {enc(first)}',f'LP 0 {enc(file_path)}',f'FILE {enc(file_path)} {enc(second)}',f'{op} 0','TEXT 0')
 for initial,newname,newvalue in itertools.product(docs[:5],[None,'','a','b'],[None,'','updated']):
  add('S 0 1',f'FILE {enc(file_path)} {enc(initial)}',f'LP 0 {enc(file_path)}',f'AN 0 0 {enc(newname)}',f'ST 0 {enc("k")} {enc(newvalue)}','SAVE 0','RELOAD 0','TEXT 0')
 for enum,key,value in itertools.product([-1,0,1,2],texts,['Zero','One','Duplicate','Minus','missing']):add('S 0 0',f'AN 0 0 {enc("a")}',f'AE 0 {enc(key)} {enum}',f'ADD 0 {enc(key)} {enc(value)}',f'GET 0 {enc(key)} 3 0')
 rng=random.Random(12943)
 for _ in range(12000):
  ini=rng.randrange(2);cmds=[f'S 0 {ini}',f'C 0 0 {enc("s")} {ini}','A 0 0']
  for j in range(16):
   key=enc(rng.choice(texts));value=enc(rng.choice(texts));kind=rng.randrange(10)
   cmds.append([f'ADD 0 {key} {value}',f'ST 0 {key} {value}',f'REM 0 {key}',f'GET 0 {key} {rng.randrange(17)} 7',f'AS 0 {rng.randrange(2)}',f'NAME 0 {key}',f'AI 0 {key} {value} 8',f'AB 0 {key} {rng.randrange(2)}',f'KS 0 {j} {rng.randrange(3)}',f'KR 0 {j} {rng.randrange(2)}'][kind])
  add(*cmds)
 source=call([a.dotnet,str(w/'app/NiniConfigReference.dll')],rows);native=call([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows),(len(source),len(native),len(rows))
 diff=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(rows,source,native)) if s!=n]
 report=dict(passed=not diff,cases=len(rows),niniSha256=hashlib.sha256((ROOT/'bin/Nini.dll').read_bytes()).hexdigest(),scope='Actual bundled Nini classes with explicit controlled ASCII conversion/culture providers; core ordered state, nulls, aliases, event partial failures, merge references, expansion and INI load/save. Full invariant backend validated separately.',differences=diff[:10]);(w/'nini-config-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:3]);print('PASS:',len(rows),'actual bundled Nini config cases')
if __name__=='__main__':main()
