"""Actual native admission/access flow over HTTP with native circuits and controlled world services."""
import argparse,http.client,json,queue,subprocess,threading,urllib.parse
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 uid=lambda n:'00000000-0000-0000-0000-00000000000'+str(n)
 for mode,options in [('normal',''),('child-lease','ExistingChild=true\nInTransit=true\nMissingPresence=true\n'),('private','[Access]\nPrivate=true\n'),('missing','MissingPresence=true\n'),('no-service','NoPresenceService=true\n'),('disabled','LoginsEnabled=false\n'),('caps-failure','FailCaps=true\n')]:
  config=a.work/(mode+'.ini');config.write_text('[Modules]\nSimulationServices=LocalSimulationConnectorModule\n[Admission]\nEnabled=true\n'+options,encoding='utf-8');process=subprocess.Popen([a.server,str(config)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);lines=queue.Queue()
  def read():
   for line in process.stdout:lines.put(line.strip())
  thread=threading.Thread(target=read,daemon=True);thread.start()
  try:
   address=lines.get(timeout=20);check(address.startswith('LISTENING '),mode+' server starts');port=urllib.parse.urlsplit(address.split(' ',1)[1]).port
   def request(method,path,value=None):
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    try:c.request(method,path,body=None if value is None else json.dumps(value).encode(),headers={'Content-Type':'application/json'});r=c.getresponse();return r.status,r.read()
    finally:c.close()
   def stats():return json.loads(request('GET','/stats')[1])['a']
   def post(agent=8,flags=128,**extra):
    body={'agent_id':uid(agent),'session_id':uid(7),'secure_session_id':uid(8),'circuit_code':str(12340+agent),'destination_uuid':uid(1),'teleport_flags':flags,'first_name':'Test','last_name':'User','viewer':'Viewer 1','caps_path':'native-seed','start_pos':'<-5, 512, 30>',**extra};return request('POST','/agent/'+uid(agent)+'/',body)
   status,body=post();s=stats()
   if mode=='child-lease':
    check(status==200 and json.loads(body)['success'] is True and s['circuits']==1 and s['authenticated'],'existing child lease admits despite unavailable grid presence')
    check(s['presence_state']==0 and s['presence_not_close'] and s['presence_flags']==128 and s['presence_adjustments']==1,'native presence machine transitions PreRemove to Running and retains teleport lease')
    check(s['admission_trace']=='incoming,scene-presence,transit,land,account-cache,add,seeds,create-caps,adjust,name-cache,activate','existing child skips grid verification and authorization but refreshes caps and seeds')
   elif mode=='normal':
    check(status==200 and json.loads(body)['success'] is True and s['circuits']==1 and s['authenticated'],'admission retains authenticatable native circuit');check(s['circuit_position']=='<1, 255, 30>' and s['circuit_flags']==128 and s['caps_path']=='native-seed' and s['activated']==1,'root position flags and caps initialized');check(s['admission_trace']=='incoming,scene-presence,land,account-cache,add,grid-presence,authorize,seeds,create-caps,name-cache,activate','source admission callback order over HTTP')
    status,body=post(agent=3);s=stats();check(status==200 and json.loads(body).get('success',False) is False and json.loads(body)['reason']=='authorization denied' and s['circuits']==1 and s['admission_trace'].endswith('authorize,remove'),'authorization rejection rolls back only incoming agent circuit')
    status,body=post(agent=4);check(status==200 and json.loads(body)['reason']=='Denied access to region Scene1: You have been banned from that region.','admission applies actual native estate authorization policy')
    before=stats()['activated'];status,body=post(viewer='Banned 1');s=stats();check(status==200 and json.loads(body)['reason']=='Access denied, your viewer is banned' and s['admission_trace']=='incoming' and s['activated']==before,'viewer restriction rejects before presence and caps callbacks')
    for viewer,channel in [('4.5.1','Banned Channel'),('ΣVIEWER 1',None),('𐐀VIEWER 1',None),('  BANNED 1  ',None)]:
     extra={'viewer':viewer}
     if channel is not None:extra['channel']=channel
     status,body=post(**extra);s=stats();check(status==200 and json.loads(body)['reason']=='Access denied, your viewer is banned' and s['admission_trace']=='incoming','native viewer channel/Unicode casing applies ban before admission')
   elif mode=='private':
    check(status==200 and json.loads(body).get('success',False) is False and s['circuits']==0 and s['admission_trace'].endswith('authorize,remove'),'private estate login rejected with circuit rollback');status,body=post(flags=0);s=stats();check(status==200 and json.loads(body)['success'] is True and s['circuits']==1 and s['circuit_position']=='<-5, 512, 30>' and 'land' not in s['admission_trace'],'default child admission preserves source see-into-region bypass and position')
   elif mode in ['missing','no-service']:
    value=json.loads(body);check(status==200 and value.get('success',False) is False and s['circuits']==0 and s['activated']==0 and s['admission_trace'].endswith('remove'),'presence verification failure removes optimistic circuit')
    expected='Failed to verify user presence in the grid for Test User'+(' in region Scene1. Presence service does not exist.' if mode=='no-service' else ', access denied to region Scene1.');check(value['reason']==expected,'presence error retains original reason')
   elif mode=='disabled':check(status==200 and json.loads(body)['reason']=='Logins to this region are disabled' and s['circuits']==0 and s['admission_trace']=='incoming','disabled region rejects before circuit setup')
   elif mode=='caps-failure':check(status==500 and s['circuits']==1 and s['caps_path']=='native-seed' and s['activated']==0 and s['admission_trace'].endswith('seeds,create-caps'),'caps exception propagates and preserves original partial circuit state')
   check(request('GET','/ready')[0]==503,'controlled services do not claim full world readiness')
  finally:
   if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
   try:process.wait(timeout=15)
   except subprocess.TimeoutExpired:process.kill();process.wait();raise
   thread.join(timeout=5)
   if process.returncode:raise RuntimeError(process.stderr.read())
 (a.work/'scene-admission-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'scope':'Native HTTP/local connector/NewUserConnection/SceneAccess with actual circuits and controlled presence/cache/caps/estate/group/parcel/permission dependencies. No full world actor, caps HTTP endpoints or viewer-session proof.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} scene admission HTTP checks')
if __name__=='__main__':main()
