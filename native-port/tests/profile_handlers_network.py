"""Exercise registered profile handlers through the native HTTP listener."""
from pathlib import Path
import argparse, subprocess, json, http.client
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
names=['avatarclassifiedsrequest','classified_update','classifieds_info_query','classified_delete','avatarpicksrequest','pickinforequest','picks_update','picks_delete','avatarnotesrequest','avatar_notes_update','avatar_properties_request','avatar_properties_update','avatar_interests_update','user_preferences_update','user_preferences_request','image_assets_request','user_data_request','user_data_update']
commands=(a.reference/'commands.txt').read_text().splitlines();reference=(a.reference/'reference.txt').read_text().splitlines();assert len(commands)==len(reference)
cases=[]
for command,row in zip(commands,reference):
 method,mode,seed,raw=command.split();method=int(method)
 if method>=18 or seed!='0' or row.startswith('EX:'):continue
 request=json.loads(bytes.fromhex(raw));
 if '__null' in request:continue # This row injects a CLR null reference outside JSON parsing.
 request={'id':'wireid','jsonrpc':'2.0','method':names[method],**request}
 cases.append((mode,json.dumps(request,separators=(',',':')).encode(),bytes.fromhex(row.split('|')[-1])))
process=subprocess.Popen([a.native,'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
checks=0;connection=None
try:
 ready=process.stdout.readline().strip();assert ready.startswith('LISTENING '),ready
 connection=http.client.HTTPConnection('127.0.0.1',int(ready.split()[1]),timeout=15)
 for i,(mode,body,expected) in enumerate(cases):
  if i and i%50==0:
   connection.close();connection=http.client.HTTPConnection('127.0.0.1',int(ready.split()[1]),timeout=15)
  connection.request('POST' if i%2==0 else 'GET','/'+mode,body,{'Content-Type':'application/json-rpc'})
  try:response=connection.getresponse()
  except Exception as ex:raise AssertionError((i,mode,body,str(ex))) from ex
  actual=response.read();assert response.status==200,(i,response.status,actual)
  assert response.getheader('Content-Type')=='text/html;charset=UTF-8',(i,response.getheaders())
  assert actual==expected,(i,body,expected,actual);checks+=1
 connection.close();connection=http.client.HTTPConnection('127.0.0.1',int(ready.split()[1]),timeout=15)
 # The dispatcher accepts StreamReader BOM encodings with actual profile operations.
 for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\x00\x00'),('utf-32-be',b'\x00\x00\xfe\xff')]:
  for mode,body,expected in cases[::max(1,len(cases)//18)]:
   connection.request('POST','/'+mode,bom+body.decode().encode(encoding),{'Content-Type':'application/json-rpc'})
   response=connection.getresponse();actual=response.read();assert response.status==200 and actual==expected,(encoding,body,actual,expected);checks+=1
 connection.close();connection=None
 process.stdin.write('stop\n');process.stdin.flush();stdout,stderr=process.communicate(timeout=20);assert process.returncode==0,(stdout,stderr)
finally:
 if connection:connection.close()
 if process.poll() is None:process.kill();process.communicate()
report=dict(passed=True,requests=checks,applicationParityVerified=False,scope='Real native HTTP listener and dispatcher, all eighteen registered profile delegates, source-derived exact wire results and errors for provider success/failure/ref replacement/null outputs, malformed parameters and maturity flags; alternating POST/GET over persistent connection and five BOM encodings. Test provider only; production persistence/configuration/viewer binding and macOS incomplete.')
(a.work/'results.json').write_text(json.dumps(report,indent=2));print('PASS:',checks,'source-derived profile HTTP requests')
