"""Original requester redirects: methods, credentials, headers, cookies and hop cap."""
import argparse,base64,hashlib,http.server,json,subprocess,threading
from pathlib import Path
from compare_rest_forms import build_reference
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();reference,source,method=build_reference(a.dotnet,w);results=[];payload=b'done<&>';expected_auth='Basic '+base64.b64encode(b'service-user:service-password').decode()
 class Handler(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1';records=[];ports=[]
  def log_message(self,*args):pass
  def handle(self):
   try:super().handle()
   except (ConnectionResetError,ConnectionAbortedError,BrokenPipeError):
    if not getattr(self,'response_started',False):raise
  def do_GET(self):self.reply()
  def do_POST(self):self.reply()
  def reply(self):
   _,_,status,limit,index,cross=self.path.split('/');status,limit,index,cross=map(int,[status,limit,index,cross]);body=self.rfile.read(int(self.headers.get('Content-Length','0'))).decode();self.records.append(dict(method=self.command,path=self.path,headers={k.lower():v for k,v in self.headers.items()},body=body));self.response_started=True
   if index<limit:
    self.send_response(status);port=self.ports[1] if cross else self.server.server_port;self.send_header('Location',f'http://127.0.0.1:{port}/redirect/{status}/{limit}/{index+1}/{cross}');self.send_header('Set-Cookie','session=must-not-forward');self.send_header('Content-Length','0');self.end_headers()
   else:self.send_response(200);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as first,http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as second:
  Handler.ports=[first.server_port,second.server_port];threads=[threading.Thread(target=s.serve_forever,daemon=True) for s in [first,second]]
  for t in threads:t.start()
  try:
   cases=[(status,1,cross) for status in [300,301,302,303,307,308] for cross in [0,1]]+[(307,10,0),(307,11,0)]+[(status,1,0) for status in [304,305,306,309]]
   for status,limit,cross in cases:
    rows=[];follows=status in [300,301,302,303,307,308];succeeds=follows and limit<=10;count=min(limit+1,11) if follows else 1
    for name,command in [('source',[a.dotnet,str(reference)]),('native',[a.native])]:
     Handler.records=[];r=subprocess.run(command+[f'http://127.0.0.1:{first.server_port}/redirect/{status}/{limit}/0/{cross}','METHOD=probe','--auth'],capture_output=True,text=True,encoding='utf-8',timeout=30);assert r.returncode==0,r.stderr;out=r.stdout.strip();assert out==('OK|'+payload.hex() if succeeds else 'EX'),(status,limit,cross,name,out);records=Handler.records[:];assert len(records)==count,(status,limit,cross,name,len(records),count)
     for index,record in enumerate(records):
      get=index>0 and status in [300,301,302,303];headers=record['headers'];assert record['method']==('GET' if get else 'POST') and record['body']==('' if get else 'METHOD=probe'),(status,name,index,record)
      assert headers.get('authorization')==(expected_auth if index==0 else None),(status,name,index,headers)
      assert 'cookie' not in headers and all(k not in headers for k in ['accept','accept-encoding','user-agent']),(status,name,index,headers)
      assert headers.get('connection','').lower()=='keep-alive' and headers.get('keep-alive')=='timeout=30, max=10',(status,name,index,headers)
      assert headers.get('content-type')==(None if get else 'application/x-www-form-urlencoded'),(status,name,index,headers)
      assert headers.get('content-length')==(None if get else str(len('METHOD=probe'))),(status,name,index,headers)
      expected_port=second.server_port if cross and index>0 else first.server_port;assert headers.get('host')==f'127.0.0.1:{expected_port}',(status,name,index,headers)
     rows.append((out,records));results.append(dict(status=status,limit=limit,cross=cross,implementation=name,result=out,requests=len(records)))
    # Full captured method/body/header assertions above are checked independently
    # against source expectations; implementation-specific default headers never
    # get filtered into a misleading equality comparison.
  finally:
   first.shutdown();second.shutdown()
   for t in threads:t.join(timeout=5)
 report=dict(passed=True,observations=len(results),profiles=len(cases),sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),methodSha256=hashlib.sha256(method.encode()).hexdigest(),scope='Exact original MakeRequest POST method and real .NET HttpClient with projected source handler defaults. Same/cross-origin redirects, method/body/content-header changes, authorization removal, no cookies/default accept/user-agent headers, ten-hop cap and non-followed statuses. Full factory/proxy/TLS/pool/stream/adaptive timeout/runtime and Unix behavior remain pending.',results=results);(w/'rest-redirects-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(f'PASS: {len(results)} original/native redirect observations')
if __name__=='__main__':main()
