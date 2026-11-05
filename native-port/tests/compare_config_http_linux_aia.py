import argparse,json,subprocess,xml.etree.ElementTree as ET,random,os,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(Path(__file__).resolve().with_name('config_http_linux_aia_reference.cs')));ET.ElementTree(project).write(w/'Reference.csproj')
r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def tlv(tag,data):
 n=len(data);length=bytes([n]) if n<128 else bytes([128+(n.bit_length()+7)//8])+n.to_bytes((n.bit_length()+7)//8,'big')
 return bytes([tag])+length+data
ca=bytes.fromhex('2b06010505073002');ocsp=bytes.fromhex('2b06010505073001');query='1.3.6.1.5.5.7.48.2'
def entry(location,oid=ca):return tlv(48,tlv(6,oid)+location)
def aia(*entries):return tlv(48,b''.join(entries))
good=entry(tlv(0x86,b'http://fixture.test/issuer'))
rows=[]
def add(label,data,oid=query):rows.append((label,oid,data))
uris=[b'',b'http://fixture.test/issuer',b'HTTP://fixture.test/a%20b#fragment',b' http://fixture.test/ ',b'http://fixture.test/\r\n',b'https://fixture.test/',b'ftp://fixture.test/',b'ldap://fixture.test/',b'file:///tmp/issuer',b'urn:issuer',b'issuer',b'//fixture.test/a',b'http:/a',b'http://',b'http://[::1]/',b'http://[bad]/',b'http://user:pass@fixture.test:80/a',b'http://fixture.test:65536/a',b'http://a_b/a',b'http://a..b/a',b'http://fixture.test/a\x00b',b'http://fixture.test/\\other',b'\x00http://fixture.test/a']
for i,uri in enumerate(uris):
 for method in [ca,ocsp,bytes.fromhex('2a0304')]:
  location=entry(tlv(0x86,uri),method)
  add(f'uri{i}-{method.hex()}',aia(location));add(f'uri{i}-then-good-{method.hex()}',aia(location,good));add(f'good-before-uri{i}-{method.hex()}',aia(good,location))
for b in range(256):
 for tag in [0x81,0x82,0x86]:add(f'ia5-{tag}-{b}',aia(entry(tlv(tag,b'http://fixture.test/'+bytes([b]))),good))
values=[b'',b'\x00',b'\x05\x00',b'\x30\x00',b'\x02\x01\x01',b'\x30\x80\x00\x00',b'\x05\x00\x05\x00',b'\xff',b'\x00\x00']
for tag in [*range(0x80,0x90),*range(0xa0,0xb0),4,5,12,0x30,0x31]:
 for value in values:add(f'general-{tag}-{value.hex()}',aia(entry(tlv(tag,value)),good))
for value in values:
 add('other-'+value.hex(),aia(entry(tlv(0xa0,tlv(6,ca)+tlv(0xa0,value))),good))
 add('directory-'+value.hex(),aia(entry(tlv(0xa4,value)),good))
for tag in [12,19,20,28,30,0x2c,0x33,0x34,0x3c,0x3e,4]:
 for value in [b'',b'valid',b'\xff',b'\xc0\x80',b'\x00A',b'\xd8\x00',b'\xdc\x00',b'\x00\x00\x00A',b'\x05\x00']:
  field=tlv(tag,value)
  for assigner in [b'',tlv(0xa0,field),tlv(0x80,field)]:add('edi-'+str(tag)+'-'+value.hex()+'-'+assigner.hex(),aia(entry(tlv(0xa5,assigner+tlv(0xa1,field))),good))
for code in [*range(0x100),*range(0xd700,0xe100),0xffff]:
 add(f'edi-bmp-{code}',aia(entry(tlv(0xa5,tlv(0xa1,tlv(30,code.to_bytes(2,'big'))))),good))
for oid in [b'',b'\x80',b'\x80\x00',b'\x2a'+b'\x81'*17+b'\x00',b'\x2a'+b'\x81'*18+b'\x00',ca]:
 add('method-'+oid.hex(),aia(entry(tlv(0x86,b'http://fixture.test/a'),oid),good));add('registered-'+oid.hex(),aia(entry(tlv(0x88,oid)),good))
for data in [b'',b'\x30',b'\x30\x80\x00\x00',b'\x30\x81\x00',aia(),aia(good)+b'\x00',aia(good,b'\x01'),aia(b'\x01',good),aia(entry(tlv(0x86,b'http://fixture.test/a'))+b'\x00')]:add('structure-'+data.hex(),data)
for oid in ['','1.3.6.1.5.5.7.48.1','1.3.6.1.5.5.7.48.2','1.2.3','invalid']:add('query-'+oid,aia(entry(tlv(0x86,b'http://ocsp.test/a'),ocsp),good),oid)
rng=random.Random(831);base=aia(entry(tlv(0x86,b'https://fixture.test/a')),good)
for i in range(5000):
 value=bytearray(base)
 for _ in range(rng.randrange(1,4)):value[rng.randrange(len(value))]=rng.randrange(256)
 add(f'mutation{i}',bytes(value))
lines='\n'.join((oid or '_')+' '+(data.hex() or '_') for _,oid,data in rows)+'\n';env=os.environ.copy();env['HOME']=str(w/'home');Path(env['HOME']).mkdir(exist_ok=True)
def run(command):
 r=subprocess.run(command,input=lines,capture_output=True,text=True,env=env,timeout=120);assert r.returncode==0,r.stdout[-1000:]+r.stderr;return r.stdout.splitlines()
source=run([a.dotnet,str(w/'app/Reference.dll')]);native=run([a.native]);assert len(source)==len(native)==len(rows),(len(rows),len(source),len(native))
diff=[dict(index=i,label=rows[i][0],query=rows[i][1],der=rows[i][2].hex(),source=s,native=n) for i,(s,n) in enumerate(zip(source,native)) if s!=n];digest=lambda value:hashlib.sha256(value.encode()).hexdigest()
(w/'results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),inputSHA256=digest(lines),outputSHA256=dict(source=digest('\n'.join(source)+'\n'),native=digest('\n'.join(native)+'\n')),differences=diff,scope='Pinned actual Linux runtime FindHttpAiaRecord versus native full AccessDescription/GeneralName parser and native System.Uri backend. First HTTP record, original UTF16 URI text, nonmatching-method validation, all GeneralName choices, DirectoryString handling, ordering/opaque fields/malformed DER/OID limits. Downloader, issuer orchestration and save-toCA require separate integration checks.'),indent=2)+'\n')
assert not diff,f'{len(diff)} differences: {w}/results.json';print(f'PASS: {len(rows)} Linux AIA record scenarios')
