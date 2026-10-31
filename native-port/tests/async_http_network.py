"""Actual deferred HTTP sockets with one request worker and nonblocking writes."""
import argparse,http.client,json,queue,socket,ssl,subprocess,threading,time
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--tls',action='store_true');a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[];sockets=[];command=[a.server]
 if a.tls:command += [str(Path(__file__).parent/'fixtures/localhost-cert.pem'),str(Path(__file__).parent/'fixtures/localhost-key.pem')]
 proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);lines=queue.Queue()
 def output():
  for line in proc.stdout:lines.put(line.strip())
 threading.Thread(target=output,daemon=True).start()
 try:
  line=lines.get(timeout=30);assert line.startswith('LISTENING '),line;port=int(line.split()[1]);context=ssl._create_unverified_context() if a.tls else None
  def connect():
   s=socket.create_connection(('127.0.0.1',port),timeout=5)
   if a.tls:s=context.wrap_socket(s,server_hostname='localhost')
   sockets.append(s);return s
  def request(path,method='GET',body=None):
   c=http.client.HTTPSConnection('127.0.0.1',port,timeout=5,context=context) if a.tls else http.client.HTTPConnection('127.0.0.1',port,timeout=5)
   try:c.request(method,path,body);r=c.getresponse();return r.status,r.read(),dict(r.getheaders())
   finally:c.close()
  def read(f,head=False):
   line=f.readline();assert line.startswith(b'HTTP/1.1 '),line;status=int(line.split()[1]);headers={}
   while True:
    line=f.readline();assert line,line
    if line==b'\r\n':break
    key,value=line.decode('latin1').split(':',1);headers[key.lower()]=value.strip()
   data=b'' if head else f.read(int(headers.get('content-length',0)));return status,data,headers
  def check(value,name):assert value,name;checks.append(name)
  held=[]
  for i in range(24):
   s=connect();s.sendall(f'POST /wait/{i} HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\ndata'.encode());held.append(s)
  end=time.monotonic()+5
  while request('/stats')[1]!=b'24:24':
   assert time.monotonic()<end,'Deferred requests exhausted the single worker';time.sleep(.01)
  check(request('/echo?x=1','POST',b'payload')[:2]==(200,b'POST|/echo|/echo?x=1|payload|HTTP/1.1'),'24 pending requests leave the single request worker free')
  for i,s in enumerate(held):
   check(request('/release/'+str(i))[0]==200,'release '+str(i));f=s.makefile('rb');check(read(f)[:2]==(200,('released:'+str(i)).encode()),'deferred response '+str(i));s.sendall(b'GET /reuse HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n');check(read(f)[:2]==(200,b'GET|/reuse|/reuse||HTTP/1.1'),'keepalive connection reused '+str(i));f.close();s.close()
  s=connect();f=s.makefile('rb');s.sendall(b'GET /wait/pipe HTTP/1.1\r\nHost: localhost\r\n\r\nGET /pipeline HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
  end=time.monotonic()+5
  while request('/stats')[1]!=b'1:1':assert time.monotonic()<end;time.sleep(.01)
  check(request('/release/pipe')[0]==200,'pipelined delayed response released');check(read(f)[:2]==(200,b'released:pipe'),'pipelined first response');check(read(f)[:2]==(200,b'GET|/pipeline|/pipeline||HTTP/1.1'),'buffered next request survives deferred response');f.close();s.close()
  check(request('/throw')[0]==500,'handler exception becomes500');check(request('/ready')[0]==503,'world readiness remains503')
  check(request('/head','HEAD')[1]==b'','HEAD suppresses body')
  for i in range(8):check(request('/asset-sized')[:2]==(200,b'a'*79234),'complete response beyond64KiB survives graceful close '+str(i))
  slow=connect();slow.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1024);slow.sendall(b'GET /large HTTP/1.1\r\nHost: localhost\r\n\r\n');time.sleep(.1)
  check(request('/responsive')[0]==200,'slow response reader does not block ordinary request worker');slow.close()
  dead=connect();dead.sendall(b'GET /wait/dead HTTP/1.1\r\nHost: localhost\r\n\r\n');end=time.monotonic()+5
  while request('/stats')[1]!=b'1:1':assert time.monotonic()<end;time.sleep(.01)
  dead.close();end=time.monotonic()+5
  while request('/stats')[1]!=b'1:0':assert time.monotonic()<end,'Disconnected deferred channel remained live';time.sleep(.01)
  check(True,'peer disconnect clears sendability');check(request('/cancel/dead')[0]==200,'closed retained channel can be cancelled')
  if a.tls:
   graceful=connect();graceful.sendall(b'GET /wait/graceful HTTP/1.1\r\nHost: localhost\r\n\r\n');end=time.monotonic()+5
   while request('/stats')[1]!=b'1:1':assert time.monotonic()<end;time.sleep(.01)
   raw=graceful.unwrap();raw.close();end=time.monotonic()+5
   while request('/stats')[1]!=b'1:0':assert time.monotonic()<end;time.sleep(.01)
   check(request('/cancel/graceful')[0]==200,'TLS close_notify acknowledged and deferred channel closed')
  cancelled=connect();cancelled.sendall(b'GET /wait/cancel HTTP/1.1\r\nHost: localhost\r\n\r\n');end=time.monotonic()+5
  while request('/stats')[1]!=b'1:1':assert time.monotonic()<end;time.sleep(.01)
  check(request('/cancel/cancel')[0]==200,'explicit deferred cancellation');check(cancelled.recv(1)==b'','cancellation closes socket')
  # Reject ambiguous framing through the unchanged parser path.
  malformed=connect();malformed.sendall(b'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n');f=malformed.makefile('rb');check(read(f)[0]==400,'ambiguous request framing rejected');f.close();malformed.close()
  stopped=connect();stopped.sendall(b'GET /wait/stop HTTP/1.1\r\nHost: localhost\r\n\r\n');end=time.monotonic()+5
  while request('/stats')[1]!=b'1:1':assert time.monotonic()<end;time.sleep(.01)
  proc.stdin.write('stop\n');proc.stdin.flush();proc.wait(timeout=10);assert proc.returncode==0,proc.stderr.read();check(stopped.recv(1)==b'','server shutdown closes deferred sockets')
  (a.work/'async-http-results.json').write_text(json.dumps({'passed':True,'checks':checks,'tls':a.tls,'scope':'Real deferred HTTP requests using one worker, keepalive, buffered pipelining, nonblocking output, disconnect/cancel/shutdown and existing parser rejection. Full original OSHttpServer wire/parser/queue/timeout/TLS lifecycle parity and Unix execution remain pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} asynchronous HTTP socket observations')
 finally:
  for s in sockets:s.close()
  if proc.poll() is None:
   proc.kill();proc.wait(timeout=5)
if __name__=='__main__':main()
