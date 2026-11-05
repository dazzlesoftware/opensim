"""Actual bundled Nini.dll differential of the retained native configuration view."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as E
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):return s.encode().hex() or '-'
def call(cmd,rows=None):
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert not r.returncode,(r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():E.SubElement(props,k).text=v
 g=E.SubElement(project,'ItemGroup');E.SubElement(g,'Compile',Include=str(Path(__file__).with_name('ini_config_host_reference.cs').resolve()));r=E.SubElement(g,'Reference',Include='Nini');dll=ROOT/'bin/Nini.dll';E.SubElement(r,'HintPath').text=str(dll);file=w/'Reference.csproj';E.ElementTree(project).write(file);call([a.dotnet,'build',str(file),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[];labels=[]
 def add(label,*ops):rows.append('|'.join(ops));labels.append(label)
 def cmd(op,i,*args):return ' '.join([op,str(i),*args])
 def text(s,i=0):return cmd('TEXT',i,enc(s))
 def setv(k,v,s='s',i=0):return cmd('SET',i,enc(s),enc(k),enc(v))
 def get(k,s='s',f='fallback',i=0):return cmd('GET',i,enc(s),enc(k),enc(f))
 def integer(k,s='s',i=0):return cmd('INT',i,enc(s),enc(k),'17')
 def boolean(k,s='s',i=0):return cmd('BOOL',i,enc(s),enc(k),'0')
 def order(s='s',i=0):return cmd('ORDER',i,enc(s))
 numeric=['','0','-0','42','+42',' -42\t','2147483647','-2147483648','2147483648','-2147483649','9999999999999999999999x','42\0','42\0\0','42\0 ','\u00a042','4 2','0x10','1.0','1e2','+','--1','\t\r\n42\v\f ','0'*120+'42','-'+'0'*120+'2147483648'];rng=random.Random(529432)
 for _ in range(700):numeric.append(rng.choice(['','+','-',' '])+str(rng.randrange(-3000000000,3000000000))+rng.choice(['','\0','\t','x','\0\0']))
 for v in numeric:add('typed-number','NEW 0',setv('x',v),integer('x'),get('x'),integer('missing'),integer('x','absent'))
 for value,enabled in itertools.product(['true','True','TRUE','false','False','FALSE','Yes','No','On','Off','1','0','',' true','true ','tr\u00adue','t\u034frue','fa\u200blse','True\0','\u212a','\ud7ff'],[0,1]):
  ops=['NEW 0',setv('x',value)]
  if enabled:ops+=[cmd('ALIAS',0,enc(s),str(v)) for s,v in [('Yes',1),('No',0),('On',1),('Off',0)]]
  add('source-alias',*ops,boolean('x'),boolean('missing'),get('x'))
 for alias,value in itertools.product(['Yes','On','true','1','y\u00ades'],['Yes','On','True','1','y\u00ades']):add('actual-argv-file-owner',text('[Probe]\nx='+value+'\n'),'ARG 1',cmd('ALIAS',1,enc(alias),'1'),'NEW 2','MERGE 2 0','MERGE 2 1',cmd('OWNER',2,enc('Probe'),'0'),boolean('x','Probe',2),boolean('x','Probe'),get('x','Probe',i=2))
 for key,look in [('x','X'),('KELVIN\u212a','kelvink'),('I','i'),('\u0131','I'),('\u017f','S'),('\U00010400','\U00010428'),('e\u0301','\u00e9')]:
  add('loaded-case',text('[s]\n'+key+'=one\n'),get(look),'CASE 0 0',get(look),setv(look,'updated'),order())
  add('added-base-case','NEW 0',setv(key,'one'),'CASE 0 0',get(look),setv(look,'updated'),order())
 add('live-copy-order','NEW 0',setv('z','old'),setv('a','second'),setv('z','new'),order(),'COPY 1 0',setv('a','shared',i=1),get('a'),order(i=1),'DROP 0',get('z',i=1))
 add('case-duplicates',text('[s]\nX=first\nx=second\n'),'CASE 0 0',order(),setv('x','updated'),order())
 for value in ['${base}','before ${base} after','${other|key}','${other|key|ignored}','${missing}','${absent|key}','${other|missing}','${unfinished','${x}','${BASE}','${base}${base}','plain']:
  add('expansion-phase',text('[s]\nbase=old\nx='+value+'\n[other]\nkey=cross\n'),get('x'),'REPLACE 0',get('x'),setv('base','new'),get('x'),setv('x','${base}'),get('x'),'REPLACE 0',get('x'))
 for n in [1,2,8,24]:add('finite-expansion',text('[s]\nk0=done\n'+''.join(f'k{i}=${{k{i-1}}}\n' for i in range(1,n+1))),get('k'+str(n)),'REPLACE 0',get('k'+str(n)),order())
 for value in ['\ud800','\udfff','1\ud800','True\ud800','\ud800\udfff','\x00','\x00True']:
  raw=''.join(f'{ord(c):04x}' for c in value) or '-';add('raw-UTF16-errors','NEW 0',cmd('RAW',0,enc('s'),enc('x'),raw),get('x'),integer('x'),boolean('x'))
 for content in ['[s]\nx=first\nx=second\n','[s]\nx=first\n[s]\ny=second\n','[s]\nInclude-X=not-read.ini\nx=${missing}\n','x=value\n','[s]\n#comment0=value\n; comment\n','[Bad','[s]\nx="unterminated','[s]\nx=\n']:add('raw-load',text(content),cmd('HAS',0,enc('s')),order(),get('x'))
 add('missing-defaults','NEW 0',get('missing'),cmd('INT',0,enc('absent'),enc('k'),'-2147483648'),cmd('BOOL',0,enc('absent'),enc('k'),'1'),setv('present',''),get('present'),cmd('BOOL',0,enc('s'),enc('missing'),'1'),cmd('HAS',0,enc('absent')),cmd('CON',0,enc('s'),enc('missing')))
 add('argv-owned-alias','ARG 1',cmd('ALIAS',1,enc('On'),'1'),setv('x','On','Startup',1),boolean('x','Startup',1),'NEW 0','MERGE 0 1',cmd('OWNER',0,enc('Startup'),'1'),boolean('x','Startup'), 'DROP 1',boolean('x','Startup'))
 add('existing-file-owner-on-argv-merge',text('[Startup]\nx=True\n'),'ARG 1',cmd('ALIAS',1,enc('On'),'1'),setv('x','On','Startup',1),boolean('x','Startup',1),'MERGE 0 1',cmd('OWNER',0,enc('Startup'),'0'),boolean('x','Startup'))
 fixture=w/'real-files';fixture.mkdir(exist_ok=True);files=[];content='[s]\nz=old\na=second\nx=${z}\nInclude-X=missing-file.ini\n'
 for index,(encoding,bom) in enumerate([('utf-8',b''),('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]):
  path=fixture/f'configuration-\u914d\u7f6e-e-{index}.ini';path.write_bytes(bom+content.encode(encoding));files.append(str(path));saved=fixture/f'saved-{index}.ini';add('real-file-save-load',cmd('LOAD',0,enc(str(path))),get('x'),setv('z','new'),order(),cmd('SAVE',0,enc(str(saved))),cmd('LOAD',1,enc(str(saved))),get('z',i=1),get('x',i=1),'REPLACE 1',get('x',i=1))
 missing=fixture/'missing-\u914d\u7f6e.ini';add('real-file-missing',cmd('LOAD',0,enc(str(missing))))
 malformed=fixture/'invalid.ini';malformed.write_text('[Bad',encoding='utf-8');add('real-file-parser-error',cmd('LOAD',0,enc(str(malformed))))
 for value in ['True','False','Yes','On','1','tr\u00adue','+42',' 42\t','42\0','-9223372036854775808','9223372036854775808','1.0000000596046447753906250000001','-0','1e9999','NaN','1,000','999999999999999999999x']:
  add('production-Flotsam-direct',text('[Probe]\nx='+value+'\n'),*[cmd(op,0,enc('Probe'),enc('x')) for op in ['FI','FL','FR','FB']])
 for value in ['\ud800','1\ud800','True\ud800']:
  add('production-Flotsam-raw-error','NEW 0',cmd('RAW',0,enc('Probe'),enc('x'),''.join(f'{ord(c):04x}' for c in value)),*[cmd(op,0,enc('Probe'),enc('x')) for op in ['FI','FL','FR','FB']])
 for index,(encoding,bom) in enumerate([('utf-8',b''),('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]):
  path=fixture/f'host-startup-{index}.ini';path.write_bytes(bom+'[s]\nz=42\na=${z}\nflag=True\n[LoginService]\nSRV_Z=z\nSRV_A=a\n'.encode(encoding));add('host-startup-encodings',cmd('HOSTLOAD',0,enc(str(path))),get('a'),integer('a'),boolean('flag'),order('LoginService'),setv('z','43'),get('a'),setv('a','${z}'),get('a'),'REPLACE 0',integer('a'))
 for label,content in [('effective-include','[s]\nInclude-X=a\n'),('include-overwritten','[s]\nInclude-X=a\n[s]\nx=ok\n'),('include-parse-error','[s]\nInclude-X=a\n[Bad'),('include-before-expansion-error','[s]\nx=${missing}\nInclude-X=a\n'),('include-culture-prefix','[s]\nI\u00adnclude-X=a\n'),('include-case-sensitive','[s]\ninclude-X=a\nx=ok\n'),('missing-expansion','[s]\nx=${missing}\n'),('self-expansion','[s]\nx=${x}\n'),('unfinished-expansion','[s]\nx=${unfinished\n')]:
  path=fixture/(label+'.ini');path.write_text(content,encoding='utf-8');add('host-'+label,cmd('HOSTLOAD',0,enc(str(path))))
 observations=[call([a.dotnet,str(w/'app/Reference.dll')],rows),call([a.native,'--probe'],rows)];assert len(observations[0])==len(observations[1])==len(rows)
 differences=[dict(index=i,label=labels[i],commands=row,source=s,native=n) for i,(row,s,n) in enumerate(zip(rows,*observations)) if s!=n];report=dict(cases=len(rows),operations=sum(len(r.split('|')) for r in rows),real_files=files,actual_nini_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),differences=differences[:12]);(w/'ini-config-host-results.json').write_text(json.dumps(report,indent=2));assert not differences,json.dumps(differences[:3]);print(f"PASS: {len(rows)} actual Nini host flows, {report['operations']} operations and {len(files)} real encoding files")
if __name__=='__main__':main()
