from pathlib import Path
import argparse,subprocess,json,xml.etree.ElementTree as ET,socket,socketserver,threading,urllib.parse,re
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet');p.add_argument('--expected',type=Path);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
if not a.expected:
 assert a.dotnet
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',Nullable='enable').items():ET.SubElement(props,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(Path(__file__).with_name('config_xml_multi_proxy_network_reference.cs')));cs=w/'Reference.csproj';ET.ElementTree(project).write(cs);subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'-v:q'],capture_output=True,check=True)
trace=[]
class Server(socketserver.ThreadingMixIn,socketserver.TCPServer):daemon_threads=True
class Handler(socketserver.BaseRequestHandler):
 def handle(self):
  self.request.settimeout(5);data=b''
  while b'\r\n\r\n' not in data:
   part=self.request.recv(65536)
   if not part:return
   data+=part
  first,*headers=data.split(b'\r\n');fields={}
  for header in headers:
   if b':' in header:
    k,v=header.split(b':',1);fields[k.decode().lower()]=v.strip().decode('latin1')
  trace.append(dict(proxy=self.server.role,request=first.decode('latin1'),headers=sorted(fields.items()),raw=data.hex()))
  if self.server.role=='close':return
  if self.server.role=='bad':self.request.sendall(b'HTTP/1.1 ZZZ Broken\r\n\r\n');return
  target=first.split(b' ')[1].decode();path=urllib.parse.urlsplit(target).path
  if path=='/redirect':self.request.sendall(b'HTTP/1.1 302 Found\r\nLocation: /normal\r\nSet-Cookie: a=redirect;Path=/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');return
  if path=='/status':self.request.sendall(b'HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');return
  if path=='/truncated':self.request.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\nabc');return
  body=(first.decode()+'|'+fields.get('host','')+'|'+fields.get('cookie','')).encode()
  self.request.sendall(b'HTTP/1.1 200 OK\r\nSet-Cookie: a=fromproxy;Path=/\r\nContent-Length: '+str(len(body)).encode()+b'\r\nConnection: close\r\n\r\n'+body)
servers=[];dead=[];ports={}
for role in ['good','close','bad']:
 server=Server(('127.0.0.1',0),Handler);server.role=role;ports[role]=server.server_address[1];servers.append(server);threading.Thread(target=server.serve_forever,daemon=True).start()
for role in ['dead1','dead2']:
 sock=socket.socket();sock.bind(('127.0.0.1',0));ports[role]=sock.getsockname()[1];dead.append(sock)
roles={v:k for k,v in ports.items()}
def canonical(output):
 lines=output.splitlines();assert len(lines)==4,lines;result=[]
 for line in lines[:2]:
  if line.startswith('OK:'):
   assert re.fullmatch('[0-9a-f]*',line[3:]),line
   result.append(dict(body=line[3:]));continue
  assert line.startswith('E:'),line;errors=[]
  for piece in line[2:].split('|'):
   kind,hr,code,message=piece.split(':');message=bytes.fromhex(message).decode('utf-16-be')
   for port,role in roles.items():message=re.sub(r'(?<![0-9])'+str(port)+r'(?![0-9])',role,message)
   errors.append(dict(type=kind,hresult=hr,code=int(code),message=message))
  result.append(dict(errors=errors))
 assert lines[2].startswith('TRACE:')and lines[3].startswith('FAILED:')
 return dict(responses=result,attempts=[roles.get(int(v),"unexpected:"+v)for v in lines[2][6:].split(',')if v],failed=sorted(roles.get(int(v),"unexpected:"+v)for v in lines[3][7:].split(',')if v))
def comparable(record):
 record=json.loads(json.dumps(record))
 if a.expected:
  for response in record['responses']:
   if any(e['type']=='System.Net.Sockets.SocketException'for e in response.get('errors',[])):
    for e in response['errors']:e.pop('message',None)
 return record
scenarios=[('fallback',['dead1','good'],'/normal'),('healthy',['good','dead1'],'/normal'),('all-refused',['dead1','dead2'],'/normal'),('single-refused',['dead1'],'/normal'),('unused-tail',['dead1','good','dead2'],'/normal'),('redirect',['dead1','good'],'/redirect'),('status-no-retry',['dead1','good','dead2'],'/status'),('closed-headers',['dead1','close','good'],'/normal'),('bad-header-no-retry',['dead1','bad','good'],'/normal'),('body-no-retry',['good','dead1'],'/truncated'),('env-single',['good'],'/normal'),('env-credentials',['good'],'/normal'),('env-refused',['dead1'],'/normal')]
expected=json.loads(a.expected.read_text())['observations']if a.expected else {};observations={};differences=[]
try:
 for name,proxies,path in scenarios:
  config=';'.join('127.0.0.1:'+str(ports[role])for role in proxies)
  if name.startswith('env-'):config='env:'+('http://user:pass@'if name=='env-credentials'else '')+config
  records={};wire={}
  commands=[('native',[str(Path(a.native).resolve()),config,path])]
  if not a.expected:commands.insert(0,('reference',[a.dotnet,str(w/'app/Reference.dll'),config,path]))
  for label,cmd in commands:
   trace.clear();r=subprocess.run(cmd,capture_output=True,text=True,timeout=45);(w/(name+'-'+label+'.txt')).write_text(r.stdout);(w/(name+'-'+label+'-error.txt')).write_text(r.stderr);assert r.returncode==0,(name,label,r.returncode,r.stderr);records[label]=canonical(r.stdout);raw=list(trace);(w/(name+'-'+label+'-wire.json')).write_text(json.dumps(raw,indent=2));wire[label]=json.loads(json.dumps([{k:v for k,v in row.items()if k!='raw'}for row in raw]))
  wanted=expected[name]if a.expected else dict(result=records['reference'],wire=wire['reference']);actual=dict(result=records['native'],wire=wire['native']);observations[name]=wanted
  if comparable(actual['result'])!=comparable(wanted['result'])or actual['wire']!=wanted['wire']:differences.append(dict(case=name,expected=wanted,actual=actual))
finally:
 for server in servers:server.shutdown();server.server_close()
 for sock in dead:sock.close()
report=dict(cases=len(scenarios),differences=len(differences),examples=differences[:2],observations=observations,applicationParityVerified=False,referencePlatform='Windows .NET8.0.31 HttpWindowsProxy via actual SocketsHttpHandler',comparison='Captured Windows outcomes with OS socket messages excluded'if a.expected else 'Actual runtime with full exception messages and request headers',scope='Real local HTTP proxy requests across refused/live/closed/malformed proxies; two requests per handler, cached failures, redirects/cookies, HTTP status and body failures. No OS discovery/PAC, TLS/SOCKS/auth or full application claim.');(w/'results.json').write_text(json.dumps(report,indent=2));print(json.dumps(dict(cases=len(scenarios),differences=len(differences),examples=differences[:1])));raise SystemExit(bool(differences))
