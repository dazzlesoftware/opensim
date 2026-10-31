"""Real body-less DELETE requests and lifecycle error handling; no scene claimed."""
import argparse,http.server,json,subprocess,threading,time
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 class Handler(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1';profile='success';requests=[]
  def log_message(self,*args):pass
  def do_DELETE(self):
   body=self.rfile.read(int(self.headers.get('Content-Length','0')));self.requests.append({'method':self.command,'path':self.path,'headers':dict(self.headers),'body':body.hex()})
   if self.profile=='disconnect':self.close_connection=True;return
   if self.profile=='delay':time.sleep(2)
   status=503 if self.profile=='error' else 200;data=b'false' if self.profile=='false' else b'OpenSim agent fixture'
   self.send_response(status);self.send_header('Content-Length',str(len(data)));self.send_header('Connection','close');self.end_headers()
   try:self.wfile.write(data)
   except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
   self.close_connection=True
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();url=f'http://127.0.0.1:{server.server_port}/'
 try:
  for profile in ['success','false','error','disconnect']:
   Handler.profile=profile;Handler.requests=[];r=subprocess.run([a.native,url],capture_output=True,text=True,timeout=15);check(r.returncode==0 and r.stdout=='1|1',profile+' release/close ignore transport or remote boolean failure');check(len(Handler.requests)==2,profile+' exactly two requests, no retry');expected=['/release/?auth=release%2Btoken','/agent/00000000-0000-0000-0000-000000000007/00000000-0000-0000-0000-000000000008/?auth=close%2Btoken']
   for i,request in enumerate(Handler.requests):
    headers={k.lower():v for k,v in request['headers'].items()};label=profile+'/'+str(i)
    check(request['method']=='DELETE' and request['path']==expected[i],label+' exact method and unencoded auth target')
    check(request['body']=='' and int(headers.get('content-length','0'))==0,label+' no JSON/null body')
    check('content-type' not in headers and 'x-content-encoding' not in headers,label+' no content or compression headers')
    check(headers.get('connection','').lower()=='close' and 'keep-alive' not in headers,label+' source close policy')
    check(headers.get('opensim-request-id','').isdigit(),label+' request ID header')
  Handler.profile='delay';start=time.monotonic();r=subprocess.run([a.native,'--transport',url+'timeout/','150'],capture_output=True,text=True,timeout=5);elapsed=time.monotonic()-start;value=json.loads(r.stdout);check(r.returncode==0 and value.get('Success')=='False' and .1<=elapsed<1.8,'transport applies supplied millisecond timeout before delayed response')
 finally:server.shutdown();server.server_close();thread.join(timeout=5)
 (a.work/'simulation-lifecycle-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'Native lifecycle connector/HTTP transport against controlled socket receiver, scene binding and full managed WebUtil host/runtime parity pending'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} native lifecycle socket checks')
if __name__=='__main__':main()
