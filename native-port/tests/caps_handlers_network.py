"""Actual socket registration/removal using an exact-path controlled listener adapter."""
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
   try:c.request(method,path,body=body);r=c.getresponse();return r.status,r.read()
   finally:c.close()
  def check(ok,name):
   assert ok,name
   checks.append(name)
  def control(op,body=''):check(request('/control/'+op,'POST',body)[0]==200,'control '+op)
  control('normal','POST\n/caps/first');check(request('/caps/first','POST','payload')==(200,b'normal:POST:payload'),'registered stream receives original request body');check(request('/caps/first')[0]==404,'stream method isolation')
  control('normal','GET\n/caps/second');check(request('/caps/first','POST')[0]==404 and request('/caps/second')==(200,b'normal:GET:'),'replacement unregisters old stream')
  control('simple','GET\n/caps/simple');check(request('/caps/second')[0]==404 and request('/caps/simple','PUT','data')==(200,b'simple:PUT:data'),'simple replacement removes normal route and accepts methods')
  control('normal','GET\n/caps/normal');check(request('/caps/simple')[0]==200 and request('/caps/normal')[0]==200,'normal setter retains existing simple handler')
  check(json.loads(request('/details')[1])['X'].endswith('/caps/normal') and json.loads(request('/requested')[1])['X'].endswith('/caps/simple'),'all listing prefers normal while requested listing prefers simple')
  control('clear');check(request('/caps/normal')[0]==404 and request('/caps/simple')[0]==200,'null normal setter preserves simple route')
  control('remove');check(request('/caps/simple')[0]==404 and json.loads(request('/details')[1])=={},'remove unregisters both containers')
  control('hidden','GET\n/caps/hidden');check(request('/caps/hidden')[0]==404 and json.loads(request('/details')[1])['X'].endswith('/caps/hidden'),'unregistered simple capability remains advertised')
  control('remove');control('normal','HEAD\n/caps/head');control('remove');check(request('/caps/head','HEAD')[0]==200 and json.loads(request('/details')[1])=={},'source removal leaves HEAD route registered after dropping capability entry')
  check(request('/ready')[0]==503,'fixture does not claim world readiness')
 finally:
  if proc.poll() is None:proc.stdin.write('quit\n');proc.stdin.flush()
  try:proc.wait(timeout=15)
  except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
  t.join(timeout=5)
  if proc.returncode:raise RuntimeError(proc.stderr.read())
 (a.work/'caps-handlers-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'Native CapsHandlers and real HTTP sockets with exact-path controlled listener. Full BaseHttpServer routing/seed endpoint/world remain pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} capability handler socket checks')
if __name__=='__main__':main()
