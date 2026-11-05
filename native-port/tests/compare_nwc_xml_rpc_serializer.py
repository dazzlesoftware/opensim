"""Actual bundled Nwc XML-RPC serializers, XML writer and UTF8Encoding wire bytes."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,random,struct
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/nwc_xml_rpc_serializer_reference.cs','bin/XMLRPC.dll']
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/sources[0]));ref=ET.SubElement(items,'Reference',Include='XMLRPC');ET.SubElement(ref,'HintPath').text=str(root/'bin/XMLRPC.dll')
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
enc=lambda s:'-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
texts=[None,'','text 😀 <&>"\r\n','a\x00b',chr(0xd800),chr(0xdc00),chr(0xd800)+'q','a'+chr(0xd800)+chr(0xd800)+chr(0xdc00),'a\x01\x1f\x7f','x'*1023+chr(0xd800), 'x'*1023+'😀'+'q','x'*2048,'e\u0301\u2000']
atoms=[(0,None)]+[(1,s) for s in texts]+[(2,s) for s in ['0','-1','2147483647','-2147483648']]+[(3,s) for s in ['True','False']]+[(4,s) for s in ['0','-0','1.5','-31','1E-5','1E16','1E17','1.2345678901234567','1E200','NaN','Infinity','-Infinity']]+[(5,s) for s in ['0','-1','9223372036854775807']]+[(k,s) for k in [6,7] for s in texts[:9]]+[(8,s) for s in ['','00','ff','00ff','00ff10','ab'*1023]]+[(9,s) for s in ['0','1','621355968000000000','638500815150000000','3155378975999999999']]+[(10,'1.5'),(11,'')]
commands=[]
def add(mode=0,shape=0,flags=0,kind=1,payload='',method='avatar_properties_request'):commands.append(f'{mode} {shape} {flags} {kind} {enc(payload)} {enc(method)}')
for mode in range(3):
 for shape in range(4):
  for flags in [0,1,2,4,16,20]:
   for kind,payload in atoms:add(mode,shape,flags,kind,payload)
for method in [None,'','.','a.b','a.b.c','.b','a.','raw 😀<&','a\x00b',chr(0xd800),chr(0xdc00),'a\r\nb']:
 for mode in [0,1]:
  for flags in [0,1,2,16]:
   for payload in texts[:8]:add(mode=mode,flags=flags,payload=payload,method=method)
rng=random.Random(117)
for i in range(300):
 bits=rng.getrandbits(64);v=struct.unpack('<d',bits.to_bytes(8,'little'))[0];s=repr(v)
 if s=='inf':s='Infinity'
 if s=='-inf':s='-Infinity'
 add(mode=i%3,shape=i%4,flags=16*(i%2),kind=4,payload=s)
outputs={}
for label,cmd in [('source',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert r.returncode==0,r.stderr+r.stdout[-1000:];outputs[label]=r.stdout.splitlines();assert len(outputs[label])==len(commands),(label,len(outputs[label]),len(commands))
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,outputs['source'],outputs['native']) if s!=n]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:20],applicationParityVerified=False,scope='Actual bundled XMLRPC.dll request/response/boxed-object serializers and request method-name getters, response constructors/SetFault/value-setter reset/fault accessors. Exact UTF8-no-BOM compact wire and diagnosticIndented4 bytes, close/partial failures/type+message, null request/parameters/method/string, all supported wire scalar/container types, unsupported boxed Int64/Single/object omission, DateTime ticks and invariant double formatting, raw UTF16/invalid pairs/control characters/stream buffer boundaries, deterministic IDictionary.Keys enumeration. Source uses real OrderedDictionary for specified order; native Value.map_order retains it. Arbitrary Hashtable randomized order, non-ArrayList IList strict-cast APIs, invalid UTF16 dictionary keys, general culture/custom virtual collection mutation and HTTP/XMLRPCRequester/full host/application/macOS remain pending.',sourceHashes={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources})
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert not diffs,str(len(diffs))+' Nwc serializer differences; see '+str(w/'results.json');print('PASS:',len(commands),'bundled Nwc serializer scenarios')
