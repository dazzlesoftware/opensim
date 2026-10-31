"""Native registry and capability dispatch adapter over real HTTP sockets."""
import argparse,http.client,json,queue,subprocess,threading
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);proc=subprocess.Popen([a.server],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);q=queue.Queue();checks=[]
 def reader():
  for line in proc.stdout:q.put(line.strip())
 t=threading.Thread(target=reader,daemon=True);t.start()
 try:
  line=q.get(timeout=20);assert line.startswith('LISTENING '),line;port=int(line.split()[1])
  def request(path,method='GET',body=None):
   c=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
   try:c.request(method,path,body=body);r=c.getresponse();return r.status,r.read(),dict(r.getheaders())
   finally:c.close()
  def check(ok,name):
   assert ok,name
   checks.append(name)
  check(request('/')[0:2]==(200,b'root|/|/|GET|'),'root streamed GET forces success status')
  check(request('/','POST')[0:2]==(404,b'fallback:/'),'root POST delegates to host RPC fallback')
  check(request('/normal/')[0:2]==(418,b'normal|/normal|/normal/|GET|'),'one trailing slash is removed from stream argument while original request path is retained')
  check(request('/normal//')[0:2]==(418,b'normal|/normal/|/normal//|GET|'),'stream matching preserves the source single-slash trim')
  check(request('/normal/deep/child')[1].startswith(b'specific|'),'longest stream prefix wins')
  check(request('/normal','POST')[0]==404,'normal route method remains part of its key')
  check(request('/generic/child')[1].startswith(b'generic|'),'generic stream key participates in prefix lookup')
  check(request('/simple/child','PUT',b'payload')[0:2]==(418,b'variable|simple|/simple/child|PUT|payload'),'simple variable path receives original method and body')
  check(request('/simple/exact')[1].startswith(b'exact|'),'exact simple route wins over variable prefix')
  check(request('/simple/exact/')[1].startswith(b'exact|'),'simple lookup trims trailing slash before exact match')
  check(request('/simple/')[0]==404,'variable simple route does not match its bare keyword')
  check(request('/simple/child/extra')[1].startswith(b'variable|'),'variable simple route uses only the first keyword')
  poll='/lslhttp/00000000-0000-0000-0000-000000000001'
  check(request(poll+'/request-id','POST')[0:2]==(200,('poll:'+poll+':POST').encode()),'variable poll dispatch selects LSL UUID prefix')
  check(request(poll,'OPTIONS')[1].endswith(b':OPTIONS'),'poll dispatch takes precedence over OPTIONS')
  check(request('/null-poll')[0]==500,'selected null poll value preserves handler failure')
  status,_,headers=request('/missing','OPTIONS');headers={k.lower():v for k,v in headers.items()};check(status==200 and headers['access-control-allow-origin']=='*' and headers['access-control-allow-methods']=='GET, POST, DELETE, PUT, OPTIONS','OPTIONS is global with original CORS headers')
  check(request('/control/remove','POST')[0]==200,'route removal control succeeds');check(request('/normal/deep/child')[1].startswith(b'normal|') and request('/simple/exact')[1].startswith(b'variable|'),'removing exact registrations reveals fallback routes')
  check(request('/control/root-nonstreamed','POST')[0]==200 and request('/')[0:2]==(404,b''),'nonstreamed root slot prevents fallback without calling stream handler')
 finally:
  if proc.poll() is None:proc.stdin.write('quit\n');proc.stdin.flush()
  try:proc.wait(timeout=15)
  except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
  t.join(timeout=5)
  if proc.returncode:raise RuntimeError(proc.stderr.read())
 (a.work/'http-route-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'Native source-backed registry and stream/simple capability dispatch over sockets. Poll execution and root RPC are controlled callbacks; full BaseHttpServer generic/RPC/WebSocket, transport defaults/metrics, async polling and platform parity remain pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} HTTP route socket checks')
if __name__=='__main__':main()
