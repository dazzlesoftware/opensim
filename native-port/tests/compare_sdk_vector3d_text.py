"""Bit-exact raw UTF16 SDK Vector3d.TryParse comparison."""
from pathlib import Path
import random,struct,math
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,itertools
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(root/'native-port/tests/sdk_vector3d_text_reference.cs'))
for name in ['OpenMetaverse','OpenMetaverseTypes']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def enc(s):return '-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
values=[None,'','0,0,0','<0,0,0>','<1,2,3>','<1,2,3 >suffix','junk<1,2,3>','1,2,3,4','1234567','1234567<','<1234567','1,2,','<1,2,3junk>','<1,2, 3>','<1,2,3'+chr(0)+'>','<1,2,3','<1,2,3>>','<>','<1,,3>','<,2,3>','<1,2,>']
scalars=['0','-0','+0','.5','4.9406564584124654e-324','2.4703282292062327e-324','2.4703282292062328e-324','1.7976931348623157e308','1.7976931348623159e308','1e9999','-1e-9999','NaN','-NaN','nAn',chr(0x221e),'-'+chr(0x221e),'Infinity',chr(9)+'1','1'+chr(0),'1'+chr(0)*2,chr(0x2000)+'NaN',chr(0x2000)+'1',chr(0xd800),'1,000']
values += ['<'+','.join(row)+'>' for row in itertools.product(scalars,repeat=3)]
for unit in range(65536):
 for position in range(3):
  row=['1','2','3'];row[position]=chr(unit);values.append('<'+','.join(row)+'>')
 values += ['<'+chr(unit)+'1,2,3>','<1'+chr(unit)+',2,3>']
commands=['P '+enc(s) for s in values];del values
rng=random.Random(74391)
def fmt(bits):return 'F '+' '.join(f'{n:016x}' for n in bits)
for sign in range(2):
 for exponent in range(2048):
  for mantissa in [0,1,(1<<52)-1,1<<51,rng.getrandbits(52)]:
   bits=(sign<<63)|(exponent<<52)|mantissa;commands.append(fmt([bits,bits^0x8000000000000000,0]))
for i in range(65536):commands.append(fmt([rng.getrandbits(64) for _ in range(3)]))
for i in range(4096):
 bits=rng.getrandbits(64);number=struct.unpack('>d',bits.to_bytes(8,'big'))[0]
 if math.isfinite(number):commands.append('P '+enc('<'+repr(number)+', -0, 0.1>'))
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr;outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(input=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n];report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,assemblySha256=hashlib.sha256((root/'bin/OpenMetaverseTypes.dll').read_bytes()).hexdigest(),scope='Actual SDK Vector3d.TryParse(string) raw UTF16/null, exact bool/IEEE double output bits and SDK ToString UTF16. All BMP code units alone in each coordinate and before/after numeric x; scalar cross-products, malformed delimiters/surrogates, finite rounding/subnormal/overflow/special symbols/NUL/whitespace/suffix, random finite decimal inputs. Formatting sweeps every double binary exponent/sign with edge/random mantissas and65536 random bit triples. No authored expected parser/formatter output; full application/macOS pending.')
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' SDK vector differences';print('PASS:',len(commands),'actual SDK double vector parse/format observations')
