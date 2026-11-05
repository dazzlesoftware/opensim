# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
from pathlib import Path
import argparse,subprocess,json,hashlib,random,itertools,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,key).text=value
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/'native-port/tests/sdk_llsd_xml_reference.cs'))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(work/'Reference.csproj');run=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);(work/'build.txt').write_text(run.stdout+run.stderr,encoding='utf-8');assert run.returncode==0,run.stdout+run.stderr
cases=[]
def add(value):cases.append(value.encode('utf-8') if isinstance(value,str) else value)
json_values=['',' ','{}','[]','null','true','false','0','1','-1','"text"','{"items":[]}','{"x":""}','{"x":"é中😀"}','{"x":"\\ud800"}','{"x":"\\udc00"}','{"\\ud800":1,"\\udc00":2,"\\ufffd":3}','[2147483648,-9223372036854775808,1.25,-0.0]','{"x":1,"x":2}',"{'x':'a'}",'/*comment*/{"x":1}','{"x":1} trailing','{"x":1}\x00','{"x":1,}','[1,]','[1e309,-1e309]']
for value in json_values:
 add(value)
 for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\x00\x00'),('utf-32-be',b'\x00\x00\xfe\xff')]:
  raw=bom+value.encode(encoding);add(raw)
  for i in range(len(raw)+1):add(raw[:i])
for header,body in [('<llsd>','<map><key>x</key><integer>1</integer></map></llsd>'),('<?xml version="1.0"?>','<llsd><array/></llsd>'),('<? llsd/xml ?>','<llsd><array/></llsd>'),('<? llsd/notation ?>',"{'items':[]}"),('<? llsd/binary ?>','!')]:
 add(header+body)
 for i in range(len(header)+1):add(header[:i]+'\0'+header[i:]+body+'\0\0\0')
 for variant in [header.upper(),header.lower(),header.title(),' '+header,'\ufeff'+header,header.replace(' ','\t'),header.replace(' ','\x00')]:add(variant+body+'\0\0\0')
for value in ['<llsd><array/></llsd>',"<? llsd/notation ?>{'items':[]}","<? llsd/binary ?>!"]:
 for encoding,bom in [('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]:add(bom+value.encode(encoding))
for raw in [b'\xff',b'\xed\xa0\x80',b'\xf0\x80\x80\x80',b'\xe2\x82',b'\0',bytes(range(256))]:add(b'{"x":"'+raw+b'"}')
for code in range(65536):add('{"x":"\\u%04x","\\u%04x":1}'%(code,code))
rows='\n'.join(x.hex() for x in cases)+'\n';outputs={}
for name,cmd in [('reference-text',[a.dotnet,str(work/'app/Reference.dll'),'--text']),('native-text',[a.native,'--text']),('reference-bytes',[a.dotnet,str(work/'app/Reference.dll'),'--bytes']),('native-bytes',[a.native,'--bytes'])]:
 r=subprocess.run(cmd,input=rows,capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stderr;(work/(name+'.txt')).write_text(r.stdout,encoding='utf-8');outputs[name]=['EX' if x.startswith('EX:') else x for x in r.stdout.splitlines()];assert len(outputs[name])==len(cases),(name,len(outputs[name]),len(cases))
diffs=[dict(overload=kind,index=i,input=cases[i].hex(),reference=x,native=y) for kind in ['text','bytes'] for i,(x,y) in enumerate(zip(outputs['reference-'+kind],outputs['native-'+kind])) if x!=y]
report=dict(passed=not diffs,scenarios=len(cases)*2,differenceCount=len(diffs),differences=diffs[:20],applicationParityVerified=False,scope='Actual OSDParser.Deserialize(string) and Deserialize(byte[]), invariant-ignore-case format sniff; UTF16 text readers differ from BOM-aware byte notation/XML readers and raw UTF8 byte JSON. String inputs are UTF8-decoded before invocation. Full typed trees plus exact reserialized JSON strings, all BMP escaped values/map keys, raw invalid bytes, malformed encodings. Parse exceptions normalized to EX; diagnostic type/message, seekable positioning API, other cultures, notation quoted EOF loop and application/macOS parity incomplete.')
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert not diffs,str(len(diffs))+' automatic OSD stream differences; see results.json';print('PASS:',len(cases)*2,'original SDK OSD string/byte overload scenarios')
