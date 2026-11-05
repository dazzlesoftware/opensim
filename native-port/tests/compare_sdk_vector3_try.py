"""Bit-exact raw UTF16 SDK Vector3.TryParse comparison."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,itertools
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/'native-port/tests/sdk_vector3_try_reference.cs'))
for name in ['OpenMetaverse','OpenMetaverseTypes']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
values=[None,'','0,0,0','<0,0,0>','<1,2,3>','<1,2,3 >suffix','junk<1,2,3>','1,2,3,4','1234567','1234567<','<1234567','1,2,','<1,2,3junk>','<1,2, 3>','<1,2,3\x00>']
values += ['<'+','.join(row)+'>' for row in itertools.product(['0','-0','+0','.5','1.401298464324817e-45','7.006492321624085e-46','3.4028235677973366e38','1e100','-1e-100','NaN','-NaN','nAn','∞','-∞','Infinity','\t1','1\x00','1\x00\x00','\u2000NaN','\u20001','\ud800','1,000'],repeat=3)]
for unit in range(65536):
 for position in range(3):
  row=['1','2','3'];row[position]=chr(unit);values.append('<'+','.join(row)+'>')
commands=['-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.' for s in values];outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr;outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(input=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n];report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,assemblySha256=hashlib.sha256((root/'bin/OpenMetaverseTypes.dll').read_bytes()).hexdigest(),scope='Actual SDK Vector3.TryParse(string) raw UTF16/null and exact bool/IEEE single output bits. All BMP code units in each coordinate, malformed delimiters/surrogates, finite rounding/subnormal/overflow/special symbols/NUL/whitespace and suffix behavior. No authored expected parser output; whole application/macOS pending.')
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' SDK vector differences';print('PASS:',len(commands),'actual SDK vector TryParse observations')
