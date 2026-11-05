"""Actual Nwc deserializer over live TextReader/StreamReader input boundaries."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/nwc_xml_rpc_reference.cs','bin/XMLRPC.dll']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/sources[0]));ref=ET.SubElement(items,'Reference',Include='XMLRPC');ET.SubElement(ref,'HintPath').text=str(root/'bin/XMLRPC.dll')
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
enc=lambda s:s.encode('utf-16-le','surrogatepass').hex() or '.'
def response(x):return '<methodResponse><params><param><value>'+x+'</value></param></params></methodResponse>'
docs=['',' ','<a/>','<a>','<a>\x00</a>','<a>&bad;</a>',response('text'),response('<string>😀é\r\nx</string>'),response('<int>7</int>'),response('<int>bad</int>'),response('<array><data><value/><value><string/></value><value><base64></base64></value></data></array>'),response('<string>'+('x'*4200)+'</string>'),'<a>\x00'+('x'*4500)+'</a>']
docs += ['<?xml version="1.0" encoding="'+e+'"?>'+response('<string>é😀</string>') for e in ['utf-8','utf-16','iso-8859-1','not-an-encoding']]
docs += ['<!DOCTYPE methodResponse [<!ENTITY x "test">]>'+response('&x;'),response(chr(0xd800)),response('<double>1e300</double>')]
commands=[]
def add(mode,chunk,fail,close,flags,payload):commands.append(f'S {mode} {chunk} {fail} {close} {flags} {payload or "."}')
for doc in docs:
 for chunk in [1,2,7,64,1024,4096]:
  for fail in [0,1,2,5]:
   for close in [0,1]:
    for flags in [0,4,8,12]:add(0,chunk,fail,close,flags,enc(doc))
 # BOM decoding precedes XML reader, so declarations never change encoding.
 for codec,bom in [('utf-8',b''),('utf-8',bytes.fromhex('efbbbf')),('utf-16-le',bytes.fromhex('fffe')),('utf-16-be',bytes.fromhex('feff')),('utf-32-le',bytes.fromhex('fffe0000')),('utf-32-be',bytes.fromhex('0000feff'))]:
  data=bom+doc.encode(codec,'surrogatepass')
  for chunk in [1,2,3,7,1024,4096]:
   for fail,close,flags in [(0,0,0),(0,0,8),(0,1,12),(1,0,8),(2,1,4)]:add(1,chunk,fail,close,flags,data.hex())
for mode in [0,1]:
 for flags in [1,1|4,1|8,2,2|8,16,16|4]:add(mode,7,0,0,flags,enc('<a/>') if mode==0 else '<a/>'.encode().hex())
outputs={}
for label,cmd in [('source',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=240);assert r.returncode==0,r.stderr+r.stdout[-1000:];outputs[label]=r.stdout.splitlines();assert len(outputs[label])==len(commands),(label,len(outputs[label]),len(commands))
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,outputs['source'],outputs['native']) if s!=n]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:25],applicationParityVerified=False,scope='Actual Nwc XMLRPC.dll deserializer over controlled segmented TextReader or real default StreamReader over segmented byte Stream. Compare exact read counts/sizes and logger/close ordering, typed values/identity and errors, early malformed input, read/close failure replacement and reuse after failures, raw UTF16 and UTF8/16/32 BOM/defaultUTF8 replacement, ignored text-reader XML encoding declarations. Native live text-input bridge and existing StreamReader new buffered span read. General custom TextReader char-array identity/overrides, HTTP/socket and full host/application/macOS remain pending.',sourceHashes={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources})
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,str(len(diffs))+' Nwc streaming differences; see '+str(w/'results.json');print('PASS:',len(commands),'bundled Nwc streaming scenarios')
