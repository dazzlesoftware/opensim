"""Real outgoing native object requests and canonical HTTP/boolean/error responses."""
import argparse,http.server,json,subprocess,threading
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--client',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[];received=[];response=[200,b'True']
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 class Handler(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1'
  def log_message(self,*args):pass
  def do_POST(self):
   body=self.rfile.read(int(self.headers.get('Content-Length',0)));received.append((self.path,dict(self.headers),body));self.send_response(response[0]);self.send_header('Content-Length',str(len(response[1])));self.send_header('Content-Type','text/plain');self.end_headers();self.wfile.write(response[1])
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();url=f'http://127.0.0.1:{server.server_port}/base/'
 try:
  for status,body,expected in [(200,b'True',True),(200,b'False',False),(200,b'false',False),(200,b'true',True),(200,b'',True),(200,b'bad',True),(200,b'{"success":false}',True),(400,b'True',False),(500,b'True',False),(204,b'',True)]:
   response[:]=[status,body];before=len(received);run=subprocess.run([a.client,url],capture_output=True,text=True,timeout=15);check(run.returncode==0 and run.stdout==str(expected),f'HTTP {status} body {body!r} preserves service-wrapper success');check(len(received)==before+1,'object request sends once without agent-create gzip retry');path,headers,payload=received[-1];data=json.loads(payload);headers={k.lower():v for k,v in headers.items()};check(path=='/base/object/00000000-0000-0000-0000-000000000007/' and headers.get('connection')=='close' and 'x-content-encoding' not in headers,'object path UUID uncompressed POST and close connection');check(data['sog']=='<object/>' and data['extra']=='x'*150000 and data['state']=='scripts' and data['modified'] is True and data['new_position']=='<1.25, 2, 30>' and data['destination_uuid'].endswith('1'),'large XML extra/state/position/destination payload retained')
 finally:server.shutdown();server.server_close();thread.join(timeout=5)
 (a.work/'simulation-object-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'Native outgoing HTTP object transport, canonical response/error behavior and 150000-byte extra payload. Source-method oracle separately verifies 40000ms request setting; real 40-second deadline, TLS and full world object crossing remain pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} outgoing object HTTP checks')
if __name__=='__main__':main()
