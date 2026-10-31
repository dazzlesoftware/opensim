"""Exercise real QUERYACCESS and gzip/plain create-agent requests over sockets."""
import argparse,gzip,http.server,json,subprocess,tempfile,threading
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(value,name):
  if not value:raise AssertionError(name)
  checks.append(name)
 # Verify platform HttpClient error text directly, as used by the original WebUtil.
 reference=a.work/'http-error-reference';reference.mkdir(parents=True,exist_ok=True)
 (reference/'HttpError.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType></PropertyGroup></Project>',encoding='utf-8')
 (reference/'Program.cs').write_text('using System;using System.Net.Http;using System.Text.Json;class Program{static void Main(string[] args){using var client=new HttpClient();using var request=new HttpRequestMessage(new HttpMethod("QUERYACCESS"),args[0]){Content=new StringContent("{}")};try{using var response=client.Send(request);response.EnsureSuccessStatusCode();Console.Write(JsonSerializer.Serialize("success"));}catch(HttpRequestException e){Console.Write(JsonSerializer.Serialize("Service request failed: ["+(int?)e.StatusCode+"] "+e.Message));}}}',encoding='utf-8')
 built=subprocess.run([a.dotnet,'build',str(reference/'HttpError.csproj'),'-c','Release','-o',str(reference/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
 if built.returncode:raise RuntimeError(built.stdout+built.stderr)
 class Handler(http.server.BaseHTTPRequestHandler):
  profile='success';requests=[]
  def log_message(self,*args):pass
  def reply(self,status,data):
   data=json.dumps(data).encode() if not isinstance(data,bytes) else data;self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def read(self):
   body=self.rfile.read(int(self.headers['Content-Length']));wire_length=len(body);compressed=self.headers.get('X-Content-Encoding')=='gzip'
   if compressed:body=gzip.decompress(body)
   data=json.loads(body);self.requests.append((self.command,self.path,dict(self.headers),data,compressed,wire_length));return data,compressed
  def do_QUERYACCESS(self):
   self.read()
   if self.profile=='denied':self.reply(200,{'success':False,'reason':'denied'});return
   if self.profile in ['method','custommethod','noreason']:
    self.send_response(405, 'Region temporarily unavailable' if self.profile=='custommethod' else '' if self.profile=='noreason' else None);self.send_header('Content-Length','0');self.end_headers();return
   if self.profile=='malformed':self.reply(200,b'not json');return
   self.reply(200,{'success':True,'reason':'access ok','negotiated_inbound_version':0.4,'negotiated_outbound_version':0.4,'features':['00000000-0000-0000-0000-000000000005']})
  def do_POST(self):
   data,compressed=self.read()
   if self.profile=='fallback' and compressed:self.reply(415,b'compressed transfer unsupported');return
   if self.profile=='reject':self.reply(200,{'success':False,'reason':'agent denied'});return
   if self.profile=='raw':self.reply(200,b'true');return
   self.reply(200,{'success':True,'reason':'created'})
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
  thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  try:
   for profile in ['success','large','fallback','reject','raw','denied','method','custommethod','noreason','malformed']:
    Handler.profile=profile;Handler.requests=[];url=f'http://127.0.0.1:{server.server_port}/base/'
    # One 30-second query and up to two 30-second create requests are source-defined.
    # The process watchdog must not kill a client within these request deadlines.
    run=subprocess.run([a.native,url]+(['large'] if profile=='large' else []),capture_output=True,text=True,encoding='utf-8',timeout=95);check(run.returncode==0,profile+' connector process completes');result=json.loads(run.stdout);requests=Handler.requests
    check(requests[0][0]=='QUERYACCESS' and requests[0][1]=='/base/agent/00000000-0000-0000-0000-000000000007/00000000-0000-0000-0000-000000000001/',profile+' original query method and URL')
    headers={k.lower():v for k,v in requests[0][2].items()};data=requests[0][3]
    check(headers['content-type']=='application/json' and headers['opensim-request-id']=='0' and headers['connection'].lower()=='keep-alive',profile+' query headers')
    check(data['viaTeleport'] is True and data['position']=='<1.25, 2, 30>' and data['my_version']=='SIMULATION/0.3' and len(data['features'])==1 and 'agent_home_uri' not in data,profile+' source query JSON')
    if profile in ['denied','method','custommethod','noreason','malformed']:
     check(not result['query'] and len(requests)==1,profile+' access failure skips agent creation')
     check(result['query_reason']==('denied' if profile=='denied' else '' if profile=='malformed' else 'Service request failed: [405] Response status code does not indicate success: 405.' if profile=='noreason' else 'Service request failed: [405] Response status code does not indicate success: 405 ('+('Region temporarily unavailable' if profile=='custommethod' else 'Method Not Allowed')+').' ),profile+' access failure reason')
     if profile in ['method','custommethod','noreason']:
      original=subprocess.run([a.dotnet,str(reference/'app/HttpError.dll'),url],capture_output=True,text=True,encoding='utf-8',timeout=15)
      check(original.returncode==0 and result['query_reason']==json.loads(original.stdout),profile+' actual .NET HttpClient error text')
     continue
    check(result['query'] and result['features']==0 and result['context']['WearablesCount']==15,profile+' negotiated versions and original outer-feature-list quirk')
    posts=requests[1:];check(len(posts)==(2 if profile=='fallback' else 1),profile+' original compressed retry count')
    for index,post in enumerate(posts):
     headers={k.lower():v for k,v in post[2].items()};data=post[3]
     check(post[0]=='POST' and post[1]=='/base/agent/00000000-0000-0000-0000-000000000007/' and post[4]==(index==0),profile+' create URL and gzip framing')
     check(headers['content-type']=='application/json' and 'content-encoding' not in headers and headers['opensim-request-id']==str(index+1),profile+' legacy-safe gzip header and request ID')
     check(data['destination_uuid']=='00000000-0000-0000-0000-000000000001' and data['teleport_flags']=='4096' and data['context']['WearablesCount']==15 and data['packed_appearance']['wearables'],profile+' complete circuit/context/appearance payload')
    if profile=='large':
     check(posts[0][3]['serviceurls']['Large']=='x'*150000,'gzip stream retains large circuit service URLs')
     check(posts[0][5]<len(json.dumps(posts[0][3]).encode())//10,'large repetitive circuit payload is actually compressed')
    check(result['create']==(profile in ['success','large','fallback']) and result['create_reason']==('created' if profile in ['success','large','fallback'] else 'agent denied' if profile=='reject' else ''),profile+' original admission result')
  finally:server.shutdown();thread.join(timeout=5)
 # Connection refusal is a service failure, rather than an invented successful admission.
 run=subprocess.run([a.native,url],capture_output=True,text=True,encoding='utf-8',timeout=35);check(run.returncode==0 and not json.loads(run.stdout)['query'],'unreachable simulator rejects access')
 (a.work/'simulation-transport-network-results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8');print(f'PASS: {len(checks)} simulation HTTP transport observations')
if __name__=='__main__':main()
