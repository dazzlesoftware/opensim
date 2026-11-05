"""Actual System.Xml retained node mutation closure, not reader/writer certification."""
import argparse,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
def enc(s):return '~' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '-'
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'OutputType':'Exe','TargetFramework':'net8.0','EnableDefaultCompileItems':'false','Nullable':'enable','RuntimeFrameworkVersion':'8.0.31','RollForward':'Disable'}.items():ET.SubElement(props,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(Path(__file__).with_name('nini_xml_dom_reference.cs').resolve()));file=w/'Reference.csproj';ET.ElementTree(project).write(file);r=subprocess.run([a.dotnet,'build',str(file),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 rows=[]
 def add(*ops):rows.append('|'.join(ops))
 for name in [None,'','a','1','a-b','a:b','xmlns','xmlns:x',':a','a:',':','a:b:c','a b','a\0b','\ud800','\ud800\udc00','\ud800a','a\udfff','\u00e9','\u6c49']:
  add('D 0',f'E 1 0 {enc(name)}',f'A 2 0 {enc(name)}')
 base=['D 0','E 1 0 0061','E 2 0 0062','E 3 0 0063','APP 0 1','APP 1 2','APP 1 3']
 for current in [0,1,2,3]:
  for change in ['RM 1 2','RM 1 3','APP 1 2','APP 2 3','APP 2 1','APP 0 2','APP 2 2']:
   add(*base,'EN 0 1',*(['MV 0']*current),change,'MV 0','CU 10 0','MV 0','CU 11 0','N 12 2','P 13 2')
 for value in [None,'','v','<>&\"\t\n\r','\0','\ud800','\udc00','\U0001f600']:
  add(*base,'A 4 0 0078',f'V 4 {enc(value)}','AA 1 4','EN 0 4','MV 0','CU 8 0',f'V 4 {enc(value)}','P 9 8','MV 0','GET 7 1 0078','EQ 7 4','OWN 10 4')
 add(*base,'A 4 0 0078','A 5 0 0079','A 6 0 0078','AA 1 4','AA 1 5','AA 1 6','AA 1 5','AA 2 6','OWN 7 4','OWN 8 6','P 9 6')
 add(*base,'D 4','E 5 4 0066','APP 4 5','APP 1 5','P 6 5','ROOT 7 4','A 8 4 0078','AA 1 8','DROP 0','P 9 1','N 10 2')
 for parent in [0,1,2]:
  for child in [0,1,2,3,4,5]:add('D 0','E 1 0 0061','A 2 0 0078','T 3 0 0076','D 4','E 5 4 0062',f'APP {parent} {child}',f'RM {parent} {child}',f'V {parent} 0076')
 for op in ['C','CD','W','S','PI']:
  for value in [None,'','v',' \t\r\n','<>&','xml','a:b','a b','\0','\ud800','\U0001f600',']]>','--','?>']:
   for parent in [0,1,2]:add('D 0','E 1 0 0061','A 2 0 0078',f'{op} 3 0 {enc(value)}',f'APP {parent} 3',f'V 3 {enc(value)}','EN 0 '+str(parent),'MV 0','CU 4 0','RM '+str(parent)+' 3','MV 0')
 for first in ['x:a','xml:a','xmlns:a']:
  for second in ['a','y:a','xml:a','xmlns:a']:add('D 0','E 1 0 0061',f'A 2 0 {enc(first)}',f'A 3 0 {enc(second)}','AA 1 2','AA 1 3','GET 4 1 '+enc(first),'GET 5 1 '+enc(second))
 for prefix in [None,'','p','xml','xmlns','a b',':','p:q']:
  for local in [None,'','x','xmlns','1','a:b','a b','\ud800']:
   for uri in [None,'','urn:test','http://www.w3.org/2000/xmlns/','http://www.w3.org/XML/1998/namespace']:
    args=enc(prefix)+' '+enc(local)+' '+enc(uri);add('D 0','E 1 0 0061','EP 2 0 '+args,'AP 3 0 '+args,'AP 4 0 '+enc('q')+' '+enc(local)+' '+enc(uri),'AA 1 3','AA 1 4')
 for name in [None,'','lt','gt','amp','apos','quot','unknown','a b','a:b','#x41','#','\ud800']:
  for parent in [0,1,2]:
   if name is None or name.startswith('#'):
    add('D 0',f'ER 3 0 {enc(name)}');continue
   add('D 0','E 1 0 0061','A 2 0 0078',f'ER 3 0 {enc(name)}','T 4 0 0076',f'APP {parent} 3','EN 0 3','MV 0','CU 5 0','APP 3 4','V 5 0078','RM 3 5','V 3 0078',f'RM {parent} 3','APP 1 3','P 6 5')
 for parent in [0,1,2]:
  for populated in [0,1,2,3]:
   ops=['D 0','E 1 0 0061','A 2 0 0078','F 3 0','E 4 0 0062','E 5 0 0063','T 6 0 0076']
   for child in range(4,4+populated):ops.append(f'APP 3 {child}')
   add(*ops,'EN 0 3','MV 0',f'APP {parent} 3','MV 0','CU 7 0','P 8 4','P 9 5','P 10 6','V 3 0076')
 rng=random.Random(953)
 for _ in range(200):
  ops=base+['A 4 0 0078','A 5 0 0078','A 6 0 0079','T 7 0 0076','EN 0 1']
  for __ in range(20):
   op=rng.randrange(7)
   if op==0:ops.append(f'APP {rng.randrange(4)} {rng.randrange(8)}')
   elif op==1:ops.append(f'RM {rng.randrange(4)} {rng.randrange(8)}')
   elif op==2:ops.append(f'AA {rng.randrange(1,4)} {rng.randrange(4,7)}')
   elif op==3:
    mutation_target=rng.randrange(8)
    mutation_value=rng.choice([None,"","text","\ud800","\0"])
    ops.append(f'V {mutation_target} {enc(mutation_value)}')
   elif op==4:ops.append('MV 0')
   elif op==5:ops.append('CU 12 0')
   else:ops.append(f'GET 13 {rng.randrange(1,4)} 0078')
  add(*ops)
 for _ in range(500):
  ops=['D 0','E 1 0 0061','E 2 0 0062','ER 3 0 '+enc(rng.choice(['lt','unknown','amp'])),'A 4 0 0078','CD 5 0 0076','C 6 0 0076','PI 7 0 0070','T 8 0 0076','W 9 0 0020','F 10 0','EN 0 1']
  for __ in range(35):
   kind=rng.randrange(8)
   if kind==0:ops.append(f'APP {rng.randrange(11)} {rng.randrange(11)}')
   elif kind==1:ops.append(f'RM {rng.randrange(11)} {rng.randrange(11)}')
   elif kind==2:ops.append(f'AA {rng.choice([1,2])} 4')
   elif kind==3:
    mutation_target=rng.randrange(11)
    mutation_value=rng.choice([None,"","x"," ","\ud800","\0"])
    ops.append(f'V {mutation_target} {enc(mutation_value)}')
   elif kind==4:ops.append('MV 0')
   elif kind==5:ops.append('CU 12 0')
   elif kind==6:ops.append(f'N 13 {rng.randrange(11)}')
   else:ops.append(f'P 14 {rng.randrange(11)}')
  add(*ops)
 if not a.quick:
  for code in range(65536):
   for name in [chr(code),'a'+chr(code)]:add('D 0',f'E 1 0 {enc(name)}',f'A 2 0 {enc(name)}')
 text='\n'.join(rows)+'\n';(w/'inputs.txt').write_text(text,encoding='utf-8');e=subprocess.run([a.dotnet,str(w/'app/Reference.dll')],input=text,text=True,capture_output=True);n=subprocess.run([str(Path(a.native).resolve()),'--probe'],input=text,text=True,capture_output=True);assert e.returncode==0,e.stderr;assert n.returncode==0,n.stderr;expected=e.stdout.splitlines();actual=n.stdout.splitlines();assert len(expected)==len(actual)==len(rows),(len(expected),len(actual),len(rows));diff=[{'input':rows[i],'expected':x,'actual':actual[i]} for i,x in enumerate(expected) if x!=actual[i]];(w/'xml-dom-results.json').write_text(json.dumps({'cases':len(rows),'operations':sum(len(row.split('|')) for row in rows),'differences':diff[:10],'scope':'Native retained DOM methods for supported node kinds; parser/DTD/declaration/writer closure remains separate.'},indent=2),encoding='utf-8');assert not diff,(len(diff),w/'xml-dom-results.json');print(f'PASS: {len(rows)} actual System.Xml retained DOM flows')
if __name__=='__main__':main()
