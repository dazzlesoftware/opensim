"""Real native access receiver and native simulation client; no scene admission."""
import argparse,http.client,json,queue,subprocess,threading,urllib.parse
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--client',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(value,name):
  if not value:raise AssertionError(name)
  checks.append(name)
 process=subprocess.Popen([a.server],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);output=queue.Queue()
 def read():
  for line in process.stdout:output.put(line.strip())
 thread=threading.Thread(target=read,daemon=True);thread.start()
 try:
  address=output.get(timeout=20);check(address.startswith('LISTENING '),'native access receiver starts');port=urllib.parse.urlsplit(address.split(' ',1)[1]).port
  one='00000000-0000-0000-0000-000000000001';two='00000000-0000-0000-0000-000000000002';nine='00000000-0000-0000-0000-000000000009';path='/agent/'+one+'/'+two+'/'
  def request(body=None,target=path,headers=None,connection=None,method='QUERYACCESS'):
   own=connection is None;connection=connection or http.client.HTTPConnection('127.0.0.1',port,timeout=10)
   try:
    connection.request(method,target,body=json.dumps(body) if body is not None else None,headers={'Content-Type':'application/json',**(headers or {})});response=connection.getresponse();status=response.status;keep=response.getheader('Connection');data=response.read();return status,keep,json.loads(data)
   finally:
    if own:connection.close()
  normal={'simulation_service_supported_min':0.3,'simulation_service_supported_max':0.8,'simulation_service_accepted_min':0.3,'simulation_service_accepted_max':0.8,'features':[one],'position':'<1, 2, 3>'}
  connection=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
  try:
   status,keep,data=request(normal,connection=connection);check(status==200 and keep.lower()=='keep-alive' and data['success'] is True,'QUERYACCESS dispatch supports accepted keepalive response');socket=connection.sock
   check(data['features']==[one,nine[:-1]+'8'] and abs(data['negotiated_inbound_version']-0.8)<1e-7 and abs(data['negotiated_outbound_version']-0.8)<1e-7,'source negotiation uses local versions and returns modified feature list')
   status,keep,data=request(normal,headers={'X-SecondLife-Shard':'fixture'},connection=connection);check(status==200 and data['success'] is True and connection.sock is socket,'accepted custom method reuses TCP connection and preserves source shard non-return behavior')
  finally:connection.close()
  before=request(target='/stats',method='GET')[2]['queries'];status,keep,data=request({});check(status==200 and data['success'] is False and keep.lower()=='close' and 'old version' in data['reason'],'missing protocol version rejects before service with source reason');check(request(target='/stats',method='GET')[2]['queries']==before,'early protocol rejection skips simulation service')
  status,keep,data=request({**normal,'simulation_service_supported_min':0.9});check(data['success'] is False and 'No version overlap' in data['reason'],'nonoverlapping supported range rejected')
  status,keep,data=request({'my_version':'SIMULATION/0.5'});check(data['success'] is True and data['version']=='SIMULATION/0.5' and data['negotiated_inbound_version']==0 and data['negotiated_outbound_version']==0,'legacy context and negotiated-zero response behavior preserved')
  status,keep,data=request(normal,target='/agent/'+nine+'/'+two+'/');check(status==200 and data['success'] is False and data['reason']=='denied by fixture' and keep.lower()=='close','service denial closes keepalive and preserves reason')
  check(request(normal,target='/agent/bad/')[0]==404 and request(normal,target='/agent/'+one+'/bad/')[0]==400,'path and zero-region validation gates access')
  check(request(normal,target='/agent/'+one+'/'+two+'/',method='POST')[0]==405,'fixture does not fabricate create-agent admission')
  check(request(target='/ready',method='GET')[0]==503,'query negotiation does not imply scene readiness')
  result=subprocess.run([a.client,f'http://127.0.0.1:{port}/'],capture_output=True,text=True,timeout=20);check(result.returncode==0,'native simulation connector completes receiver interoperability');values=json.loads(result.stdout);check(values['query'] is True and values['create'] is False and values['features']==0 and '405' in values['create_reason'],'native client accepts negotiation, preserves feature wrapper quirk and reports absent scene admission')
 finally:
  if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
  try:process.wait(timeout=15)
  except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
  thread.join(timeout=5)
 check(process.returncode==0,'access receiver shuts down and joins workers');(a.work/'agent-access-network-results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} native inbound access socket observations')
if __name__=='__main__':main()
