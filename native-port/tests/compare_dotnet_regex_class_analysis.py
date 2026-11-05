"""Compare RegexCharClass optimizer helpers, partial outputs, and string identities."""
from pathlib import Path
import argparse,hashlib,json,random,subprocess,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);lab=Path(__file__).resolve().parent
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(lab/'dotnet_regex_class_analysis_reference.cs'));ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);(w/'build.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
ref=[a.dotnet,str(w/'app/Reference.dll')];r=subprocess.run(ref+['--classes'],capture_output=True,text=True,check=True);(w/'runtime-classes.json').write_text(r.stdout);classes=json.loads(r.stdout);assert len(classes)==67
def enc(s):return '-' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '.'
def cls(ranges=(),categories=(),negative=0,sub=''):return chr(negative)+chr(len(ranges))+chr(len(categories))+''.join(chr(c&65535) for c in [*ranges,*categories])+sub
sets={bytes.fromhex(value).decode('utf-16-be','surrogatepass') for value in classes}
for bounds in [[],[0],[65535],[0,1],[0,128],[0,65535],[1,2],[65,91],[127,128],[128,129],[65,66,97,98],[0,1,65535],[0,128,256,512]]:
 for neg in [0,1]:sets.add(cls(bounds,negative=neg));sets.add(cls(bounds,negative=neg,sub=cls([65,91])))
for group in [[0,1,2,0],[0,-1,-2,0],[1,-2],[-1,2],[100],[-100],[0,100,0],[0,-100,0],[0],[0,0],[0,1],[1],[0,1,0,0,-2,0]]:
 for neg in [0,1]:sets.add(cls(categories=group,negative=neg))
sets.update(['','\0','\1','\2','\2\0\0','\0\0','\0\u0004\0a','\0\0\u0002\u0001',cls([65535,0]),'\0\u0002\0\uffff\0'])
ordered=[None]+sorted(sets);rows=['W']
for set in ordered:
 value=enc(set);rows.extend(['A '+value,'F '+value,'R '+value]);rows.extend('K '+value+' '+str(cap) for cap in [0,1,2,5,30]);rows.extend('T '+value+' '+str(cap) for cap in [0,1,2,16,128])
for left in ordered:
 for right in ordered:rows.append('O '+enc(left)+' '+enc(right))
for value in ordered:
 for offset in [-2147483648,-1,0,1,2,3,4,2147483647]:rows.append('N '+enc(value)+' '+str(offset))
for code in range(65536):rows.append('H '+enc(chr(code)))
for value in [None,'','Aa','AA','aa','aA','Zz','00','A\uffff','\uffffA','\uffff\uffff','aba','é中','\ud800\udfff','x'*32768]:rows.append('H '+enc(value))
for code in range(65,91):
 upper,lower=chr(code),chr(code+32);pairs=[upper+lower,lower+upper,upper+upper,lower+lower]
 for left in pairs:
  for right in pairs:rows.append('I '+enc(left)+' '+enc(right))
for left,right in [(None,''),('',''),('A','A'),('é','é'),('\uffff','\uffff'),('00','00'),('Aa','Bb'),('Kk','KK')]:rows.append('I '+enc(left)+' '+enc(right))
for value in [-2147483648,-65537,-65536,-1,65536,65537,2147483647]:rows.append('J '+str(value))
for code in range(65536):rows.append(f'D {code} 0')
rng=random.Random(8031)
for i in range(512):rows.append(f'D {rng.randrange(65536)} {rng.randrange(65536)}')
(w/'input.txt').write_text('\n'.join(rows)+'\n');outputs={}
for name,cmd in [('reference',ref),('native',[str(Path(a.native).resolve())])]:
 r=subprocess.run(cmd,input='\n'.join(rows)+'\n',capture_output=True,text=True,timeout=180);(w/(name+'.txt')).write_text(r.stdout);(w/(name+'-error.txt')).write_text(r.stderr);assert r.returncode==0,(name,r.stderr);outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(rows),(name,len(outputs[name]),len(rows))
differences=[dict(index=i,input=rows[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y];report=dict(passed=not differences,flows=len(rows),sets=len(ordered),parserClasses=len(classes),unicodePredicates=65536,oneCharSerializationFlows=65536,oneBitFlows=65536+512,differences=len(differences),firstDifferences=differences[:10],applicationParityVerified=False,scope='Actual .NET class singleton/range/category extraction, conservative Analyze/MayOverlap, nullable/error order, partial caller buffers, Unicode case-conversion/ASCII predicates and char serialization with cache identities. Full regex parser/runner and login std::regex replacement remain unfinished.',outputSHA256={name:hashlib.sha256(('\n'.join(value)+'\n').encode()).hexdigest() for name,value in outputs.items()});(w/'results.json').write_text(json.dumps(report,indent=2));assert report['passed'],report;print(f'PASS: {len(rows)} actual .NET regex class analysis flows')
