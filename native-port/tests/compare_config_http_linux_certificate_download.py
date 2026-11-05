import argparse,base64,hashlib,http.server,json,os,random,socket,subprocess,threading,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(exist_ok=True,parents=True)
def run(command,**kwargs):
 result=subprocess.run(command,capture_output=True,text=True,timeout=120,**kwargs);assert result.returncode==0,result.stdout+result.stderr;return result.stdout
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,key).text=value
ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(Path(__file__).with_name('config_http_linux_certificate_download_reference.cs').resolve()));ET.ElementTree(project).write(w/'Reference.csproj')
run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q']);reference=[a.dotnet,str(w/'app/Reference.dll')]
run([*reference,'--prepare',str(w/'cert.der'),str(w/'cert.pfx')]);der=(w/'cert.der').read_bytes()
def pem(data,label='CERTIFICATE'):
 encoded=base64.b64encode(data);return b'-----BEGIN '+label.encode()+b'-----\n'+b'\n'.join(encoded[i:i+64] for i in range(0,len(encoded),64))+b'\n-----END '+label.encode()+b'-----\n'
normal=pem(der);(w/'cert.pem').write_bytes(normal)
cases=[]
def add(label,data,status=200,ticks=100000000,redirect=None):cases.append(dict(label=label,body=data,status=status,ticks=ticks,redirect=redirect))
add('der',der);add('pem',normal);add('empty',b'');add('garbage',b'not a certificate');add('pfx',(w/'cert.pfx').read_bytes())
for mode,options in [('plain',[]),('trust-server',['-addtrust','serverAuth']),('trust-client',['-addtrust','clientAuth']),('reject-server',['-addreject','serverAuth']),('reject-client',['-addreject','clientAuth']),('trust-and-reject',['-addtrust','clientAuth','-addreject','serverAuth']),('alias',['-setalias','downloaded alias'])]:
 target=w/(mode+'.pem');run(['openssl','x509','-in',str(w/'cert.pem'),'-trustout',*options,'-out',str(target)]);data=target.read_bytes();add('aux-'+mode,data)
 decoded=base64.b64decode(b''.join(data.splitlines()[1:-1]));add('aux-as-der-'+mode,decoded)
 if mode=='trust-and-reject':
  for n in range(len(der),len(decoded)):add('aux-truncated-'+str(n),pem(decoded[:n],'TRUSTED CERTIFICATE'))
  add('aux-trailing-garbage',pem(decoded+b'junk','TRUSTED CERTIFICATE'))
run(['openssl','crl2pkcs7','-nocrl','-certfile',str(w/'cert.pem'),'-outform','DER','-out',str(w/'cert.p7b')]);pkcs7=(w/'cert.p7b').read_bytes();add('pkcs7-der',pkcs7);add('pkcs7-pem',pem(pkcs7,'PKCS7'));add('pkcs7-then-cert',pem(pkcs7,'PKCS7')+normal)
for suffix in [b'\x00',b'junk',normal,der,pkcs7]:add('der-suffix-'+suffix[:8].hex(),der+suffix)
for prefix in [b'junk\n',b'\xef\xbb\xbf',b'\x00',b'\r\n \t',pem(b'bad')]:add('pem-prefix-'+prefix[:8].hex(),prefix+normal)
for label,data in [('missing-footer',normal.split(b'-----END')[0]),('crlf',normal.replace(b'\n',b'\r\n')),('second-cert',normal+normal),('der-leading-zero',b'\x00'+der),('pem-invalid-first',pem(b'bad')+normal)]:add(label,data)
for n in range(len(der)):add('der-truncated-'+str(n),der[:n])
rng=random.Random(80131)
for index in range(120):
 mutated=bytearray(der);at=rng.randrange(len(mutated));mutated[at]^=1<<rng.randrange(8);add('der-bit-'+str(index),bytes(mutated))
for status in [201,299,300,301,302,303,304,305,307,308,400,404,500]:add('status-'+str(status),der,status)
add('redirect-to-cert',b'redirect',302,redirect='/0');add('redirect-to-pkcs7',b'redirect',302,redirect='/'+str(next(i for i,c in enumerate(cases) if c['label']=='pkcs7-der')))
add('timeout-zero',der,ticks=0);add('timeout-negative',der,ticks=-1)
requests=[];active=''
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def setup(self):super().setup();self.connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);self.group=active
 def log_message(self,*args):pass
 def do_GET(self):
  requests.append(dict(group=self.group,path=self.path));case=cases[int(self.path[1:])];self.send_response(case['status']);self.send_header('Content-Length',str(len(case['body'])));
  if case['redirect'] is not None:self.send_header('Location',case['redirect'])
  self.end_headers()
  if case['status']!=304:self.wfile.write(case['body'])
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();base='http://127.0.0.1:'+str(server.server_port)
protocol='\n'.join(str(c['ticks'])+' '+(base+'/'+str(i)).encode('utf-16-be').hex() for i,c in enumerate(cases))+'\n';env=os.environ.copy();env.update(NO_PROXY='*',no_proxy='*')
for key in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:env.pop(key,None)
outputs={}
try:
 for kind,command in [('source',reference),('native',[a.native])]:active=kind;outputs[kind]=run(command,input=protocol,env=env).splitlines();assert len(outputs[kind])==len(cases)
finally:server.shutdown();server.server_close();thread.join()
differences=[dict(label=c['label'],source=s,native=n) for c,s,n in zip(cases,outputs['source'],outputs['native']) if s!=n];paths={kind:[x['path'] for x in requests if x['group']==kind] for kind in outputs}
if paths['source']!=paths['native']:differences.append(dict(wire=paths))
assert outputs['source'][0]!='_';assert outputs['source'][next(i for i,c in enumerate(cases) if c['label']=='pkcs7-der')]=='_';assert outputs['source'][next(i for i,c in enumerate(cases) if c['label']=='aux-trust-client')].split()[0]!=outputs['source'][next(i for i,c in enumerate(cases) if c['label']=='aux-trust-client')].split()[1]
report=dict(passed=not differences,cases=len(cases),inputSHA256=hashlib.sha256(protocol.encode()).hexdigest(),fixtures=[dict(label=c['label'],bodySHA256=hashlib.sha256(c['body']).hexdigest(),bodyHex=c['body'].hex(),status=c['status'],ticks=c['ticks'],redirect=c['redirect']) for c in cases],outputs=outputs,requests=paths,differences=differences,scope='Actual pinned Linux OpenSslCertificateAssetDownloader.DownloadCertificate via HTTP versus native HTTP client plus certificate importer. Exact RawData and i2d_X509_AUX bytes, accepted/rejected malformed DER/PEM/AUX and containers, HTTP statuses/redirects and complete request order. Native AIA-chain bridge, CA persistence and live TLS remain separate.')
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert not differences,f'{len(differences)} differences: {w}/results.json';print(f'PASS: {len(cases)} certificate downloads with DER/AUX parity')
