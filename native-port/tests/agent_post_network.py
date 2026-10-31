"""Native compressed create-agent receiver fixture with real circuit-manager handoff."""
import argparse,gzip,http.client,json,queue,subprocess,threading,urllib.parse
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--client',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 process=subprocess.Popen([a.server,'admission'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);output=queue.Queue()
 def read():
  for line in process.stdout:output.put(line.strip())
 thread=threading.Thread(target=read,daemon=True);thread.start()
 try:
  address=output.get(timeout=20);check(address.startswith('LISTENING '),'admission receiver starts');port=urllib.parse.urlsplit(address.split(' ',1)[1]).port
  def request(body=None,headers=None,path='/agent/00000000-0000-0000-0000-000000000099/',method='POST'):
   connection=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
   try:
    connection.request(method,path,body=body,headers={'Content-Type':'application/json',**(headers or {})});response=connection.getresponse();status=response.status;keep=response.getheader('Connection');data=response.read();return status,keep,data
   finally:connection.close()
  def stats():return json.loads(request(path='/stats',method='GET')[2])
  base={'agent_id':'00000000-0000-0000-0000-000000000007','session_id':'00000000-0000-0000-0000-000000000008','secure_session_id':'00000000-0000-0000-0000-000000000006','circuit_code':'123456','destination_uuid':'00000000-0000-0000-0000-000000000001','destination_x':'25600','destination_y':'51200','destination_name':'Native','teleport_flags':128,'first_name':'First','last_name':'Last','start_pos':'<1, 2, 30>','caps_path':'seed'}
  for kind in ['plain','x-encoding','encoding','content-type']:
   data=json.dumps(base).encode();headers={}
   if kind!='plain':data=gzip.compress(data,compresslevel=9,mtime=0);headers={'X-Content-Encoding':'gzip'} if kind=='x-encoding' else {'Content-Encoding':'gzip'} if kind=='encoding' else {'Content-Type':'application/x-gzip'}
   status,keep,body=request(data,headers);value=json.loads(body);check(status==200 and value=={'success':True,'your_ip':'127.0.0.1'},kind+' source success response omits empty reason and echoes caller address');check(keep.lower()=='close',kind+' pure-login closes connection');snapshot=stats();check(snapshot['agent']==base['agent_id'] and snapshot['session']==base['session_id'] and snapshot['code']=='123456' and snapshot['authenticated'] is True,kind+' body circuit identity survives manager handoff despite mismatched path ID')
  before=stats()['creates'];status,keep,body=request(b'not gzip',{'X-Content-Encoding':'gzip'});check(status==400 and body==b'false' and stats()['creates']==before,'invalid compressed request rejected before service')
  denied={**base,'agent_id':'00000000-0000-0000-0000-000000000009','teleport_flags':0};status,keep,body=request(json.dumps(denied).encode());value=json.loads(body);check(status==200 and value=={'reason':'denied by fixture','your_ip':'127.0.0.1'} and keep.lower()=='keep-alive','source rejection omits false success and keeps non-login connection')
  hg={**base,'teleport_flags':1073741952};status,keep,body=request(json.dumps(hg).encode(),{'X-SecondLife-Shard':'fixture'});check(status==200 and json.loads(body)['success'] is True and keep.lower()=='keep-alive','HG/login flag combination and source shard non-return behavior preserved')
  before=stats()['creates'];status,keep,body=request(json.dumps({**base,'source_uuid':'bad'}).encode());check(status==500 and stats()['creates']==before,'invalid source UUID propagates outside circuit-unpack boundary')
  check(request(json.dumps(base).encode(),path='/agent/bad/')[0]==404 and request(b'[]')[0]==400,'URI and JSON-map validation reject malformed requests')
  check(request(path='/ready',method='GET')[0]==503,'circuit fixture does not claim native scene readiness')
  for profile in ['normal','large']:
   before=stats()['creates'];run=subprocess.run([a.client,f'http://127.0.0.1:{port}/']+(['large'] if profile=='large' else []),capture_output=True,text=True,timeout=20);check(run.returncode==0,profile+' native simulation client completes');value=json.loads(run.stdout);check(value['query'] is True and value['create'] is True and value['create_reason']=='',profile+' native query and gzip create-agent interoperate');snapshot=stats();check(snapshot['creates']==before+1 and snapshot['authenticated'] is True and snapshot['urls']==(1 if profile=='large' else 0) and (profile!='large' or snapshot.get('large_bytes')==150000),profile+' first compressed request succeeds without fallback and retains service URLs')
 finally:
  if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
  try:process.wait(timeout=15)
  except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
  thread.join(timeout=5)
 check(process.returncode==0,'admission fixture shuts down cleanly');(a.work/'agent-post-network-results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} native create-agent socket observations')
if __name__=='__main__':main()
