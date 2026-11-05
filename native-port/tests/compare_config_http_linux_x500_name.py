import argparse,json,subprocess,xml.etree.ElementTree as ET,random,re,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(Path(__file__).resolve().with_name('config_http_linux_x500_name_reference.cs')));ET.ElementTree(project).write(w/'Reference.csproj')
r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def tlv(tag,value):
 n=len(value);length=bytes([n]) if n<128 else bytes([128+(n.bit_length()+7)//8])+n.to_bytes((n.bit_length()+7)//8,'big')
 return bytes([tag])+length+value
def attr(value,oid=bytes.fromhex('550403')):return tlv(48,tlv(6,oid)+value)
def name(*rdns):return tlv(48,b''.join(tlv(49,b''.join(rdn)) for rdn in rdns))
rows=[]
def add(label,data):rows.append((label,data))
strings=[b'',b'hello',b' hello',b'hello ',b'a,b',b'a+b',b'a=b',b'a"b',b'a\nb',b'a\rb',b'a#b',b'a;b',b'a<b',b'a>b',b'a\\b',b'\x00',b'a\x00\x00',b'a\x00b',b'\t',b'\v',b'\x7f',b'\x80',b'\xff',b'\xc3\xa9',b'\xc3\x28',b'\xf0\x9f\x98\x80',b'\xed\xa0\x80',b'\xc0\x80']
for tag in [4,5,12,18,19,20,22,23,24,26,27,28,30,0x24,0x2c,0x32,0x33,0x34,0x36,0x3e,0x80,0xa0]:
 for value in strings:
  add(f'tag{tag}-value{value.hex()}',name([attr(tlv(tag,value))]))
for code in range(65536):
 # Exhaust all BMP code units in BMPString; includes surrogate rejection.
 add(f'bmp{code:04x}',name([attr(tlv(30,code.to_bytes(2,'big')))]))
for code in range(256):
 for tag in [12,18,19,20,22]:add(f'byte{tag}-{code}',name([attr(tlv(tag,bytes([code])))]))
oids=[b'',b'\x80',b'\x80\x00',bytes.fromhex('550403'),bytes.fromhex('55040a'),bytes.fromhex('2a864886f70d010901'),bytes.fromhex('2b0601040183b20301'),b'\x2a'+b'\x81'*17+b'\x00',b'\x2a'+b'\x81'*18+b'\x00']
for oid in oids:add('oid-'+oid.hex(),name([attr(tlv(12,b'value'),oid)]))
def encode_oid(text):
 arcs=list(map(int,text.split('.')));values=[arcs[0]*40+arcs[1],*arcs[2:]];output=b''
 for value in values:
  groups=[value&127];value>>=7
  while value:groups.insert(0,(value&127)|128);value>>=7
  output+=bytes(groups)
 return output
table=Path(__file__).resolve().parents[1]/'src/config_http_linux_x500_oids.inc'
for oid in [*re.findall(r'\{"([0-9.]+)"',table.read_text()),'2.5.4.97','1.3.6.1.4.1.311.60.2.1.1','1.3.6.1.4.1.55555.123',f'2.{2**128-81}',f'2.{2**128-80}']:
 add('friendly-'+oid,name([attr(tlv(12,b'value'),encode_oid(oid))]))
for data in [b'',b'\x30',b'\x30\x80\x00\x00',b'\x30\x81\x00',b'\x30\x00',b'\x30\x00\x00',name([]),name([],[]),name([attr(tlv(12,b'first')),attr(tlv(12,b'second'))],[attr(tlv(12,b'third'))]),name([attr(tlv(4,b'\x05\x00'))]),name([attr(tlv(5,b''))])]:add('structure-'+data.hex(),data)
# Deterministic mutated names exercise malformed lengths/tags/ordering without
# using a native implementation to construct the expected result.
rng=random.Random(831);base=name([attr(tlv(12,b'Example'))],[attr(tlv(19,b'Example Org'),bytes.fromhex('55040a'))])
for i in range(4000):
 value=bytearray(base)
 for _ in range(rng.randrange(1,4)):value[rng.randrange(len(value))]=rng.randrange(256)
 add(f'mutation{i}',bytes(value))
lines='\n'.join(data.hex() or '_' for _,data in rows)+'\n'
def run(command):
 r=subprocess.run(command,input=lines,capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stdout[-1000:]+r.stderr;return r.stdout.splitlines()
source=run([a.dotnet,str(w/'app/Reference.dll')]);native=run([a.native]);assert len(source)==len(native)==len(rows),(len(rows),len(source),len(native))
diff=[dict(index=i,label=rows[i][0],der=rows[i][1].hex(),source=s,native=n) for i,(s,n) in enumerate(zip(source,native)) if s!=n]
digest=lambda text:hashlib.sha256(text.encode()).hexdigest()
(w/'results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),inputSHA256=digest(lines),outputSHA256=dict(source=digest('\n'.join(source)+'\n'),native=digest('\n'.join(native)+'\n')),differences=diff,scope='Actual pinned Linux runtime X500DistinguishedName.Name versus native DER decoder; raw UTF16 output, all BMP units, byte alphabets, quoting, binary fallback, malformed DER and OID limits. System-store loading/cache and default TLS binding remain separate work.'),indent=2)+'\n')
assert not diff,f'{len(diff)} differences: {w}/results.json'
print(f'PASS: {len(rows)} Linux X500 name scenarios')
