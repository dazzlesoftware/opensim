import argparse,json,subprocess,xml.etree.ElementTree as ET,os,threading,time,urllib.parse,http.server,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--http-downloads',action='store_true');p.add_argument('--pem-aux-downloads',action='store_true');a=p.parse_args();assert not a.pem_aux_downloads or a.http_downloads;w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(Path(__file__).resolve().with_name('config_http_linux_aia_chain_reference.cs')));ET.ElementTree(project).write(w/'Reference.csproj')
def run(command,**kwargs):
 r=subprocess.run(command,capture_output=True,text=True,timeout=120,**kwargs);assert r.returncode==0,r.stdout+r.stderr;return r.stdout
run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q']);reference=[a.dotnet,str(w/'app/Reference.dll')]
routes={};requests=[];active='source'
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def log_message(self,*args):pass
 def do_GET(self):
  requests.append((active,self.path));reply=routes[self.path];time.sleep(reply['delay']/1000);body=reply.get('body',bytes.fromhex(reply['der']) if reply['der'] else b'')
  self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
run([*reference,'--prepare',str(w/'fixtures.json'),str(server.server_port)]);rows=json.loads((w/'fixtures.json').read_text())
for row in rows:
 for reply in row['replies']:
  if a.pem_aux_downloads and reply['der']:
   (w/'issuer.der').write_bytes(bytes.fromhex(reply['der']));reply['body']=run(['openssl','x509','-inform','DER','-in',str(w/'issuer.der'),'-trustout','-addtrust','clientAuth','-addreject','serverAuth','-setalias','AIA issuer auxiliary metadata']).encode()
  routes[urllib.parse.urlsplit(reply['uri']).path]=reply
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
env=os.environ.copy();env.update(HOME=str(w/'home'),SSL_CERT_FILE='',SSL_CERT_DIR='',NO_PROXY='*',no_proxy='*');Path(env['HOME']).mkdir(exist_ok=True)
for key in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:env.pop(key,None)
try:source=json.loads(run([*reference,'--run',str(w/'fixtures.json')],env=env))
except BaseException:server.shutdown();server.server_close();thread.join();raise
def listing(values):return [str(len(values)),*values]
def utf16(value):return value.encode('utf-16-be').hex()
lines=[]
for row in rows:
 parts=[row['leaf'],*listing(row['roots']),*listing(row['extra']),*listing(row['manual']),str(row['mode']),str(row['timeout']),str(len(row['replies']))]
 for reply in row['replies']:parts +=[utf16(reply['uri']),reply['der'] or '_']
 lines.append(' '.join(parts))
protocol='\n'.join(lines)+'\n';active='native'
try:values=run([a.native,*(['--http-downloads'] if a.http_downloads else [])],input=protocol,env=env).splitlines()
finally:server.shutdown();server.server_close();thread.join()
assert len(values)==len(source)==len(rows),(len(values),len(source),len(rows))
differences=[];observations=[]
for row,expected,value in zip(rows,source,values):
 actual,trace=value.rsplit(' U:',1);calls=[] if trace=='-' else [item.rsplit('/',1) for item in trace.split(',')]
 native_paths=[urllib.parse.urlsplit(bytes.fromhex(uri).decode('utf-16-be')).path for uri,_ in calls]
 source_paths=[path for kind,path in requests if kind=='source' and path.split('/')[1]==row['label']]
 if a.http_downloads:assert native_paths==[path for kind,path in requests if kind=='native' and path.split('/')[1]==row['label']],row['label']
 case=dict(label=row['label'],source=expected['expected'],native=actual,sourcePaths=source_paths,nativePaths=native_paths,downloadTimeouts=[int(duration) for _,duration in calls],sourceDownloadElapsedMilliseconds=expected['downloadElapsedMilliseconds']);observations.append(case)
 if actual!=expected['expected'] or source_paths!=native_paths or any(int(duration)!=row['timeout'] for _,duration in calls):differences.append(case)
 if row['label']=='per-download-timeout':assert expected['downloadElapsedMilliseconds']>=1200 and expected['expected'].startswith('20 0 '),case
 if row['label']=='manual-duplicate-instances':assert ' I:0,2 ' in expected['expected'],case
digest=lambda value:hashlib.sha256(value.encode()).hexdigest()
(w/'results.json').write_text(json.dumps(dict(passed=not differences,cases=len(rows),sourceHttpRequests=sum(kind=='source' for kind,path in requests),nativeHttpRequests=sum(kind=='native' for kind,path in requests),nativeHttpDownloads=a.http_downloads,pemAuxDownloads=a.pem_aux_downloads,inputSHA256=digest(protocol),downloadBodySHA256={path:digest(reply['body'].decode()) for path,reply in routes.items() if 'body' in reply},differences=differences,observations=observations,scope='Actual pinned Linux runtime private FindFirstChain/FindChainViaAia/Finish with real local HTTP downloads versus native AIA workflow. In HTTP mode native uses real HTTP requester, downloaded-certificate importer and AUX-preserving chain insertion; otherwise it uses the explicit parsed-DER callback. AUX mode serves trusted PEM with clientAuth trust, serverAuth reject and an alias for every nonempty issuer. Exact native return codes, retained downloaded DER, chosen-chain DER/status order, URL request order and per-download timeout arguments. Manual duplicate X509 instances verify selected handle identity, and unrelated pre-seeded issuer verifies index offsets. Save-toCA and live default TLS integration remain unfinished.'),indent=2)+'\n')
assert not differences,f'{len(differences)} differences: {w}/results.json';print(f'PASS: {len(rows)} Linux AIA workflows / {sum(kind=="source" for kind,path in requests)} source HTTP requests / {sum(kind=="native" for kind,path in requests)} native HTTP requests')
