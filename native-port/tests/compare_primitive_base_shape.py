"""Actual full PrimitiveBaseShape methods with explicit SDK/XML dependency boundaries."""
import argparse,hashlib,itertools,json,random,struct,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def invoke(cmd,data=None):
 r=subprocess.run(cmd,input=data,capture_output=True,text=True,encoding='utf-8',timeout=240)
 assert r.returncode==0,(r.returncode,r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultItems='false',AllowUnsafeBlocks='true').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for file in [ROOT/'OpenSim/Framework/PrimitiveBaseShape.cs',ROOT/'native-port/tests/primitive_base_shape_reference.cs']:ET.SubElement(group,'Compile',Include=str(file))
 for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','log4net','System.Configuration.ConfigurationManager']:
  ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/f'bin/{name}.dll')
 cs=w/'PrimitiveBaseShapeReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[];groups={};last=0
 def add(row):rows.append(row)
 def mark(name):
  nonlocal last
  groups[name]=len(rows)-last;last=len(rows)
 def data(b):return bytes(b).hex() or '-'
 rng=random.Random(886421)
 for k in range(8):
  for n in [-2147483648,-1,0,1,2,3,4,5,6,7,8,9,128,255,2147483647]:add(f'factory {k} {n}')
 for v in range(256):add(f'seed|profile {v}')
 for h,p in itertools.product(range(256),repeat=2):add(f'rawprofile {h} {p}')
 mark('factoriesAndAllProfileBytes')
 for type,entry,enable in itertools.product(range(256),range(2),range(2)):add(f'seed|sculpt {type} {entry}|animesh {enable}')
 for bits,enable in itertools.product([0,1,2,3,255,256,257,0x12345678,0x80000000,0xffffffff],range(2)):add(f'seed|read 5 0 0 {struct.pack("<I",bits).hex()}|animesh {enable}')
 mark('animeshAndFlagPreservation')
 for k,length in [(1,17),(2,16),(3,16),(4,28),(5,4),(6,9)]:
  for n,pos in itertools.product(range(length+5),[0,1,4,32,2147483647]):
   for initial in ['', 'seed|']:
    b=rng.randbytes(n);add(f'{initial}read {k} {pos} 0 {data(b)}')
  for pos in [0,1,4]:add(f'seed|read {k} {pos} 0 null')
  for _ in range(1000):
   n=length+rng.randrange(12);pos=rng.randrange(n-length+1);add(f'seed|read {k} {pos} 0 {rng.randbytes(n).hex()}')
 mark('allExtraRecordReadersAndLengthBoundaries')
 for k,n in [(1,17),(2,16),(3,16),(4,28),(5,4),(6,9)]:
  for index,value in itertools.product(range(n),range(256)):
   b=bytearray(n);b[index]=value;add(f'seed|read {k} 0 0 {b.hex()}')
 mark('everyExtraRecordByteValue')
 for type,use,n in itertools.product([0,16,32,48,64,96,112,128,144,255,256,65535],range(2),[0,1,3,4,8,9,15,16,17,18,28,32]):
  b=bytearray(rng.randbytes(n))
  if type==128 and n>17:b[0]=0
  add(f'seed|update {type} {use} {data(b)}')
 for type,use in itertools.product([0,16,32,48,64,96,112,128,144,65535],range(2)):add(f'seed|update {type} {use} null')
 mark('extraUpdatesEnableDisableUnknownNull')
 for count,size,padding in itertools.product([0,1,2,7,127,128,255],[-2147483648,-1,0,1,16,17,18,35,128,4353,2147483647],[0,1,4]):
  b=bytes(padding)+bytes([count])+rng.randbytes(17*count)
  add(f'seed|read 7 {padding} {size} {b.hex()}')
 for size in [-2147483648,-1,0,17,2147483647]:add(f'seed|read 7 0 {size} null')
 mark('renderMaterialsCountSizeAndOffsets')
 for initial in ['', 'seed|']:
  for b in [None,b'',b'\0',b'\1',b'\2',b'\xff',bytes([2,0,0]),bytes([255,255]),bytes([1,128,0,0,0,0,0]),bytes([2,128,0,18,0,0,0,1])+bytes(17)+bytes(20)]:
   add(f'{initial}read 0 0 0 '+('null' if b is None else data(b)))
  for type,length in [(16,16),(32,16),(48,17),(64,28),(96,17),(112,4),(144,9)]:
   for n in range(length+4):
    b=bytes([1,type,255,255,255,255,255])+rng.randbytes(n);add(f'{initial}read 0 0 0 {b.hex()}')
 for _ in range(1000):
  b=bytes([rng.randrange(8)])+bytes(rng.choice([x for x in range(256) if x!=128]) for _ in range(rng.randrange(64)));add(f'seed|read 0 0 0 {data(b)}')
 mark('wholeExtraParserResetUnknownTypeTruncationAndIgnoredHeaders')
 floatbits=[0,0x80000000,1,0x80000001,0x3f000000,0x3f800000,0x3f800001,0xbf800000,0x7f800000,0xff800000,0x7fc00000,0xffc01234,0x7f800001,0x4f000000,0xcf000000,0x7f7fffff,0xff7fffff]
 floatbits += [rng.randrange(1<<32) for _ in range(4000)]
 for channel,bits in itertools.product(range(4),floatbits):add(f'color {channel} {bits:08x}')
 for index,bits in itertools.product(range(18),floatbits[:1017]):add(f'seed|float {index} {bits:08x}')
 for _ in range(2000):
  values=[rng.randrange(1<<32) for _ in range(4)];add('seed|meshkey '+' '.join(f'{v:08x}' for v in values)+' '+str(rng.randrange(2)))
 for bits,convex in itertools.product(floatbits[:17],range(2)):add('seed|meshkey '+' '.join([f'{bits:08x}']*4)+' '+str(convex))
 for _ in range(4000):
  values=[]
  for i in range(24):
   if i in [0,2,15,16,17]:v=rng.randrange(65536)
   elif i in [3,9,10,11,12,13]:v=rng.randrange(-128,128)
   elif i==23:v=rng.randrange(-2147483648,2147483648)
   else:v=rng.randrange(256)
   values.append(f'set {i} {v}')
  hashes='meshkey '+' '.join(f'{rng.randrange(1<<32):08x}' for _ in range(4))+' '+str(rng.randrange(2))
  add('|'.join(values+[hashes]))
 mark('floatBitsClampSerializationAndMeshHash')
 for value in ['null','-','00','abcd',rng.randbytes(128).hex()]:add('texture '+value)
 add('alias');add('defaults');add('aliasreplace');add('nulltextures')
 for n in range(16):add('texture '+data(rng.randbytes(n))+'|decode')
 mark('textureAssignmentDecodeNullAndMemberwiseClone')
 # SDK MathF.Round and unchecked packing, including half-even thresholds and nonfinite values.
 for kind,bits in itertools.product(range(4),floatbits):add(f'range {kind} {bits:08x} {bits:08x}')
 for n in range(-200,201):
  for delta in [-.50001,-.5,-.49999,0,.49999,.5,.50001]:
   bits=struct.unpack('<I',struct.pack('<f',(n+delta)/50000))[0];add(f'range 0 {bits:08x} {bits:08x}')
 mark('sdkCutPackingRoundEvenFullFloatBits')
 for code in range(65536):add(f'ovrnew 1|ovrset 0 7 {code:04x}')
 for text in ['a','é','中','😀','a\0b','\ud800a','a\udfff','\ud800\ud800\udfff','{"asset":true}']:
  raw=text.encode('utf-16-be',errors='surrogatepass').hex();add('ovrnew 2|ovrset 0 255 '+raw+'|ovrset 1 7 null')
 for count in [0,1,2,127,128,129,255,256,257]:
  add('|'.join([f'ovrnew {count}']+[f'ovrset {n} {n%256} 0061' for n in range(count)]))
 for length in [255,256,65534,65535,65536,65537]:add('ovrnew 1|ovrset 0 1 '+'0061'*length)
 mark('overrideUtf16EncodingAllCodeUnitsAndLengthWraps')
 prefixes=['','ovrnew 1|ovrset 0 19 0078|']
 for prefix in prefixes:
  for raw in ['null','-','00','01','02','80','81','ff','01000000','0100000000']:
   add(prefix+'ovrread '+raw)
  for _ in range(2000):
   b=rng.randbytes(rng.randrange(80));add(prefix+'ovrread '+data(b))
  for value in range(256):
   b=bytes([1,7,1,0,value]);add(prefix+'ovrread '+b.hex())
  for text in ['{"asset":true}','{\0"asset":true}','{"Asset":true}','{"\u200basset":true}','é','😀','a\0','a\0b']:
   b=text.encode();packet=bytes([1,9,len(b)%256,len(b)//256])+b;add(prefix+'ovrread '+packet.hex())
 mark('overrideDecodeNullMalformedUtf8LegacyPrefixAndPriorClear')
 for row in list(rows):
  if row.startswith('factory ') or row.startswith('seed|float ') or row.startswith('set '):add(row+'|convert 0')
 for profile,sculpt in itertools.product(range(256),[-1,0,1,5,6,64,128,133,255]):
  add(f'fromomv {profile} 32 255 9 {sculpt} 1 '+' '.join(['3f000000']*19))
 for _ in range(4000):
  header=f'fromomv {rng.randrange(256)} {rng.randrange(256)} {rng.randrange(256)} {rng.randrange(256)} {rng.choice([-1]+list(range(256)))} {rng.randrange(2)} '
  add(header+' '.join(f'{rng.randrange(1<<32):08x}' for _ in range(19)))
 for bits in floatbits[:1017]:add('fromomv 49 16 7 9 133 1 '+' '.join([f'{bits:08x}']*19))
 for positioned in range(2):add('seed|convert '+str(positioned))
 add('nullomv');mark('fullSdkConstructorAndOmvConversionProjection')
 def u16(text):return text.encode('utf-16-be',errors='surrogatepass').hex() or '-'
 for capacity in [-1,0,1,4]:add(f'medianew {capacity}')
 for n in [0,1,2,7]:
  add('|'.join(['medianew 0']+['mediaadd '+str(i%2) for i in range(n)]+['mediaxml']))
 for offset in range(0,65536,128):
  raw=''.join(f'{n:04x}' for n in range(offset,min(offset+128,65536)));add('medianew 0|mediaadd 1|mediafield 0 '+raw+'|mediaxml')
 texts=['','a','<&>"\'','a\0b','é中😀','\ud800a','\udfff','\ud800\udfff','\n\r\t']
 for text in texts:
  for field in range(2):add(f'medianew 0|mediaadd 1|mediafield {field} {u16(text)}|mediaxml')
 for index,value in itertools.product(range(12),[-2147483648,-1,0,1,2,7,127,255,256,2147483647]):add(f'medianew 0|mediaadd 1|mediavalue {index} {value}|mediaxml')
 for count in [-1,0,1,3]:
  add('|'.join(['medianew 0','mediaadd 1',f'mediawhites {count}']+[f'mediawhite {n} '+('null' if n%2 else u16('é<&>')) for n in range(max(count,0))]+['mediaxml']))
 for _ in range(4000):
  text=''.join(chr(rng.randrange(65536)) for _ in range(rng.randrange(20)));add('medianew 0|mediaadd 1|mediafield 0 '+u16(text)+'|mediaxml')
 mark('mediaListToXmlFieldsAllUtf16UnitsAndWhitelist')
 xmls=['<OSMedia type="sl"><OSData><llsd><array/></llsd></OSData></OSMedia>',
 '<OSMedia type="sl"><OSData><llsd><array><undef/><boolean>1</boolean><string>x</string><map/></array></llsd></OSData></OSMedia>',
 '<OSMedia type="sl"><OSData><llsd><map/></llsd></OSData></OSMedia>',
 '<OSMedia type="sl"><OSData><llsd><array><map><key>auto_play</key><boolean>1</boolean><key>home_url</key><string>é&amp;&lt;</string><key>height_pixels</key><integer>-5</integer><key>perms_control</key><integer>257</integer><key>whitelist</key><array><string>x</string><undef/><integer>7</integer><map/></array></map></array></llsd></OSData></OSMedia>',
 '<OSMedia type="other"/>','<Other type="sl"/>','<OSMedia type="sl"/>','<OSMedia/>','<OSMedia type="sl">\n<OSData><llsd><array><map/></array></llsd></OSData></OSMedia>',
 '<OSMedia type="sl"><OSData></OSData></OSMedia>','bad','',None]
 for text in xmls:
  raw='null' if text is None else u16(text)
  add('medianew 0|mediaadd 0|mediaread '+raw)
  add('mediafrom '+raw)
 for text in xmls[:4]:
  raw=u16('<outer>'+text+'</outer>');add('medianew 0|mediaouter '+raw)
 add('medianew 0|mediaouter '+u16('<outer/>'));mark('mediaListReaderAppendFromXmlAndOuterReader')
 for empty,fault in itertools.product(range(2),range(3)):
  add(f'medianew 0|mediaouterfault {empty} {fault} '+u16(xmls[1]))
 for fault in range(2):add(f'medianew 0|mediaadd 1|mediawrite {fault}')
 for op in ['mediawritenull','mediareadnull','mediacollect 0']:add('medianew 0|'+op)
 add('medianew 0|mediaadd 1|mediacollect 1|mediaxml')
 add('medianew 0|mediaadd 1|mediafield 0 d800|mediawritenull');mark('mediaXmlWriterNullFaultAndOuterReaderBoundaries')
 for kind,bits in itertools.product(range(3),floatbits):add(f'dimension {kind} {bits:08x}')
 for type,prefix in itertools.product(range(256),['','seed|']):add(prefix+f'sculptprops {type} ffffffff-0000-1234-abcd-001122334455')
 for id in ['00000000-0000-0000-0000-000000000000','ffffffff-ffff-ffff-ffff-ffffffffffff','00112233-4455-6677-8899-aabbccddeeff']:add('textures '+id+'|decode')
 add('seed|getmesh');mark('remainingScalarMethodsCreateTexturesSculptAndNoop')
 for sign in ['-','~','NEG','−','a'+chr(0)+'b',chr(0xd800)]:
  add('medianew 0|mediaadd 1|mediavalue 7 -1|mediavalue 8 -2|mediavalue 9 -3|mediaculture '+u16(sign)+'|mediaxml')
 add('medianew 0|mediaadd 1|mediavalue 7 -1|mediavalue 8 -2|mediavalue 9 -3|mediafield 0 006f006c0064|mediawhites 1|mediawhite 0 0077|mediamutate');mark('mediaCurrentCultureNegativeSignAndGetOsdSnapshotMutation')
 payload='\n'.join(rows)+'\n';expected=invoke([a.dotnet,str(w/'app/PrimitiveBaseShapeReference.dll')],payload);actual=invoke([a.native,'--probe'],payload);assert len(expected)==len(actual)==len(rows),(len(expected),len(actual),len(rows))
 diffs=[dict(input=v,source=s,native=n) for v,s,n in zip(rows,expected,actual) if s!=n]
 report=dict(passed=not diffs,cases=len(rows),groups=groups,sourceSha256=hashlib.sha256((ROOT/'OpenSim/Framework/PrimitiveBaseShape.cs').read_bytes()).hexdigest(),referenceSha256=hashlib.sha256((ROOT/'native-port/tests/primitive_base_shape_reference.cs').read_bytes()).hexdigest(),sdkSha256={name:hashlib.sha256((ROOT/f'bin/{name}.dll').read_bytes()).hexdigest() for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','log4net','System.Configuration.ConfigurationManager']},scope='Actual entire unchanged PrimitiveBaseShape source with real SDK and log4net. Native methods cover scalar storage/profile normalization/log order, factories, shallow clone aliases, texture raw assignment, extra record readers/writer/update/full-parser quirks, and mesh hashing. Unsafe SDK UUID/float reads outside input buffers, negative offsets, impossible managed array sizes are explicitly excluded; native raises NativeShapeUnsafeRead rather than reproducing unrelated memory bytes. SDK constructor/packing/ToOmvPrimitive are compared through the explicitly named consumed Primitive property projection; unrelated SDK Primitive fields/methods are not claimed. PBR raw/string codecs and MediaList UTF16 XML serialization/append/from-XML/outer-reader/null/fault methods are compared. CurrentCulture StartsWith, MediaList integer formatting, and System.Xml reader+SDK LLSD parser+CurrentCulture MediaEntry coercion remain explicit required providers; this fixture binds startup-invariant prefix matching and a clearly fixture-only DOM/parser adapter for selected canonical reader cases. No complete production System.Xml/LLSD binding is claimed. TextureEntry decode uses existing native SDK model; null assignment and lengths0..15 are compared, while malformed unsafe native SDK reads are excluded. Fixed-length native arrays retain shared identity and element mutation but cannot be resized/reassigned through the array object.',differences=diffs[:10]);(w/'primitive-base-shape-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:2],indent=2);print(f'PASS: {len(rows)} actual PrimitiveBaseShape method observations')
if __name__=='__main__':main()
