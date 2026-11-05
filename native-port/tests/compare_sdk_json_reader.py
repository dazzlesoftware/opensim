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
seeds=['',' ','{}','[]','null','true','false','"text"',"'text'",'0','-0','1','1 ','-1','1.0','1e0','1e+','[1e+]','[9223372036854775808]','[18446744073709551615]','[18446744073709551616]','[-9223372036854775809]','[9223372036854775808x]','[null,true,false,{},[]]','{"x":1,"x":2}','{"x":"\\ud800","\\udc00":null}',"{'key':'value'}",'/*comment*/{"x":[-1.5e+2,0]}//tail','{"x":1}junk','{"x":1}{}','["\\q"]','["\\u0000"]','["\\uZZZZ"]','["a\x00b"]','["a\nb"]','[01]','[+1]','[1,]','{"x":}','{"x" 1}','{1:2}','[/*x*/1]','//comment','/*','[','{','["','{"x":']
for seed in seeds:
 add(seed)
 for i in range(len(seed)+1):add(seed[:i])
 for c in ['\x00',' ','x','{','}',':','/','\uffff','\n']:add(seed+c)
for template in ['{}','[]','[1]','{"a":1}','["s"]','[true]','[null]','[1e+2]','/*x*/{}']:
 for code in range(128):
  for i in range(len(template)+1):add(template[:i]+chr(code)+template[i:])
for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\x00\x00'),('utf-32-be',b'\x00\x00\xfe\xff')]:
 for seed in seeds:
  raw=bom+seed.encode(encoding);add(raw)
  for i in range(len(raw)+1):add(raw[:i])
for code in range(65536):add(b'\xff\xfe'+code.to_bytes(2,'little'))
rng=random.Random(1011);alphabet='{}[]:,"\'\\/0123456789-+eE. truefalsnuxABC \n\r\t\x00'
for i in range(10000):add(''.join(rng.choice(alphabet) for _ in range(rng.randrange(40))))
for depth in range(1,65):
 for value in ['[]','{}','null','""','0','true']:
  add('['*depth+value+']'*depth)
  add('{"a":'*depth+value+'}'*depth)
rows='\n'.join(x.hex() for x in cases)+'\n';outputs={}
for name,cmd in [('reference',[a.dotnet,str(work/'app/Reference.dll'),'--json']),('native',[a.native,'--json'])]:
 with (work/(name+'.txt')).open('w',encoding='utf-8') as output:r=subprocess.run(cmd,input=rows,stdout=output,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(work/(name+'.txt')).read_text(encoding='utf-8').splitlines();assert len(outputs[name])==len(cases),(name,len(outputs[name]),len(cases))
diffs=[dict(index=i,input=cases[i].hex(),reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not diffs,scenarios=len(cases),differenceCount=len(diffs),differences=diffs[:20],applicationParityVerified=False,scope='Actual SDK DeserializeJson(Stream) with bundled LitJson lexer/reader/mapper; exact typed trees, JSON reserialization and UTF16 error messages, including NUL/EOF/invalid escapes, numeric EOF/overflow/type-cast precedence, all BMP root characters, malformed prefixes/randomized inputs and BOM UTF8/16/32 streams. Invariant culture, finite bounded corpus; native nesting limit512, general reader/JsonData/object/reflection/stream/culture/runtime/macOS/application parity remain incomplete.')
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert not diffs,str(len(diffs))+' SDK JSON reader differences; see results.json';print('PASS:',len(cases),'original SDK JSON reader scenarios')
