"""Actual unchanged Nini.dll vs concrete native reader/DOM load and live merge.
File-reader and XML writer callbacks remain explicit required dependencies.
"""
import argparse,hashlib,json,random,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def enc(s):return '~' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '-'
class Peer:
 def __init__(self,cmd,cwd):self.p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',cwd=cwd)
 def ask(self,s):
  self.p.stdin.write(s+'\n');self.p.stdin.flush();line=self.p.stdout.readline()
  if not line:raise AssertionError((self.p.poll(),self.p.stderr.read()))
  return line.rstrip('\r\n')
 def close(self):self.p.stdin.close();self.p.wait(timeout=30);assert self.p.returncode==0,self.p.stderr.read()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--quick',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(Path(__file__).with_name('nini_xml_native_document_reference.cs').resolve()));r=ET.SubElement(group,'Reference',Include='Nini');dll=next(path for path in [ROOT/'bin/Nini.dll',ROOT.parent/'bin/Nini.dll'] if path.is_file());ET.SubElement(r,'HintPath').text=str(dll);file=w/'Reference.csproj';ET.ElementTree(project).write(file)
 built=subprocess.run([a.dotnet,'build',str(file),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert built.returncode==0,built.stdout+built.stderr
 rows=[];labels=[]
 def add(label,*ops):labels.append(label);rows.append('|'.join(ops))
 def reader(xml,mode=1,op='LOADR',i=0):return f'{op} {i} {enc(xml)} {mode}'
 def setv(section,key,value,i=0,op='SET'):return f'{op} {i} {enc(section)} {enc(key)} {enc(value)}'
 base="<Nini><Section Name='s'><Key Name='z' Value='old'/><Key Name='a' Value='second'/></Section><Section Name='other'><Key Name='x' Value='v'/></Section></Nini>"
 xmls=['<Nini/>',base,'<nini/>','<Other/>','<Nini><Section/></Nini>',"<Nini><Section Name='s'><Key/></Section></Nini>","<Nini><Section Name='s'><Key Name='x'/></Section></Nini>","<Nini><Section Name='s'><Key Value='v'/></Section></Nini>","<Nini><Section Name='s'><Key Name='x' Value='one'/><Key Name='x' Value='two'/></Section></Nini>","<Nini><Section Name='s'><Key Name='x' Value='first'/></Section><Section Name='s'><Key Name='y' Value='lost'/></Section></Nini>","<Nini xmlns='urn:default'><Section Name='s'><Key Name='x' Value='v'/></Section></Nini>","<p:Nini xmlns:p='urn:p'/>","<Nini xmlns:p='urn:p'><p:Section Name='ignored'/><Section Name='s'><p:Key Name='ignored'/><Key Name='' Value=''/></Section></Nini>","<?xml version='1.0'?><!--before--><Nini><?pi data?><Section Name='s'>text<![CDATA[cdata]]><Key Name='x' Value='a&amp;b &quot;q&quot; &#xA;'/><!--kept--></Section></Nini>","<!DOCTYPE Nini [<!ENTITY x 'expanded'>]><Nini><Section Name='s'><Key Name='x' Value='&x;'/></Section></Nini>","<!DOCTYPE Nini [<!ELEMENT Nini ANY><!ATTLIST Section Name CDATA 'default'>]><Nini><Section/></Nini>",'<Nini>','',"<Nini xml:space=' preserve '> \n<Section Name='s'> <Key Name='x' Value='v'/> </Section> </Nini>","<Nini><Unknown/><Section Name='s'><key Name='ignored' Value='v'/><Key Name='x' Value='v'><Other/></Key></Section></Nini>","<Nini xmlns:p='' broken",None]
 for xml in xmls:
  for mode in [0,1,2]:
   for op in ['LOADR','READER']:add('native-load-document-and-partial-state','NEW 0',setv('old','k','kept'),reader(xml,mode,op),'IOSTATE 0','SAVE 0','RELOAD 0')
 for count in [-1,0,1,2,3,5,20]:add('supplied-reader-current-state-and-ownership','NEW 0',reader(base),f'LOADLAST 0 {count}','IOSTATE 0')
 for name in [None,'','s','other','\u212a','K','\ud800',"quote'\"&"]:
  for value in [None,'','v','\0','\ud800','x\r\ny','<&"','\U0001f600']:add('live-shared-source-key-mutations','NEW 0',reader(base),setv(name,'key',value),'MERGE 0 0','SAVE 0','IOSTATE 0')
 for event in ['a0:'+enc('s'),'c0:'+enc('s'),'k0:'+enc('s')+':'+enc('z')+':'+enc('new')]:
  for mutation in [0,1,2]:add('source-event-reentry-and-partial-state','NEW 0',reader(base),f'FAIL 0 {enc(event)} 1 {mutation}',setv('s','z','new'),reader(base),'IOSTATE 0')
 add('shared-source-owner-retention','NEW 0','NEW 1',setv('foreign','z','first',1),'MERGE 0 1',setv('foreign','z','new'),reader('<Nini/>',1,'READER',1),'IOSTATE 0')
 for xml in xmls:
  if xml is None:continue
  for index in range(len(xml)):add('truncated-source-to-native-flow','NEW 0',reader(xml[:index]),'IOSTATE 0')
 for c in ['\0','\t','\n','\r','\u0085','\u00a0','\ud7ff','\ud800','\udfff','\ue000','\ufffe','\uffff','\U0001f600','\u212a','\u017f','\u0131']:
  for attribute in ['Name','Value']:add('raw-UTF16-config-attribute','NEW 0',reader("<Nini><Section Name='s'><Key Name='k' "+attribute+"='"+c+"'/></Section></Nini>"),'IOSTATE 0')
 for xml in xmls:
  if xml is None:continue
  for codec,bom in [('utf-8',b''),('utf-8',bytes.fromhex('efbbbf')),('utf-16-le',bytes.fromhex('fffe')),('utf-16-be',bytes.fromhex('feff')),('utf-32-le',bytes.fromhex('fffe0000')),('utf-32-be',bytes.fromhex('0000feff'))]:
   payload=bom+xml.encode(codec,'surrogatepass');add('six-encoding-remote-bytes-native-Nini-flow','NEW 0',reader(payload.hex(),1,'LOADB'),'IOSTATE 0')
 for uri in ['', 'https://example.invalid/original/config.xml', 'https://example.invalid/\u00e9/\ud800.xml']:
  for xml in [base,'<Nini>','<Nini><Section Name=\"s\"><Key Name=\"k\" Value=\"\0\"/></Section></Nini>']:
   add('raw-base-URI-error-metadata','NEW 0',reader(xml,1,'LOADU')+' '+enc(uri),'IOSTATE 0')
 if not a.quick:
  rng=random.Random(173811);names=[None,'','s','other','new',"quoted'\"",'\ud800'];keys=[None,'','z','a','x','new'];values=[None,'','v','new','<&"','\0','\ud800','line\r\nnext']
  for _ in range(400):
   ops=['NEW 0',reader(base),'NEW 1']
   for step in range(16):
    op=rng.choice(['SET','RAWADD','REM','DROPSEC','ADD','CLEAR','MERGE','SAVE','RELOAD','AUTO','LOADR','LOADLAST'])
    if op in ['SET','RAWADD']:ops.append(setv(rng.choice(names),rng.choice(keys),rng.choice(values),op=op))
    elif op=='REM':ops.append(f'REM 0 {enc(rng.choice(names))} {enc(rng.choice(keys))}')
    elif op in ['DROPSEC','ADD']:ops.append(f'{op} 0 {enc(rng.choice(names))}')
    elif op=='MERGE':ops.append('MERGE 0 '+str(rng.randrange(2)))
    elif op=='AUTO':ops.append('AUTO 0 '+str(rng.randrange(2)))
    elif op=='LOADR':ops.append(reader(rng.choice(xmls),rng.randrange(2)))
    elif op=='LOADLAST':ops.append('LOADLAST 0 '+str(rng.choice([-1,0,1,2])))
    else:ops.append(op+' 0')
   ops.append('IOSTATE 0');add('deterministic-public-load-merge-state',*ops)
 source=Peer([a.dotnet,str(w/'app/Reference.dll')],w);native=Peer([str(Path(a.native).resolve()),'--probe'],w);diff=[];started=time.monotonic();processed=0
 try:
  for index,row in enumerate(rows):
   expected=source.ask(row);actual=native.ask(row);processed+=1
   if expected!=actual:
    diff.append(dict(index=index,label=labels[index],row=row,source=expected,native=actual))
    if len(diff)>=10:break
 finally:source.close();native.close()
 report=dict(cases=processed,planned=len(rows),elapsed_seconds=time.monotonic()-started,differences=diff,actual_nini_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),scope='Actual Nini.dll vs concrete native reader + retained DOM + Nini class load/merge. Remote DtdIgnore and Prohibit tested. DtdParse, XML serialization and file policy remain separate required closures. No CLR calls or parser substitutions in native code.');(w/'native-document-results.json').write_text(json.dumps(report,indent=2));print(f'{processed-len(diff)}/{processed} actual native document/Nini flows agree');assert not diff,w/'native-document-results.json'
if __name__=='__main__':main()
