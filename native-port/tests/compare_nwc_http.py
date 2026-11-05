"""Real HTTP Nwc POST/stream response against actual bundled DLL and legacy helper."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET,http.server,threading,urllib.parse,xmlrpc.client,time,gzip
root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--global-client',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/nwc_http_reference.cs','bin/XMLRPC.dll','OpenSim/Framework/WebUtil.cs','OpenSim/Services/Interfaces/OpenProfileClient.cs','OpenSim/Framework/UserProfiles.cs','OpenSim/Framework/Util.cs','OpenSim/Services/Connectors/Hypergrid/UserAgentServiceConnector.cs']
s=(root/sources[2]).read_bytes().decode('utf-8-sig');start=s.index('public static class XMLRPCRequester');i=s.index('{',start)+1;depth=1
while depth:
 if s[i]=='{':depth+=1
 elif s[i]=='}':depth-=1
 i+=1
def extract(file,signature):
 text=(root/file).read_bytes().decode('utf-8-sig');first=text.index(signature);pos=text.index('{',first)+1;level=1
 while level:
  if text[pos]=='{':level+=1
  elif text[pos]=='}':level-=1
  pos+=1
 return text[first:pos]
detail=''.join(extract('OpenSim/Framework/WebUtil.cs',sig) for sig in ['public static void LogOutgoingDetail(string type, int reqnum, string output)','public static void LogOutgoingDetail(string context, string output)','public static void LogResponseDetail(int reqnum, string input)'])
binary=''.join(extract('OpenSim/Framework/Util.cs',sig) for sig in ['public static void BinaryToASCII(char[] chars)','public static string BinaryToASCII(string src)'])
agent_methods=''.join(extract('OpenSim/Services/Connectors/Hypergrid/UserAgentServiceConnector.cs',sig) for sig in ['public UserAgentServiceConnector(string url)','private bool setServiceURL(string url)','private Hashtable CallServer(string methodName, Hashtable hash)','public Dictionary<string,object> GetUserInfo (UUID userID)','private bool GetBoolResponse(XmlRpcRequest request, out string reason)','public GridRegion GetHomeRegion(UUID userID, out Vector3 position, out Vector3 lookAt)','public List<UUID> GetOnlineFriends(UUID userID, List<string> friends)'])
(w/'Extracted.cs').write_text('using System;using System.Collections;using System.Collections.Generic;using System.Net.Http;using System.Net;using GridRegion=OpenSim.Services.Interfaces.GridRegion;using System.Xml.Linq;using System.Reflection;using log4net;using Nwc.XmlRpc;using OpenMetaverse;using OpenSim.Framework;namespace OpenSim.Framework{'+s[start:i]+'public static partial class WebUtil{'+detail+'}public static partial class Util{'+binary+'}}namespace OpenSim.Services.Connectors.Hypergrid{public class UserAgentServiceConnector{static ILog m_log=LogManager.GetLogger(typeof(UserAgentServiceConnector));string m_ServerURL;'+agent_methods+'public bool Bool(out string reason){return GetBoolResponse(new XmlRpcRequest("verify_agent",new ArrayList{new Hashtable()}),out reason);}}}')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
for f in [root/sources[0],root/sources[3],root/sources[4],root/'native-port/tests/user_agent_region_reference_stubs.cs',root/'OpenSim/Services/Interfaces/IGridService.cs',root/'OpenSim/Framework/RegionFlags.cs',w/'Extracted.cs']:ET.SubElement(items,'Compile',Include=str(f))
for name in ['XMLRPC','OpenMetaverse','OpenMetaverseTypes']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
enc=lambda s:'-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
record_lock=threading.Lock();records=[];connections=[]
def xml(value):return xmlrpc.client.dumps((value,),methodresponse=True,allow_none=True).encode('utf-8')
def profile():return {'data':[dict(ProfileUrl='https://profile.test/é',AboutText='remote 😀 <&>\r\n',FirstLifeAboutText='life',Image='00000000-0000-0000-0000-000000000009',FirstLifeImage='00000000-0000-0000-0000-000000000008',Partner='00000000-0000-0000-0000-000000000007',wantmask=-1,wanttext='wants',skillsmask=-2147483648,skillstext='skills',languages='en fr')]}
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def log_message(self,*args):pass
 def do_GET(self):self.route()
 def do_POST(self):self.route()
 def route(self):
  body=self.rfile.read(int(self.headers.get('Content-Length','0')));selected={k.lower():v for k,v in self.headers.items() if k.lower() in ['content-type','content-length','connection','keep-alive','expect','transfer-encoding','cookie','accept','user-agent','accept-encoding','authorization','host']}
  with record_lock:
   records.append(dict(method=self.command,target=self.path,headers=selected,body=body.hex()))
   connections.append(self.client_address)
  path=urllib.parse.urlsplit(self.path).path.rstrip('/') or '/';query=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
  status=200;reason=None;headers={};data=xml(dict(number=7,text='remote 😀',binary=xmlrpc.client.Binary(b'\x00\xff'),date=xmlrpc.client.DateTime('20240229T12:34:56')))
  if path.startswith('/redirect/'):
   status=int(path.rsplit('/',1)[1]);headers['Location']=query.get('to',['/echo'])[0];data=b''
  elif path=='/loop':status=302;headers['Location']='/loop';data=b''
  elif path=='/echo':data=xml(dict(method=self.command,path=self.path,bodySHA256=hashlib.sha256(body).hexdigest(),headers=selected))
  elif path=='/setcookie':status=302;headers['Location']='/echo';headers['Set-Cookie']='session=must-not-return; Path=/';data=b''
  elif path.startswith('/profile'):
   value=profile()
   if path=='/profile-empty':value={'data':[]}
   if path=='/profile-bad-uuid':value['data'][0]['Image']='bad uuid'
   if path=='/profile-bad-mask':value['data'][0]['wantmask']='overflow2147483648'
   data=xml(value)
  elif path.startswith('/home'):
   value=dict(result='True',uuid='00000000-0000-0000-0000-000000000009',x='1000',y='2000',size_x='512',size_y='1024',region_name='home é😀',hostname='HOST.test',http_port='9000',internal_port='65535',server_uri='https://HOST/Case',position='<1.25, -2.5, 3.75>',lookAt='<0, 1, 0>')
   if path=='/home-false':value['result']='false'
   elif path=='/home-bad-port':value['internal_port']='-1'
   elif path=='/home-bad-vector':value['position']='bad'
   elif path=='/home-bad-look':value['lookAt']=47
   elif path=='/home-missing':value={'result':'True'}
   data=xml(value)
  elif path.startswith('/friends'):
   value={'friend_0':'00000000-0000-0000-0000-000000000009'}
   if path=='/friends-empty':value={}
   elif path=='/friends-bad':value={'friend_0':'invalid uuid'}
   elif path=='/friends-null':value={'friend_0':None}
   data=xml(value)
  elif path.startswith('/bool'):
   value={'result':'True'}
   if path=='/bool-false':value={'result':'false'}
   elif path=='/bool-bad':value={'result':47}
   elif path=='/bool-null':value={'result':None}
   elif path=='/bool-missing':value={}
   data=xml(value)
  elif path.startswith('/userinfo'):
   data=xml(dict(user_flags=47,user_created=-31,title='remote é😀',URL='https://Home/Case',nested=[True,9],binary=xmlrpc.client.Binary(b'\x00\xff'),nothing=None))
   if path=='/userinfo-empty':data=xml({})
  elif path=='/fault':data=xmlrpc.client.dumps(xmlrpc.client.Fault(9,'remote fault')).encode()
  elif path=='/fault-bad':data=b'<methodResponse><fault><value><struct></struct></value></fault></methodResponse>'
  elif path=='/null':data=b'<methodResponse><params><param><value/></param></params></methodResponse>'
  elif path=='/string':data=xml('simple')
  elif path=='/badxml':data=b'<methodResponse><params><param><value><int>bad</int></value></param></params></methodResponse>'
  elif path=='/gzip':data=gzip.compress(data);headers['Content-Encoding']='gzip'
  elif path=='/status':status=int(query.get('code',['404'])[0]);reason=query.get('reason',[None])[0];data=b'ignored non XML error'
  elif path.startswith('/bom/'):
   codec=path.rsplit('/',1)[1];bom={'utf8':b'\xef\xbb\xbf','utf16le':b'\xff\xfe','utf16be':b'\xfe\xff','utf32le':b'\xff\xfe\x00\x00','utf32be':b'\x00\x00\xfe\xff'}[codec];encoding={'utf8':'utf-8','utf16le':'utf-16-le','utf16be':'utf-16-be','utf32le':'utf-32-le','utf32be':'utf-32-be'}[codec];data=bom+data.decode('utf-8').encode(encoding)
  if path=='/slow-header':time.sleep(.35)
  try:
   self.send_response(status,reason);self.send_header('Content-Type','text/xml; charset=iso-8859-1')
   for k,v in headers.items():self.send_header(k,v)
   chunked=path=='/chunked';self.send_header('Transfer-Encoding','chunked') if chunked else self.send_header('Content-Length',str(len(data)));self.end_headers()
   if chunked:
    for i in range(0,len(data),7):part=data[i:i+7];self.wfile.write(('%x\r\n'%len(part)).encode()+part+b'\r\n');self.wfile.flush()
    self.wfile.write(b'0\r\n\r\n')
   elif path=='/slow-body':self.wfile.write(data[:10]);self.wfile.flush();time.sleep(.35);self.wfile.write(data[10:])
   else:self.wfile.write(data)
   self.wfile.flush()
  except (BrokenPipeError,ConnectionResetError,OSError):pass
class Server(http.server.ThreadingHTTPServer):
 def handle_error(self,request,client_address):pass
server=Server(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();base=f'http://127.0.0.1:{server.server_port}';rows=[]
def add(path,mode=0,method='test.method',payload='payload 😀',timeout=30000,absolute=False):rows.append(f'{mode} {timeout} {enc(method)} {enc(payload)} {enc(path if absolute else base+path)}')
for path in ['/reply','/echo','/chunked','/bom/utf8','/bom/utf16le','/bom/utf16be','/bom/utf32le','/bom/utf32be','/string','/null','/fault','/fault-bad','/badxml','/gzip','/status?code=404','/status?code=500&reason=','/status?code=503&reason=Custom','/loop','/setcookie','/echo/é?q=😀#fragment']:
 add(path)
for status in [301,302,303,307,308]:add(f'/redirect/{status}?to=/echo')
for path in ['/profile','/profile-empty','/profile-bad-uuid','/profile-bad-mask','/fault','/fault-bad','/null','/string','/badxml','/status?code=404','/redirect/302?to=/profile','/chunked','/bom/utf16le']:
 add(path,mode=1,payload='seed 😀')
# Multi-member CLR Hashtable enumeration is process-randomized. Use
# zero/one-member response maps for exact cross-process diagnostic logs.
for path in ['/profile-empty','/fault-bad','/null','/string','/badxml','/status?code=404']:
 add(path,mode=3,payload='debug seed 😀\r\n')
for path in ['/userinfo','/userinfo-empty','/fault','/fault-bad','/null','/string','/badxml','/status?code=404&end=/','/redirect/302?to=/userinfo','/chunked','/bom/utf16le']:
 add(path,mode=4)
for path in ['/bool','/bool-false','/bool-bad','/bool-null','/bool-missing','/fault','/fault-bad','/null','/string','/badxml','/status?code=404&end=/','/redirect/307?to=/bool']:
 add(path,mode=5)
for path in ['/home','/home-false','/home-bad-port','/home-bad-vector','/home-bad-look','/home-missing','/fault','/null','/string','/status?code=404&end=/','/redirect/302?to=/home','/bom/utf16le']:
 add(path,mode=6)
for path in ['/friends','/friends-empty','/friends-bad','/friends-null','/fault','/null','/string','/badxml','/status?code=404&end=/','/redirect/307?to=/friends']:
 add(path,mode=7)
for method,payload in [(None,None),('',''),('a.b.c','<&>"\r\n'),('test',chr(0xd800)),(chr(0xdc00),'test'),('test','x'*131073)]:add('/echo',method=method,payload=payload)
for url in [None,'','relative/path','ftp://example.test/a','http://127.0.0.1:bad/a','http://[invalid/']:
 add(url,absolute=True)
add('/slow-body',timeout=100);add('/slow-header',timeout=100)
outputs={};captures={};pool_reused={}
try:
 for label,cmd in [('source',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native]+(['--global-client'] if a.global_client else []))]:
  with record_lock:
   records.clear()
   connections.clear()
  r=subprocess.run(cmd,input='\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=120);assert r.returncode==0,r.stderr+r.stdout[-2000:];outputs[label]=r.stdout.splitlines();assert len(outputs[label])==len(rows),(label,len(outputs[label]),len(rows))
  with record_lock:
   captures[label]=list(records)
   assert [item['target'] for item in records[:2]]==['/reply','/echo']
   pool_reused[label]=len(connections)>=2 and connections[0]==connections[1]
finally:server.shutdown();server.server_close()
diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(rows,outputs['source'],outputs['native']) if s!=n]
wire_diff=[]
for i in range(max(len(captures['source']),len(captures['native']))):
 s=captures['source'][i] if i<len(captures['source']) else None;n=captures['native'][i] if i<len(captures['native']) else None
 if s!=n:wire_diff.append(dict(index=i,source=s,native=n))
report=dict(passed=not diffs and not wire_diff,scenarios=len(rows),differenceCount=len(diffs),differences=diffs[:15],wireDifferenceCount=len(wire_diff),wireDifferences=wire_diff[:8],sourceRequests=len(captures['source']),nativeRequests=len(captures['native']),applicationParityVerified=False,scope='Actual bundled Nwc client.Send with real HTTP and unchanged OpenProfileClient/XMLRPCRequester helper. Controlled WebUtil global client/log/tick boundary: source handler configured as original no cookies/proxy/decompression, redirect10 and shared lifetime; native explicit direct/error policies. Actual native user-agent GetUserInfo/CallServer (11 flows), GetBoolResponse (12 flows), GetHomeRegion (12 flows) and GetOnlineFriends (10 flows) now compose with real10000ms HTTP, exact reasons and logs; native wrapper/default diagnostic formatter/detail logging also composes with transport;6 DebugLevel5 scenarios compare exact stable-map logs excluding nondeterministic elapsed durations. Exact captured method/target/selected headers/body, typed outcomes and profile partial state, redirects/limit, faults/errors, BOM/charset/chunked/gzip, early XML failure, large POST and separate header/body timeout. Handler pooling, TLS/proxy/credentials/global policy and exact all disposal observability/full production host/application/macOS remain pending.',sourceHashes={s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources})
report['sharedNativeClient']=a.global_client
report['reusedFirstConnection']=pool_reused
if a.global_client:
 report['passed']=report['passed'] and all(pool_reused.values())
 report['scope']+=' Shared mode exercises the native WebUtil handler and verifies reuse across the first two XML-RPC calls.'
(w/'results.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],f'{len(diffs)} outcomes/{len(wire_diff)} wire differences, reuse={pool_reused}; see '+str(w/'results.json');print('PASS:',len(rows),'real Nwc HTTP and legacy profile scenarios;',len(captures['native']),'wire requests')
