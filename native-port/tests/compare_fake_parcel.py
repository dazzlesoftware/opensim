"""Bit-exact raw UTF16 SDK Vector3d.TryParse comparison."""
from pathlib import Path
import random,struct,math
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,itertools
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
source=root/'OpenSim/Framework/Util.cs'
text=source.read_bytes().decode('utf-8-sig')
def extract(sig):
 start=text.index(sig);i=text.index('{',start)+1;depth=1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[start:i]
(w/'Extracted.cs').write_text('using OpenMetaverse;class Original{'+''.join(extract(sig) for sig in ['public static UUID BuildFakeParcelID(ulong regionHandle, uint x, uint y)','public static UUID BuildFakeParcelID(ulong regionHandle, uint x, uint y, uint z)','public static bool ParseFakeParcelID(UUID parcelID, out ulong regionHandle, out uint x, out uint y)','public static void ParseFakeParcelID(UUID parcelID, out ulong regionHandle, out uint x, out uint y, out uint z)'])+'}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(w/'Extracted.cs'));ET.SubElement(items,'Compile',Include=str(root/'native-port/tests/fake_parcel_reference.cs'))
for name in ['OpenMetaverse','OpenMetaverseTypes']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
rng=random.Random(83219)
handles=[0,(1<<64)-1,0x10000000200,0x100000000]+[1<<i for i in range(64)]
coords=[0,1,255,256,16383,16384,65535,65536,4294967295]
commands=[f'B {h:016x} {x:08x} {y:08x} {z:08x}' for h in handles for x,y,z in itertools.product(coords,repeat=3)]
commands += [f'B {h:016x} {x:08x} {y:08x} -' for h in handles for x,y in itertools.product(coords,repeat=2)]
for byte in range(16):
 for n in range(256):
  data=bytearray(16);data[byte]=n;commands.append('P '+data.hex())
for i in range(65536):
 commands.append('P '+rng.getrandbits(128).to_bytes(16,'big').hex())
 commands.append(f'B {rng.getrandbits(64):016x} {rng.getrandbits(32):08x} {rng.getrandbits(32):08x} {rng.getrandbits(32):08x}')
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr;outputs[name]=r.stdout.splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(input=c,source=s,native=n) for c,s,n in zip(commands,outputs['reference'],outputs['native']) if s!=n];report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,assemblySha256=hashlib.sha256((root/'bin/OpenMetaverseTypes.dll').read_bytes()).hexdigest(),sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),scope='All four unchanged original Util.BuildFakeParcelID/ParseFakeParcelID overloads compiled against bundled actual SDK UUID/Utils. Compare UUID text, all16 wire bytes, validation bool and handle/x/y/z on valid/invalid inputs. Boundary and full-width coordinate truncation, every handle bit, all byte values in each UUID position, random raw UUIDs and build inputs. Full application/macOS pending.')
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' SDK vector differences';print('PASS:',len(commands),'original fake parcel build/parse observations')
