"""Bidirectional actual BinaryFormatter AssetBase compatibility and original DecodeFromNRBF."""
import argparse,hashlib,json,random,re,struct,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def call(command,lines):
 r=subprocess.run(command,input='\n'.join(lines)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110);assert r.returncode==0,r.stderr;out=r.stdout.splitlines();assert len(out)==len(lines),(len(out),len(lines),r.stderr);return out

def string(s):
 b=s.encode('utf-8');n=len(b);prefix=bytearray()
 while n>=128:prefix.append((n&127)|128);n>>=7
 return bytes(prefix)+bytes([n])+b

def value_graph(value):
 i=lambda n:struct.pack('<i',n)
 return b'\0'+i(1)+i(-1)+i(1)+i(0)+bytes([12])+i(2)+string('OpenSim.Framework')+bytes([3])+i(1)+string('OpenSim.Framework.AssetBase')+i(2)+string('m_metadata')+string('unused')+i(2)+bytes([3])+i(3)+string('OpenSim.Framework.AssetMetadata')+i(0)+i(2)+value+bytes([11])

def graph(primitive,data):return value_graph(bytes([8,primitive])+data)

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 slutil=ROOT/'OpenSim/Framework/SLUtil.cs';version=ROOT/'OpenSim/Framework/VersionInfo.cs';source=slutil.read_text(encoding='utf-8-sig');conversion=w/'SLUtil.cs';conversion.write_text(source[:source.index('static readonly char[] seps')]+'\n}}\n',encoding='utf-8');v=re.search(r'AssemblyVersionNumber = "([^"]+)"',version.read_text(encoding='utf-8'))[1]
 proj=ET.Element('Project',Sdk='Microsoft.NET.Sdk');pg=ET.SubElement(proj,'PropertyGroup')
 for k,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',AllowUnsafeBlocks='true',EnableUnsafeBinaryFormatterSerialization='true',AssemblyName='OpenSim.Framework',AssemblyVersion=v,NoWarn='SYSLIB0011').items():ET.SubElement(pg,k).text=value
 files=[ROOT/'OpenSim/Framework/AssetBase.cs',ROOT/'OpenSim/Framework/NrbfReader.cs',ROOT/'native-port/tests/asset_nrbf_reference.cs',conversion];ig=ET.SubElement(proj,'ItemGroup')
 for f in files:ET.SubElement(ig,'Compile',Include=str(f))
 ref=ET.SubElement(ig,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll');cs=w/'AssetNrbfReference.csproj';ET.ElementTree(proj).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 managed=[a.dotnet,str(w/'app/OpenSim.Framework.dll')];native=[a.native,'--probe'];profiles=1024;emitted=call(managed,[f'emit {n}' for n in range(profiles)]);assert all('!' in s for s in emitted);blobs=[s.split('!',1)[0] for s in emitted];expected=call(managed,['read '+s for s in blobs]);transcoded=call(native,['transcode '+s for s in blobs]);assert all('!' in s for s in transcoded),[(i,s) for i,s in enumerate(transcoded) if '!' not in s][:3];encoded=[s.split('!',1)[0] for s in transcoded];observed=[s.split('!',1)[1] for s in transcoded];roundtrip=call(managed,['read '+s for s in encoded]);selfread=call(native,['read '+s for s in encoded]);legacy_source=call(managed,['legacy '+s for s in blobs]);legacy_native=call(native,['legacy '+s for s in blobs]);legacy_written=call(managed,['legacy '+s for s in encoded]);diffs=[]
 for name,rows,baseline in [('read original .NET blobs',observed,expected),('native writer to actual BinaryFormatter',roundtrip,expected),('native writer native reader',selfread,expected),('original DecodeFromNRBF',legacy_native,legacy_source),('native writer original DecodeFromNRBF',legacy_written,legacy_source)]:
  diffs += [dict(phase=name,profile=i,expected=x,actual=y) for i,(x,y) in enumerate(zip(baseline,rows)) if x!=y]
 rng=random.Random(89231);malformed=[]
 for n in [0,1,2,3,8,31,32,64,128,255,256,512,777,1023]:
  b=bytes.fromhex(blobs[n]);malformed += [b[:k] for k in range(len(b))]
  for _ in range(80):
   c=bytearray(b);index=rng.randrange(len(c));c[index]^=1<<rng.randrange(8);malformed.append(bytes(c))
 decimals=['','0','-0','1',' 1 ','- 1','1-','1 -','+1-','1,','1,,2','1,.2','1.,2','.1','1.','NaN','Infinity','1e3','1\0','1\0 ','1 \0','\u00a01','1\u00a0','79228162514264337593543950335','79228162514264337593543950336','79228162514264337593543950335.4','79228162514264337593543950335.5','0.'+'0'*100+'1']
 for _ in range(1200):
  digits=''.join(str(rng.randrange(10)) for _ in range(rng.randrange(1,100)));at=rng.randrange(len(digits)+1);decimals.append(rng.choice(['','+','-',' '])+digits[:at]+'.'+digits[at:]+rng.choice(['','+','-',' ',',' ]))
 malformed += [graph(5,string(s)) for s in decimals]
 primitives=[(1,bytes([v])) for v in [0,1,2,255]]+[(2,b'\xff'),(3,b'A'),(3,'é'.encode()),(3,'😀'.encode()),(3,b'\x80a'),(3,b'\xffabc'),(6,struct.pack('<d',float('nan'))),(6,struct.pack('<d',float('inf'))),(7,struct.pack('<h',-32768)),(8,struct.pack('<i',-2147483648)),(9,struct.pack('<q',-9223372036854775808)),(10,b'\x80'),(11,struct.pack('<f',-0.0)),(12,struct.pack('<q',9223372036854775807)),(14,struct.pack('<H',65535)),(15,struct.pack('<I',4294967295)),(16,struct.pack('<Q',18446744073709551615)),(17,b''),(18,string('text <&> 😀'))]
 primitives += [(13,struct.pack('<Q',ticks|(kind<<62))) for ticks in [0,3155378975999999999,3155378976000000000] for kind in range(4)];malformed += [graph(t,b) for t,b in primitives]
 i=lambda n:struct.pack('<i',n)
 # Every source record form, null runs, metadata reuse and depth boundaries.
 records=[bytes([1])+i(6)+i(3),bytes([1])+i(6)+i(999),bytes([2])+i(6)+string('unused')+i(0),bytes([6])+i(8)+string('value 😀'),bytes([9])+i(999),bytes([10]),bytes([12])+i(8)+string('unused library')+bytes([10]),bytes([13,1]),bytes([13,0]),bytes([14])+i(3),bytes([14])+i(0),bytes([15])+i(8)+i(3)+bytes([2,0,255,1]),bytes([16])+i(8)+i(3)+bytes([13,2,6])+i(9)+string('last'),bytes([17])+i(8)+i(3)+bytes([14])+i(2)+bytes([6])+i(9)+string('last'),bytes([0]),bytes([11]),bytes([255])]
 for t in range(8):
  extra=bytes([8]) if t==0 else bytes([2]) if t==7 else string('System.Guid') if t==3 else string('unused')+i(2) if t==4 else b''
  value=i(123) if t==0 else bytes([10])
  records.append(bytes([5])+i(6)+string('unused')+i(1)+string('member')+bytes([t])+extra+i(2)+value)
 for kind,rank in [(0,1),(1,1),(0,2),(0,0)]:records.append(bytes([7])+i(6)+bytes([kind])+i(rank)+i(3)+bytes([0,2,0,255,1]))
 for depth in range(58,69):
  value=bytes([8,1,1])
  for _ in range(depth):value=bytes([2])+i(6)+string('nested')+i(1)+string('child')+value
  records.append(value)
 malformed += [value_graph(r) for r in records]
 commands=['legacy '+b.hex() for b in malformed];left=call(managed,commands);right=call(native,commands);diffs += [dict(phase='legacy truncation/mutation/primitive/decimal',index=i,payload=malformed[i].hex(),expected=s,actual=n) for i,(s,n) in enumerate(zip(left,right)) if s!=n]
 files=[ROOT/'OpenSim/Framework/AssetBase.cs',ROOT/'OpenSim/Framework/NrbfReader.cs',slutil,version,ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not diffs,profiles=profiles,validComparisons=profiles*5,legacyMalformedComparisons=len(commands),sourceHashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Native NRBF AssetBase reader/writer tested against actual original classes, SDK and .NET BinaryFormatter both directions, plus original DecodeFromNRBF. Raw private ID/content type, nullable metadata/strings/arrays, mutable data/SHA alias, distinct equal-content arrays and shared empty-array identity, signed types/flags/upload attempts, Unicode and DateTime kind3 ambiguity retained in file mode; original legacy omissions, setter normalization and graph behavior separately preserved. Truncation, bit mutation, supported record forms/null runs/class reuse/depth boundaries and all reader primitives/decimal validation through actual legacy decoder. File-cache I/O/module integration, arbitrary CLR object graphs/type binding, invalid CLR UTF16/string reference identity, and all platforms remain separate requirements.',differences=diffs[:4]);(w/'asset-nrbf-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:2]);print(f'PASS: {profiles*5} bidirectional actual BinaryFormatter/original decoder comparisons and {len(commands)} legacy malformed/primitive observations')
if __name__=='__main__':main()
