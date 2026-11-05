"""TimeSpan.FromHours/TotalHours against the installed .NET8 BCL, by raw bits."""
import argparse,hashlib,json,math,random,struct,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def invoke(command,rows=None):
 result=subprocess.run(command,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110)
 assert result.returncode==0,(result.returncode,result.stdout[-2000:],result.stderr[-2000:])
 return result.stdout.splitlines()
def bits(value):return struct.unpack('<Q',struct.pack('<d',value))[0]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--native',required=True);parser.add_argument('--dotnet',required=True);parser.add_argument('--work',required=True,type=Path);parser.add_argument('--prepare-only',action='store_true');args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,key).text=value
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/time_span_reference.cs'));cs=work/'TimeSpanReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([args.dotnet,'build',str(cs),'-o',str(work/'app'),'--nologo','-v','quiet']);managed=[args.dotnet,str(work/'app/TimeSpanReference.dll')];runtime=invoke(managed+['--metadata']);assert runtime[0].startswith('8.'),runtime
 if args.prepare_only:print('Prepared actual .NET TimeSpan oracle '+runtime[0]);return
 hour_bits={0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x800fffffffffffff,0x0010000000000000,0x8010000000000000,0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000}
 for sign in [0,1<<63]:
  for fraction in [1,2,0xfff,0xffffffffff,0x7ffffffffffff,0x8000000000000,0x8000000000001,0xfffffffffffff]:hour_bits.add(sign|0x7ff0000000000000|fraction)
 for ticks in [0,.125,.5,.999,1,1.001,1.5,2,2.5,9998.5,9999,9999.5,10000,10000.5,10001,20000,10000000,36000000000,1728000000000,2**31,2**32,2**53,2**63]:
  for sign in [-1,1]:
   center=bits(sign*ticks/36000000000.)
   for delta in range(-8,9):
    if 0<=center+delta<2**64:hour_bits.add(center+delta)
 for value in [-48.,-1.,-.016,0.,.016,1.,48.,2**31,-2**31]:
  center=bits(value)
  for delta in range(-4,5):
   if 0<=center+delta<2**64:hour_bits.add(center+delta)
 for sign in [-1,1]:
  center=bits(sign*(2**63/36000000000.))
  for delta in range(-128,129):hour_bits.add(center+delta)
 for exponent in range(-1074,1024,7):
  value=math.ldexp(1.,exponent)
  hour_bits.update([bits(value),bits(-value)])
 rng=random.Random(2311424)
 for _ in range(40000):hour_bits.add(rng.getrandbits(64))
 for _ in range(12000):hour_bits.add(bits(rng.uniform(-1.,1.)*(2**63/36000000000.)))
 tick_bits={0,1,2,3,(1<<63)-1,1<<63,(1<<64)-1}
 for exponent in range(64):
  for sign in [-1,1]:
   for delta in [-1025,-1024,-1023,-2,-1,0,1,2,1023,1024,1025]:
    value=sign*(1<<exponent)+delta
    if -(1<<63)<=value<(1<<63):tick_bits.add(value&((1<<64)-1))
 for _ in range(40000):tick_bits.add(rng.getrandbits(64))
 rows=['h '+format(value,'016x') for value in sorted(hour_bits)]+['t '+format(value,'016x') for value in sorted(tick_bits)];outputs=[invoke(managed,rows),invoke([args.native,'--probe'],rows)]
 assert len(outputs[0])==len(outputs[1])==len(rows),(len(outputs[0]),len(outputs[1]),len(rows))
 differences=[dict(index=i,command=row,source=original,native=native) for i,(row,original,native) in enumerate(zip(rows,*outputs)) if original!=native]
 assembly=Path(runtime[1]);report=dict(passed=not differences,cases=len(rows),fromHoursCases=len(hour_bits),totalHoursCases=len(tick_bits),runtimeVersion=runtime[0],runtimeAssembly=dict(path=str(assembly),sha256=hashlib.sha256(assembly.read_bytes()).hexdigest()),scope='Actual .NET8 BCL TimeSpan.FromHours and TotalHours. FromHours compares exact Int64 ticks or exception type/message/HResult/null parameter; successful results also compare TotalHours Double bits. Raw NaN payloads, signaling/quiet/sign variants, infinities, signed zero, subnormal/sub-tick/sub-millisecond values, positive2^63 special clamp, negative2^63, adjacent ULPs and arbitrary signed Int64 bit patterns are covered. This is the hours/ticks provider required by native Flotsam configuration, not complete TimeSpan parsing/formatting/arithmetic or arbitrary localized exceptions.',differences=differences[:4]);(work/'time-span-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not differences,json.dumps(differences[:3]);print(f'PASS: {len(rows)} .NET8 TimeSpan hours/tick/exception/float-bit observations')
if __name__=='__main__':main()
