"""Actual Nini.dll class closure with an explicitly test-only live System.Xml provider.
The production C++ class has a required XML dependency; this does NOT certify a native parser/writer.
"""
import argparse,hashlib,itertools,json,random,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):return '~' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '-'
class Peer:
 def __init__(self,cmd,cwd):self.p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',cwd=cwd)
 def send(self,s):self.p.stdin.write(s+'\n');self.p.stdin.flush()
 def read(self):
  line=self.p.stdout.readline()
  if not line:raise AssertionError(('peer closed',self.p.poll(),self.p.stderr.read()[-2000:]))
  return line.rstrip('\r\n')
 def ask(self,s):self.send(s);return self.read()
 def close(self):
  self.p.stdin.close()
  try:self.p.wait(timeout=20)
  except subprocess.TimeoutExpired:self.p.kill();self.p.wait()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');reference=Path(__file__).with_name('nini_xml_config_reference.cs');ET.SubElement(group,'Compile',Include=str(reference.resolve()));r=ET.SubElement(group,'Reference',Include='Nini');dll=ROOT/'bin/Nini.dll';ET.SubElement(r,'HintPath').text=str(dll);file=w/'Reference.csproj';ET.ElementTree(project).write(file)
 built=subprocess.run([a.dotnet,'build',str(file),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert not built.returncode,built.stdout+built.stderr
 source=Peer([a.dotnet,str(w/'app/Reference.dll')],w);dom=Peer([a.dotnet,str(w/'app/Reference.dll'),'--dom'],w);native=Peer([str(Path(a.native).resolve()),'--probe'],w);rows=[];labels=[];paths={};requests=0
 def add(label,*ops):labels.append(label);rows.append('|'.join(ops))
 def reader(xml,mode=0,op='LOADR',i=0):return f'{op} {i} {enc(xml)} {mode}'
 def setv(section,key,value,i=0,op='SET'):return f'{op} {i} {enc(section)} {enc(key)} {enc(value)}'
 def filename(name):return str(w/(name+'.xml'))
 base="<Nini><Section Name='s'><Key Name='z' Value='old'/><Key Name='a' Value='second'/></Section><Section Name='other'><Key Name='x' Value='v'/></Section></Nini>"
 xmls=['<Nini/>',base,'<nini/>','<Other/>','<Nini><Section/></Nini>',"<Nini><Section Name='s'><Key/></Section></Nini>","<Nini><Section Name='s'><Key Name='x'/></Section></Nini>","<Nini><Section Name='s'><Key Value='v'/></Section></Nini>","<Nini><Section Name='s'><Key Name='x' Value='one'/><Key Name='x' Value='two'/></Section></Nini>","<Nini><Section Name='s'><Key Name='x' Value='first'/></Section><Section Name='s'><Key Name='y' Value='lost'/></Section></Nini>","<Nini xmlns='urn:default'><Section Name='s'><Key Name='x' Value='v'/></Section></Nini>","<p:Nini xmlns:p='urn:p'/>","<Nini xmlns:p='urn:p'><p:Section Name='ignored'/><Section Name='s'><p:Key Name='ignored'/><Key Name='' Value=''/></Section></Nini>","<?xml version='1.0'?><!--before--><Nini><?pi data?><Section Name='s'>text<![CDATA[cdata]]><Key Name='x' Value='a&amp;b &quot;q&quot; &#xA;'/><!--kept--></Section></Nini>","<!DOCTYPE Nini [<!ENTITY x 'expanded'>]><Nini><Section Name='s'><Key Name='x' Value='&x;'/></Section></Nini>","<!DOCTYPE Nini [<!ELEMENT Nini ANY><!ELEMENT Section ANY><!ATTLIST Section Name CDATA 'default'>]><Nini><Section/></Nini>",'<Nini>','',"<Nini xml:space='preserve'> \n<Section Name='s'> <Key Name='x' Value='v'/> </Section> </Nini>","<Nini><Unknown/><Section Name='s'><key Name='ignored' Value='v'/><Key Name='x' Value='v'><Other/></Key></Section></Nini>"]
 for xml,mode,op in itertools.product(xmls[:5] if a.quick else xmls,range(3),['LOADR','READER']):add('load-reader-public', 'NEW 0',reader(xml,mode,op),'STRING 0','SAVE 0','SAVEW 0 0','IOSTATE 0','SAVES 0 0','IOSTATE 0')
 add('null-reader-method','NEW 0',reader(None),'STRING 0','IOSTATE 0')
 add('null-reader-constructor','NEW 0',reader(None,0,'READER'),'STRING 0','IOSTATE 0')
 for count in [0,-1]:add('consumed-closed-reader','NEW 0',reader(base),f'LOADLAST 0 {count}','IOSTATE 0','STRING 0')
 for name in [None,'','s',"quote'\"&",'\ud800']:
  for value in ([None,'v'] if a.quick else [None,'','v','\0','\ud800','x\r\ny','<&"', '\U0001f600']):add('live-config-to-DOM','NEW 0',setv(name,'key',value),'STRING 0','SAVEW 0 0','IOSTATE 0')
 for xml in [base,"<Nini><Section Name='a'/><Section Name='b'/><Section Name='c'/></Nini>"]:
  add('live-child-removal','NEW 0',reader(xml),'CLEAR 0','STRING 0','STRING 0')
 for fail in [0,1,2,3,10,-1,-2]:
  for op in ['SAVEW','SAVES']:add('writer-stream-lifecycle-fault','NEW 0',reader(base),f'{op} 0 {fail}','IOSTATE 0','STRING 0')
 target=filename('state');paths[target]=base.encode();
 for xml in [base,"<Wrong><Section Name='s'><Key Name='new' Value='reload'/></Section></Wrong>","<Nini><Section/><Section Name='s'/></Nini>","<Nini><Section Name='s'><Key Name='z' Value='changed'/><Key Name='new' Value='added'/></Section></Nini>"]:
  add('file-load-reload-current',f'PATH 0 {enc(target)}',f'FILE 0 {enc(target)} {enc(xml)}','RELOAD 0','STRING 0')
 for n,xml in enumerate(xmls[:5] if a.quick else xmls):
  path=filename('load-policy-'+str(n));paths[path]=xml.encode('utf-8','surrogatepass')
  for operation in ['PATH','LOADP']:add('file-default-document-policy','NEW 0',setv('old','k','kept'),f'{operation} 0 {enc(path)}','STRING 0')
 for path in [None,'',filename('missing'),str(w/'missing-directory/file.xml')]:add('path-error-savepath-order','NEW 0',setv('old','k','kept'),f'LOADP 0 {enc(path)}','SAVE 0','STRING 0',f'SAVEP 0 {enc(path)}')
 for fail in [0,1,2,-1,-2]:
  for op in ['SAVEW','SAVES']:add('file-path-preserved-on-save-failure',f'PATH 0 {enc(target)}',f'{op} 0 {fail}','IOSTATE 0','SAVE 0','STRING 0')
 add('reader-keeps-file-path',f'PATH 0 {enc(target)}',reader("<Nini><Section Name='changed'><Key Name='new' Value='data'/></Section></Nini>"),'SAVE 0','RELOAD 0','STRING 0')
 add('failed-reader-keeps-file-path',f'PATH 0 {enc(target)}',reader(None),'SAVE 0','STRING 0')
 for path in [None,'',filename('missing'),str(w/'missing-directory/file.xml')]:add('path-constructor-failure-retains-slot','NEW 0',setv('old','k','kept'),f'PATH 0 {enc(path)}','STRING 0')
 for event in ['s0','r0','a0:'+enc('s'),'k0:'+enc('s')+':'+enc('z')+':'+enc('new')]:
  for mutation in [0,1,2]:add('events-reentry-partial-state',f'PATH 0 {enc(target)}',f'FAIL 0 {enc(event)} 1 {mutation}','AUTO 0 1',setv('s','z','new'),'SAVEW 0 0',reader(base),'RELOAD 0','STRING 0')
 add('source-owner-merge','NEW 0','NEW 1',setv('foreign','z','first',1),'MERGE 0 1',setv('foreign','z','new'),'STRING 0','STRING 1')
 for index,(encoding,bom) in enumerate([('utf-8',b''),('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]):
  path=filename('encoding-'+str(index));paths[path]=bom+base.encode(encoding);add('real-file-byte-provider',f'PATH 0 {enc(path)}',setv('s','z','new'),'SAVE 0','RELOAD 0','STRING 0')
 if not a.quick:
  rng=random.Random(410873);names=[None,'','s','other','new',"quoted'\"",'\ud800'];keys=[None,'','z','a','x','new'];values=[None,'','v','new','<&"','\0','\ud800','line\r\nnext']
  for _ in range(260):
   operations=['NEW 0',reader(base)]
   for step in range(14):
    op=rng.choice(['SET','RAWADD','REM','DROPSEC','ADD','CLEAR','STRING','SAVEW','SAVES','AUTO','LOADR'])
    if op in ['SET','RAWADD']:operations.append(setv(rng.choice(names),rng.choice(keys),rng.choice(values),op=op))
    elif op=='REM':operations.append(f'REM 0 {enc(rng.choice(names))} {enc(rng.choice(keys))}')
    elif op in ['DROPSEC','ADD']:operations.append(f'{op} 0 {enc(rng.choice(names))}')
    elif op in ['SAVEW','SAVES']:operations.append(f'{op} 0 {rng.choice([0,1,2,-1,-2])}')
    elif op=='AUTO':operations.append('AUTO 0 '+str(rng.randrange(2)))
    elif op=='LOADR':operations.append(reader(rng.choice(xmls),rng.randrange(3)))
    else:operations.append(op+' 0')
   operations+=['STRING 0','IOSTATE 0'];add('random-public-state-flow',*operations)
 differences=[];started=time.monotonic()
 try:
  for index,row in enumerate(rows):
   for path,data in paths.items():Path(path).write_bytes(data)
   expected=source.ask(row)
   for path,data in paths.items():Path(path).write_bytes(data)
   native.send(row)
   while True:
    actual=native.read()
    if actual.startswith('@DOM '):requests+=1;native.send(dom.ask(actual[5:]));continue
    break
   if actual!=expected:
    differences.append(dict(index=index,label=labels[index],row=row,source=expected,native=actual))
    if len(differences)>=5:break
  report=dict(cases=index+1,planned=len(rows),dom_method_calls=requests,elapsed_seconds=time.monotonic()-started,differences=differences,actual_nini_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),scope='Actual unchanged Nini.dll vs native XmlConfigSource class over a REQUIRED test-only live System.Xml DOM/reader/writer provider. Provider serializes/parses with actual BCL; this certifies class algorithm/events/partial state/lifetime semantics, NOT a production native XML engine.');(w/'xml-config-class-results.json').write_text(json.dumps(report,indent=2));assert not differences,json.dumps(differences[:2]);print(f"PASS: {index+1} actual XmlConfigSource class flows, {requests} live System.Xml method calls")
 finally:source.close();native.close();dom.close()
if __name__=='__main__':main()
