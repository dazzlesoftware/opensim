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
def add(text):
 if isinstance(text,str) and text.startswith(('[','{','<',' \t')):text+='\0\0\0'
 cases.append(text.encode('utf-8') if isinstance(text,str) else text)
values=['',' ','!','0','1','t','T','f','F','true','TRUE','false','FALSE','tr','tru','fa','fals','True','False','i','i-','i+1','i123','i-2147483648','i2147483648','i999999999999999999999','i１２٣','r','r0','r-0','r+1.25e-2','r1e309','r1,234','rNaN','rInfinity','r1.2.3','r１２٣','u00000000-0000-0000-0000-000000000001','u123','u'+'x'*36,'b','b(','b16"00"','b64""','b64"aGVsbG8="','b64"bad"','b64XaGVsbG8="','b99""','bX','b٦٤""','s(0)""','s(3)"abc"','s3XabcY','s(2)X😀Z','s(2147483648)"x"','s(-1)"x"',"''",'""',"'hello'",'"a\\n\\t\\x41\\q"','"\\xGG"','"\\x0z"',"'a\x00b'",'l""','l"http://example.com/a b"','l"http://[bad"','d"1970-01-01T00:00:00Z"','d"2020-02-29T12:34:56.1234567Z"','d"bad"','d"0001-01-01"','[]','{}','[!]','[!,]','[! !]','[!x!]','[\x00','{\x00',"{'a':i1,'b':i2,'a':i3}","{i1:i2}","{'a';i1}","{'a':}","{'a':i1x'b':i2}"]
for value in values.copy():
 if '\x00' not in value:values.extend(['['+value+']',"{'x':"+value+'}'])
for value in values:
 for header in ['', '<? llsd/notation ?>\n','<? llsd/notation ?>','<? LLSD/NOTATION ?>\r\n','<123456789012345678>']:
  add(header+value)
 for i in range(len(value)+1):add(value[:i]+'\0\0\0')
 for suffix in [' junk','\0',' ']:add(value+suffix)
for code in range(128):
 c=chr(code);add(c+'\0\0\0');add('i'+c+'\0');add('r'+c+'\0');add('"\\'+c+'"\0');add('"'+c+'"\0')
for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\x00\x00'),('utf-32-be',b'\x00\x00\xfe\xff')]:
 for value in ['<? llsd/notation ?>{\'items\':[]}',"'é中😀'",'i１２٣','[true,False]']:add(bom+(value+'\0\0\0').encode(encoding))
rng=random.Random(929)
for i in range(3000):
 value=str(rng.randrange(-1<<63,1<<63));add('i'+value);add('r'+str(rng.uniform(-1e12,1e12)))
# The SDK uses Char.IsDigit, but adds the UTF16 code unit minus ASCII '0'.
for code in range(65536):add(('i'+chr(code)+'\0').encode('utf-8','surrogatepass'))
(work/'cases.json').write_text(json.dumps([x.hex() for x in cases]));rows='\n'.join(x.hex() for x in cases)+'\n';outputs={}
for name,cmd in [('reference',[a.dotnet,str(work/'app/Reference.dll'),'--notation']),('native',[a.native,'--notation'])]:
 
 with (work/(name+'.txt')).open('w',encoding='utf-8') as output:r=subprocess.run(cmd,input=rows,stdout=output,stderr=subprocess.PIPE,text=True,timeout=90)
 assert r.returncode==0,r.stderr;outputs[name]=(work/(name+'.txt')).read_text(encoding='utf-8').splitlines();assert len(outputs[name])==len(cases),(name,len(outputs[name]),len(cases))
diffs=[dict(index=i,input=cases[i].hex(),reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not diffs,scenarios=len(cases),differenceCount=len(diffs),differences=diffs[:20],applicationParityVerified=False,sourceEofQuotedLoopEquivalent=False,scope='Actual bundled SDK DeserializeLLSDNotation(Stream): typed ordered trees and errors, StreamReader encodings/BOM, numeric wrap, separators/boolean/header quirks, escapes/UUID/binary/URI/date. Prefixes terminated with NUL to stop source EOF-to-char allocation loop. Native throws on unterminated quoted EOF; known parity difference, full SDK/application/macOS incomplete.')
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert not diffs,str(len(diffs))+' notation LLSD differences; see results.json';print('PASS:',len(cases),'original SDK LLSD notation scenarios')
