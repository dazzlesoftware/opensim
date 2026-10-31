"""Native source-event dispatch + Caps/seed/event queue + actual sockets and client verification."""
import argparse,concurrent.futures,http.client,json,ssl,subprocess,time,urllib.parse,xml.etree.ElementTree as ET
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--tls',action='store_true');a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);command=[a.server]
 if a.tls:command += [str(Path(__file__).parent/'fixtures/localhost-cert.pem'),str(Path(__file__).parent/'fixtures/localhost-key.pem')]
 proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);checks=[];connections=[]
 def check(value,label):assert value,label;checks.append(label)
 try:
  line=proc.stdout.readline();assert line.startswith('LISTENING '),line+proc.stderr.read();port=int(line.split()[1]);scheme='https' if a.tls else 'http'
  def connect(host='127.0.0.1',target_port=port):
   c=http.client.HTTPSConnection(host,target_port,timeout=10,context=ssl._create_unverified_context()) if a.tls else http.client.HTTPConnection(host,target_port,timeout=10);connections.append(c);return c
  def request(path,body='',method='POST',ctype='application/llsd+xml'):
   uri=urllib.parse.urlparse(path);c=connect(uri.hostname or '127.0.0.1',uri.port or port)
   try:c.request(method,uri.path or path,body,headers={'Content-Type':ctype});r=c.getresponse();return r.status,r.read()
   finally:c.close()
  def state():return json.loads(request('/state')[1])
  def until(predicate,label):
   end=time.monotonic()+5
   while time.monotonic()<end:
    if predicate():return
    time.sleep(.01)
   raise AssertionError(label)
  def control(path,body=''):check(request(path,body)[0]==200,'control '+path)
  names=b'<llsd><array><string>EventQueueGet</string><string>SEED</string><string>ViewerBenefits</string><string>VTPBR</string><string>ObjectAnimation</string><string>EventQueueGet</string><string>Missing</string></array></llsd>'
  def seed(path):
   code,data=request(path,names);assert code==200,(code,data);items=list(ET.fromstring(data).find('map'));return {items[i].text:items[i+1].text for i in range(0,len(items),2)}
  initial=state();check(initial['caps']==initial['polls']==initial['simple']==initial['circuits']==1 and initial['commands']==5,'native module startup registers seed, event queue, circuit and commands');check(initial['event_errors']==initial['followers']==1,'registration exception isolated and later subscriber called')
  first='/CAPS/first0000'
  check(request(first,names,method='GET')[0]==404 and request(first,names,ctype='application/llsd+xml; charset=utf-8')[0]==404 and state()['checks']==0,'seed method and exact content type precede activation/client verification')
  seen=state()['seed_requests']
  with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
   waiting=pool.submit(seed,first)
   try:
    until(lambda:state()['seed_requests']>seen,'seed route not entered');check(not waiting.done() and state()['checks']==0,'seed handler waits on actual inactive Caps');control('/activate');caps=waiting.result(timeout=5)
   finally:
    if not waiting.done():request('/activate')
  check(set(caps)=={'EventQueueGet'},'seed lists only requested registered capability');url=caps['EventQueueGet'];uri=urllib.parse.urlparse(url);check(uri.scheme==scheme and uri.port==port and uri.path.startswith('/CE/'),'seed advertises actual protocol/listener and generated event route');check(state()['checks']==state()['verifications']==1 and state()['flags']==0x3901,'actual scene circuit/HG verification and viewer flags applied')
  c=connect(uri.hostname,uri.port);c.request('POST',uri.path,b'');until(lambda:state()['pending']==1,'viewer poll missing');check(request('/ready')[0]==503,'seed-discovered delayed poll leaves ordinary HTTP worker responsive');control('/physics');r=c.getresponse();data=r.read();check(r.status==200 and b'<string>ObjectPhysicsProperties</string>' in data and b'<integer>100</integer>' in data,'seed-discovered URL delivers native physics event');c.close()
  control('/register','first');check(state()['followers']==1,'same seed path reuses Caps without re-registering modules')
  control('/enqueue','<string>old</string>');control('/register','second');control('/enqueue','<string>new</string>');check(request(first,names)[0]==404 and state()['caps']==state()['polls']==state()['simple']==1,'replacement removes prior seed and poll registrations')
  code,data=request(url);check(code==200 and b'<string>old</string>' in data and b'<string>new</string>' not in data and b'<integer>101</integer>' in data,'replacement queue marker drains old batch before new seed activation');code,data=request(url);check(code==200 and b'<string>new</string>' in data and b'<integer>101</integer>' in data,'replacement queue resets sequence at source marker boundary');control('/activate');check(seed('/CAPS/second0000')['EventQueueGet']==url,'new seed advertises reused queue URL')
  control('/deregister');check(request(url)[0]==404 and request('/CAPS/second0000',names)[0]==404,'deregister event and Caps disposal remove seed/poll routes');control('/register','third');control('/activate');check(seed('/CAPS/third0000')['EventQueueGet']==url,'deregistration preserves queue until client-closed event');check(request(url)==(502,b''),'marker-only batch returns source empty502');control('/enqueue','<string>after</string>');code,data=request(url);check(code==200 and b'<integer>102</integer>' in data,'marker-only batch installs new sequence')
  control('/closed');s=state();check(s['caps']==s['polls']==s['simple']==s['circuits']==0 and request(url)[0]==404,'client close emits cleanup events and removes circuit/capability routes')
  control('/readmit','denied');control('/activate');control('/allow','0');check(request('/CAPS/denied0000',names)[0]==403,'denied HG verifier rejects native seed request');s=state();check(s['caps']==s['polls']==s['simple']==s['circuits']==0 and not s['presence'] and s['closed']==2,'verification denial logs out presence and closes scene capability resources')
  control('/allow','1');control('/readmit','disabled');control('/activate');control('/disable-eq');control('/register','noqueue');control('/activate');check(seed('/CAPS/noqueue0000')=={} and state()['polls']==0,'region module removal unsubscribes queue registration');control('/enable-eq');control('/register','restored');control('/activate');restored=seed('/CAPS/restored0000')['EventQueueGet'];check(restored!=url,'closed agent receives a fresh event queue path after module reattach');check(state()['event_errors']==state()['followers'] and state()['errors']==0,'all scene registration failures isolated without transport errors');control('/shutdown');check(state()['polls']==state()['simple']==0,'owner shutdown removes retained seed/queue routes')
  proc.stdin.write('stop\n');proc.stdin.flush();proc.wait(timeout=10);assert proc.returncode==0,proc.stderr.read();check(True,'native owner teardown completed without retained routes')
  (a.work/'scene-capabilities-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'tls':a.tls,'scope':'Actual native CapabilitiesModule, three scene event dispatchers, concrete Caps/seed, EventQueue, polling transport and circuit/presence client verification. Controlled world actors/HG verifier/event producer/admission; remaining capabilities, full world and Unix execution pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} scene capability network observations')
 finally:
  for c in connections:c.close()
  if proc.poll() is None:proc.kill();proc.wait(timeout=5)
if __name__=='__main__':main()
