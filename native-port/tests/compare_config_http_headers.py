from pathlib import Path
import argparse,socketserver,threading,subprocess,json,xml.etree.ElementTree as ET
ap=argparse.ArgumentParser();ap.add_argument('--native',required=True);ap.add_argument('--work',type=Path,required=True);a=ap.parse_args();root=Path(__file__).resolve().parents[2];p=root/'native-port/build/config-http-headers-preparation';w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
g=ET.SubElement(project,'ItemGroup');ET.SubElement(g,'Compile',Include=str(root/'native-port/tests/config_http_headers_reference.cs'));cs=w/'HeaderReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');subprocess.run(['dotnet','build',str(cs),'-o',str(w/'app'),'-v','quiet'],check=True,capture_output=True)
responses=[];labels=[]
def add(label,data):labels.append(label);responses.append(data)
def head(name=b'X-Test',value=b'value',status=b'HTTP/1.1 200 OK',extra=b''):return status+b'\r\n'+name+b':'+value+b'\r\n'+extra+b'Content-Length: 0\r\nConnection: close\r\n\r\n'
for i in range(256):
 add('value-'+str(i),head(value=b'x'+bytes([i])+b'y'))
 add('name-'+str(i),head(name=b'X'+bytes([i])+b'Y'))
 add('reason-'+str(i),head(status=b'HTTP/1.1 200 a'+bytes([i])+b'b'))
for i in range(256):add('location-'+str(i),head(name=b'Location',value=b'/x'+bytes([i])+b'y'))
for value in [b' a ',b'\ta\t',b'a\r\n b',b'a\n b',b'a\r\n\tb',b'a\r\n  b',b'a\r\n\t b',b'a\r\n \r\n b']:
 add('fold-'+value.hex(),head(value=value))
for status in [b'HTTP/1.0 200',b'HTTP/1.2 200 ',b'HTTP/1.9 200 OK',b'HTTP/2.0 200 OK',b'HTTP/1.1 000',b'HTTP/1.1 999',b'HTTP/1.1 20x OK',b'HTTP/1.1 200\tOK']:
 add('status-'+status.hex(),head(status=status))
for name in [b'X-Test ',b'X-Test\t',b' X-Test',b'\tX-Test',b'',b' ']:add('name-extra-'+name.hex(),head(name=name))
for value in [bytes.fromhex(h) for h in ['c2a9','c3a9','e282ac','f09f9982','eda080','f4908080','c080','efbbbf','e180','c2']]:add('location-multi-'+value.hex(),head(name=b'Location',value=b'/'+value))
for extra in [b'Set-Cookie: a=one\r\nSet-Cookie: b=two\r\n',b'Location: /one\r\nLocation: /two\r\n',b'Content-Length: 1\r\n',b'Content-Length: bad\r\n',b'Transfer-Encoding: gzip\r\n',b'Transfer-Encoding: chunked\r\n']:
 add('duplicate-'+extra.hex(),head(extra=extra))
class Server(socketserver.ThreadingMixIn,socketserver.TCPServer):daemon_threads=True
class Handler(socketserver.BaseRequestHandler):
 def handle(self):
  data=b''
  while b'\r\n\r\n' not in data:
   piece=self.request.recv(65536)
   if not piece:return
   data+=piece
  index=int(data.split(b' ')[1][1:]);self.request.sendall(responses[index])
s=Server(('127.0.0.1',0),Handler);threading.Thread(target=s.serve_forever,daemon=True).start();urls=['http://127.0.0.1:'+str(s.server_address[1])+'/'+str(i) for i in range(len(responses))]
r=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll')],input='\n'.join(urls)+'\n',capture_output=True,text=True,timeout=60);s.shutdown();assert r.returncode==0,(r.stdout,r.stderr);results=r.stdout.splitlines();assert len(results)==len(responses)
(w/'source.json').write_text(json.dumps([dict(label=l,response=b.hex(),result=r) for l,b,r in zip(labels,responses,results)],indent=2)+'\n',encoding='utf-8');print(len(results),'actual raw response observations')

def normalized(value):
 if not value.startswith('ok:'):return value
 parts=value.split('/');headers={}
 for field in parts[1:]:
  raw=field.split(':');name=bytes.fromhex(raw[1]).decode('utf-16-be').lower();headers.setdefault(name,[]).extend(raw[2:])
 return parts[0],headers
r=subprocess.run([a.native,'--probe'],input='\n'.join(b.hex() for b in responses)+'\n',capture_output=True,text=True,check=True);native=r.stdout.splitlines();assert len(native)==len(results)
differences=[dict(label=l,source=s,native=n) for l,s,n in zip(labels,results,native) if normalized(s)!=normalized(n)]
(w/'header-results.json').write_text(json.dumps(dict(cases=len(results),passed=not differences,differences=differences),indent=2)+'\n',encoding='utf-8');assert not differences,differences[:3]
encoding_rows=['-']+[f'{i:04x}' for i in range(65536)]+['d800dc00','d83dde42','dc00d800'];text='\n'.join(encoding_rows)+'\n'
source_encoding=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll'),'--encode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();native_encoding=subprocess.run([a.native,'--encode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();assert len(source_encoding)==len(native_encoding)==len(encoding_rows)
differences=[dict(row=r,source=s,native=n) for r,s,n in zip(encoding_rows,source_encoding,native_encoding) if s!=n]
(w/'request-encoding-results.json').write_text(json.dumps(dict(cases=len(encoding_rows),passed=not differences,differences=differences[:5]),indent=2)+'\n',encoding='utf-8');assert not differences,differences[:3]
print('PASS',len(results),'actual raw HTTP headers and',len(encoding_rows),'actual BCL WriteString observations')

# HeaderDescriptor.Location invokes actual strict Utf8.ToUtf16 before Latin1.
import random,itertools
rng=__import__('random').Random(103817)
decode_rows=['-']+[bytes([i]).hex() for i in range(256)]+[bytes([i,j]).hex() for i in range(256) for j in range(256)]
decode_rows += [chr(c).encode('utf-8').hex() for c in range(0x110000) if not 0xd800<=c<=0xdfff]
decode_rows += [bytes(rng.randrange(256) for _ in range(rng.randrange(16))).hex() or '-' for _ in range(20000)]
text='\n'.join(decode_rows)+'\n'
source_decode=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll'),'--decode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();native_decode=subprocess.run([a.native,'--decode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();assert len(source_decode)==len(native_decode)==len(decode_rows)
differences=[dict(row=r,source=s,native=n) for r,s,n in zip(decode_rows,source_decode,native_decode) if s!=n]
(w/'location-decoding-results.json').write_text(json.dumps(dict(cases=len(decode_rows),passed=not differences,differences=differences[:5]),indent=2)+'\n',encoding='utf-8');assert not differences,differences[:3]
print('PASS',len(decode_rows),'actual HeaderDescriptor UTF8/Latin1 observations')
def enc(v):return v.encode('utf-16-be','surrogatepass').hex() or '-'
framing_rows=[]
for c in map(chr,range(65536)):
 for op,value in [('C','1'+c),('T','chunked'+c),('T','gzip;a="'+c+'",chunked')]:framing_rows.append(op+' '+enc(value))
lengths=['0','1','01','-1','+1','1,1','1,2','9223372036854775807','9223372036854775808',' 1 ','\t1\t','','  ','1\r\n','00000000000000000000']
for first,last in itertools.product(lengths,repeat=2):framing_rows.append('C '+enc(first)+'|C '+enc(last))
codings=['chunked','CHUNKED','gzip, chunked','chunked, gzip','chunked; a=b','gzip; a=b, chunked','bad@, chunked','chunked, bad@',',chunked',',,chunked,,','chunked\r','chunked\0','gzip,\tchunked','gzip;a="\\\r",chunked']
for first,last in itertools.product(codings,repeat=2):framing_rows.append('T '+enc(first)+'|T '+enc(last))
rng=random.Random(91343);parts=['chunked','gzip','deflate',',',';','=','"','\\',' ','\t','\r','\n','a','b','\0','é','\ud800']
for _ in range(20000):framing_rows.append('T '+enc(''.join(rng.choice(parts) for _ in range(rng.randrange(18)))))
text='\n'.join(framing_rows)+'\n';source_framing=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll'),'--framing'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();native_framing=subprocess.run([a.native,'--framing'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();assert len(source_framing)==len(native_framing)==len(framing_rows)
differences=[dict(row=r,source=s,native=n) for r,s,n in zip(framing_rows,source_framing,native_framing) if s!=n]
(w/'entity-header-results.json').write_text(json.dumps(dict(cases=len(framing_rows),passed=not differences,differences=differences[:5]),indent=2)+'\n',encoding='utf-8');assert not differences,differences[:3]
print('PASS',len(framing_rows),'actual Content-Length/Transfer-Encoding getter observations')

# Exercise actual HeaderDescriptor.GetHeaderValue for the consumed Set-Cookie and
# Location descriptors with real ASCII/Latin1/UTF8 Encoding instances.
rng=random.Random(293843)
selected_bytes=['-']+[bytes([i]).hex() for i in range(256)]+[bytes([i,j]).hex() for i in range(256) for j in range(256)]
selected_bytes += [chr(c).encode('utf-8').hex() for c in range(0,0x110000,997) if not 0xd800<=c<=0xdfff]
selected_bytes += [bytes(rng.randrange(256) for _ in range(rng.randrange(16))).hex() or '-' for _ in range(10000)]
selected_rows=[name+' '+encoding+' '+value for name in ['L','S'] for encoding in ['A','L','U'] for value in selected_bytes]
text='\n'.join(selected_rows)+'\n';source_selected=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll'),'--selected-decode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();native_selected=subprocess.run([a.native,'--selected-decode'],input=text,text=True,capture_output=True,check=True).stdout.splitlines();assert len(source_selected)==len(native_selected)==len(selected_rows)
differences=[dict(row=r,source=s,native=n) for r,s,n in zip(selected_rows,source_selected,native_selected) if s!=n]
(w/'selected-encoding-results.json').write_text(json.dumps(dict(cases=len(selected_rows),passed=not differences,differences=differences[:5]),indent=2)+'\n',encoding='utf-8');assert not differences,differences[:3]
print('PASS',len(selected_rows),'actual consumed HeaderDescriptor/Encoding observations')

# Actual MapSendException with controlled _canRetry states; distinguishes the
# initial zero read from EOF after delivered header data without network races.
source_map=subprocess.run(['dotnet',str(w/'app/HeaderReference.dll'),'--map-eof'],input='0\n1\n',text=True,capture_output=True,check=True).stdout.splitlines();native_map=subprocess.run([a.native,'--map-eof'],input='0\n1\n',text=True,capture_output=True,check=True).stdout.splitlines();assert len(source_map)==len(native_map)==2;assert source_map==native_map,(source_map,native_map)
(w/'header-eof-mapping-results.json').write_text(json.dumps(dict(cases=2,passed=True,source=source_map,native=native_map),indent=2)+'\n',encoding='utf-8')
print('PASS actual header EOF typed metadata and both retry states')
