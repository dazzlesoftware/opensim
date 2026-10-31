"""Real HTTP form transport, auth, headers, reply BOMs and missing content length."""
import argparse,http.server,json,subprocess,tempfile,threading,urllib.parse
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 seven='00000000-0000-0000-0000-000000000007';friend='foreign friend & 試験';xml=f'<ServerResponse><friend0 type="List"><PrincipalID>{seven}</PrincipalID><Friend>foreign &amp; friend</Friend><MyFlags>7</MyFlags><TheirFlags>5</TheirFlags></friend0></ServerResponse>'
 class Handler(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1';profile='normal';requests=[]
  def log_message(self,*args):pass
  def handle(self):
   try:super().handle()
   except (ConnectionResetError,ConnectionAbortedError,BrokenPipeError):
    # Header-only clients intentionally close before rejected reply bodies or
    # the next keepalive request. Request and result assertions remain strict.
    if not getattr(self,'response_started',False):raise
  def do_POST(self):
   body=self.rfile.read(int(self.headers['Content-Length'])).decode();self.requests.append((self.path,dict(self.headers),body));method=urllib.parse.parse_qs(body)['METHOD'][0];data=xml if method.startswith('getfriends') else '<ServerResponse><Result>'+('Success' if self.profile=='success' else 'True')+'</Result></ServerResponse>';data=data.encode()
   if self.profile=='utf16':data=b'\xff\xfe'+data.decode().encode('utf-16-le')
   if self.profile=='empty':data=b''
   if self.profile=='malformed':data=b'bad xml'
   self.response_started=True;self.send_response(401 if self.profile=='unauthorized' else 200);self.send_header('Content-Type','application/xml')
   if self.profile=='chunked':self.send_header('Transfer-Encoding','chunked');self.end_headers();self.wfile.write(f'{len(data):x}\r\n'.encode()+data+b'\r\n0\r\n\r\n')
   else:self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
  worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
  try:
   with tempfile.TemporaryDirectory(prefix='friends-http-',dir=a.work) as temp:
    config=Path(temp)/'friends.ini';config.write_text(f'[FriendsService]\nFriendsServerURI=http://127.0.0.1:{server.server_port}/base/\nAuthType=BasicHttpAuthentication\nHttpAuthUsername=friend-user\nHttpAuthPassword=friend-password\n',encoding='utf-8')
    def token(s):return s.encode().hex() or '.'
    commands=[f'{mode} {token(seven)} {token(friend)} .' for mode in range(5)]
    for profile in ['normal','success','utf16','chunked','empty','malformed','unauthorized']:
     Handler.profile=profile;Handler.requests=[];r=subprocess.run([a.native,'--network',str(config)],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=30);check(r.returncode==0,profile+' connector process finishes');rows=r.stdout.splitlines();check(len(rows)==5 and len(Handler.requests)==5,profile+' each operation sends one HTTP request')
     expected=['getfriends','getfriends_string','storefriend','deletefriend','deletefriend_string']
     for mode,(path,headers,body) in enumerate(Handler.requests):
      fields=urllib.parse.parse_qs(body,keep_blank_values=True);h={k.lower():v for k,v in headers.items()};check(path=='/base//friends' and fields['METHOD']==[expected[mode]],profile+' source URI and operation '+str(mode));check(h.get('authorization')=='Basic ZnJpZW5kLXVzZXI6ZnJpZW5kLXBhc3N3b3Jk' and h.get('content-type')=='application/x-www-form-urlencoded',profile+' configured auth and form content type '+str(mode));check(h.get('connection','').lower()=='keep-alive' and h.get('keep-alive')=='timeout=30, max=10',profile+' source keepalive headers '+str(mode))
     normal=profile in ['normal','success','utf16'];check(all(row.startswith('|1|'+seven+':') for row in rows[:2]) if normal else rows[:2]==['|0','|0'],profile+' retrieval parses or falls back to an empty list');check(rows[2:]==(['|T']*3 if profile in ['normal','utf16'] else ['|F']*3),profile+' mutation Boolean and error handling')
     fields=urllib.parse.parse_qs(Handler.requests[2][2]);check(fields['PrincipalID']==[seven] and fields['Friend']==[friend] and fields['MyFlags']==['-2147483647'],profile+' source store field casing, encoding and signed flags')
  finally:server.shutdown();worker.join(timeout=5)
 (a.work/'friends-connector-network-results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} friends connector socket observations')
if __name__=='__main__':main()
