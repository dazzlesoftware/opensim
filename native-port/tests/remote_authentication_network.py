"""Two actual native processes: remote login/auth tokens and local-storage isolation."""
import argparse,base64,contextlib,hashlib,http.client,json,queue,sqlite3,subprocess,tempfile,threading,urllib.parse,uuid,xmlrpc.client
from pathlib import Path
import xml.etree.ElementTree as ET
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);exe=str(Path(a.native).resolve());checks=[]
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 with tempfile.TemporaryDirectory(prefix='remote-auth-',dir=a.work) as folder:
  root=Path(folder);backend_config=root/'backend.ini';front_config=root/'frontend.ini';sentinel=root/'must-not-open.db';sentinel.write_bytes(b'not a SQLite database; leave untouched')
  backend_settings='''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=backend-accounts.db
AuthenticationDatabase=backend-auth.db
EnableAuthenticationEndpoint=true
[Network]
http_listener_port=0
[LoginService]
RequireInventory=false
[AuthenticationService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=remote-auth
HttpAuthPassword=remote-secret
AllowGetAuthInfo=true
AllowSetAuthInfo=true
AllowSetPassword=true
''';backend_config.write_text(backend_settings,encoding='utf-8')
  def cli(config,*options,password=None,expected=0):
   r=subprocess.run([exe,'--config',str(config),*options],input=password,capture_output=True,text=True,encoding='utf-8',timeout=30);check(r.returncode==expected,'CLI '+config.name+' '+str(options)+': '+(r.stderr if r.returncode!=expected else 'exit matches'))
   if password:check(password.strip() not in r.stdout+r.stderr,'password is not logged')
   return r
  class Running:
   def __init__(self,config):
    self.closed=False;self.process=subprocess.Popen([exe,'--config',str(config),'--console'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8');self.lines=queue.Queue()
    def read():
     for line in self.process.stdout:self.lines.put(line.strip())
    self.reader=threading.Thread(target=read,daemon=True);self.reader.start()
    try:
     while True:
      line=self.lines.get(timeout=20)
      if line.startswith('LISTENING '):self.port=urllib.parse.urlsplit(line.split(' ',1)[1]).port;break
    except BaseException:self.process.kill();self.process.wait();raise
   def request(self,path,body,content_type='application/x-www-form-urlencoded',authorized=True):
    headers={'Content-Type':content_type}
    if authorized:headers['Authorization']='Basic '+base64.b64encode(b'remote-auth:remote-secret').decode()
    c=http.client.HTTPConnection('127.0.0.1',self.port,timeout=40)
    try:c.request('POST',path,body,headers);r=c.getresponse();return r.status,r.read()
    finally:c.close()
   def auth(self,method,principal,**values):
    status,body=self.request('/auth/plain',urllib.parse.urlencode(dict(METHOD=method,PRINCIPAL=principal,**values)));check(status==200,'auth '+method+' HTTP200');return ET.fromstring(body)
   def login(self,password='backend-password',method='login_to_simulator',**extra):
    values=dict(first='Remote',last='User',passwd=password,**extra);status,body=self.request('/',xmlrpc.client.dumps((values,),methodname=method).encode(),'text/xml',False);check(status==200,'XML-RPC '+method+' HTTP200');return xmlrpc.client.loads(body)[0][0]
   def stop(self):
    if self.closed:return
    if self.process.poll() is None:self.process.stdin.write('quit\n');self.process.stdin.flush()
    try:self.process.wait(timeout=15)
    except subprocess.TimeoutExpired:self.process.kill();self.process.wait();raise
    self.reader.join(timeout=3);error=self.process.stderr.read();check(self.process.returncode==0,'native remote-auth process clean shutdown: '+error);self.process.stdin.close();self.process.stdout.close();self.process.stderr.close();self.closed=True
   def __enter__(self):return self
   def __exit__(self,*_):self.stop()
  cli(backend_config,'--init-db');cli(backend_config,'--create-user','Remote','User','--user-level','200',password='backend-password\n')
  with contextlib.closing(sqlite3.connect(root/'backend-accounts.db')) as db:principal=db.execute('SELECT PrincipalID FROM UserAccounts').fetchone()[0]
  backend=Running(backend_config)
  try:
   front_settings=f'''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=frontend-accounts.db
AuthenticationDatabase=must-not-open.db
AuthenticationProvider=Remote
EnableAuthenticationEndpoint=true
EnableAccountEndpoint=true
[Network]
http_listener_port=0
[LoginService]
RequireInventory=false
AllowRemoteSetLoginLevel=true
[UserAccountService]
AllowCreateUser=true
[AuthenticationService]
AuthenticationServerURI=http://127.0.0.1:{backend.port}
ConnectionString=Unsupported=must-not-parse
Realm=
AuthType=BasicHttpAuthentication
HttpAuthUsername=remote-auth
HttpAuthPassword=remote-secret
AllowGetAuthInfo=true
AllowSetAuthInfo=true
AllowSetPassword=true
''';front_config.write_text(front_settings,encoding='utf-8')
   cli(front_config,'--init-db');cli(front_config,'--create-user','Remote','User','--user-level','200',password='frontend-password-is-not-installed\n')
   with contextlib.closing(sqlite3.connect(root/'frontend-accounts.db')) as db,db:
    db.execute('UPDATE UserAccounts SET PrincipalID=?',(principal,));check(db.execute('SELECT name,version FROM migrations ORDER BY name').fetchall()==[('UserAccount',3),('migrations',1)],'remote initialization only migrates accounts')
    check(db.execute("SELECT name FROM sqlite_master WHERE name IN ('auth','tokens')").fetchall()==[],'remote initialization creates no local authentication tables')
   with Running(front_config) as front:
    check(front.login('wrong')['message'].startswith('Could not authenticate'),'remote backend rejects incorrect viewer credentials')
    good=front.login();check(good['message'].startswith('Error connecting'),'remote authenticated viewer reaches destination selection')
    check(front.login('$1$'+hashlib.md5(b'backend-password').hexdigest())==good,'viewer MD5 prefix reaches remote authenticator')
    check(front.login('frontend-password-is-not-installed')['message'].startswith('Could not authenticate'),'remote CLI account creation does not invent backend credentials')
    check(front.login(hashlib.md5(b'backend-password').hexdigest(),method='set_login_level',level=100)['success'] is True,'administrator gate uses remote authentication')
    check(front.login(hashlib.md5(b'backend-password').hexdigest(),method='set_login_level',level=0)['success'] is True,'remote-auth administrator restores login level')
    result=front.auth('authenticate',principal,PASSWORD=hashlib.md5(b'backend-password').hexdigest());token=result.findtext('Token');check(result.findtext('Result')=='Success' and str(uuid.UUID(token))==token,'frontend authentication endpoint relays remote UUID token')
    check(backend.auth('verify',principal,TOKEN=token).findtext('Result')=='Success','token issued through frontend exists on backend')
    check(front.auth('verify',principal,TOKEN=token,LIFETIME=30).findtext('Result')=='Success','frontend verify forwards to remote provider')
    check(front.auth('release',principal,TOKEN=token).findtext('Result')=='Success','frontend release forwards to remote provider')
    check(backend.auth('verify',principal,TOKEN=token).findtext('Result')=='Failure','remote release invalidates backend token')
    before=backend.auth('getauthinfo',principal).findtext('result/PasswordHash')
    for method in ['getauthinfo','setauthinfo','setpassword']:check(front.auth(method,principal,PASSWORD='blocked',PasswordHash='blocked').findtext('Result')=='Failure','remote connector retains inert administration '+method)
    check(backend.auth('getauthinfo',principal).findtext('result/PasswordHash')==before,'frontend cannot mutate backend authentication metadata')
    new_id=str(uuid.uuid4());status,body=front.request('/accounts',urllib.parse.urlencode(dict(METHOD='createuser',PrincipalID=new_id,FirstName='Remote',LastName='Provisioned',Password='ignored')));check(status==200 and ET.fromstring(body).findtext('result/PrincipalID')==new_id,'source provisioning retains account when remote SetPassword returns false')
    check(backend.auth('getauthinfo',new_id).findtext('Result')=='Failure','remote provisioning does not create backend auth rows')
    persistent=front.auth('authenticate',principal,PASSWORD=hashlib.md5(b'backend-password').hexdigest()).findtext('Token');old_port=backend.port;backend.stop()
    check(front.login()['message'].startswith('Error generating Login Response'),'remote transport failure follows login internal-error boundary')
    status,body=front.request('/auth/plain',urllib.parse.urlencode(dict(METHOD='verify',PRINCIPAL=principal,TOKEN=persistent)));check(status==500 and b'remote-secret' not in body,'remote endpoint transport exception follows HTTP500 without credentials')
    backend_config.write_text(backend_settings+f'\n[Network]\nhttp_listener_port={old_port}\n',encoding='utf-8');backend=Running(backend_config)
    check(front.auth('verify',principal,TOKEN=persistent).findtext('Result')=='Success','remote tokens survive backend restart')
    check(front.login()==good,'existing frontend resumes authentication after backend restart')
   with Running(front_config) as front:check(front.login()['message'].startswith('Error connecting'),'remote authentication survives frontend restart')
   check(sentinel.read_bytes()==b'not a SQLite database; leave untouched','remote mode leaves selected local auth file untouched')
   with contextlib.closing(sqlite3.connect(root/'frontend-accounts.db')) as db:
    check(db.execute("SELECT name FROM sqlite_master WHERE name IN ('auth','tokens')").fetchall()==[],'remote operations never create local authentication storage')
   bad=root/'invalid.ini';bad.write_text(front_settings.replace('AuthenticationProvider=Remote','AuthenticationProvider=Bad').replace('frontend-accounts.db','not-created.db'),encoding='utf-8');cli(bad,'--init-db',expected=1);check(not(root/'not-created.db').exists(),'invalid authentication provider rejected before account writes')
   bad.write_text(front_settings.replace(f'AuthenticationServerURI=http://127.0.0.1:{old_port}','AuthenticationServerURI=').replace('frontend-accounts.db','not-created.db'),encoding='utf-8');cli(bad,'--init-db',expected=1);check(not(root/'not-created.db').exists(),'missing remote URI rejected before account writes')
  finally:backend.stop()
 (a.work/'remote-authentication-network-results.json').write_text(json.dumps(dict(passed=True,checks=len(checks),observations=checks,scope='Actual native frontend/backend processes, viewer/admin login and authentication endpoint relay, Basic transport, remote inert administration, account provisioning, local auth-storage isolation, backend/front restart and failure recovery. HTTP-only; successful native world/viewer session and actual Unix execution remain pending.'),indent=2)+'\n',encoding='utf-8');print(f'PASS: {len(checks)} two-native-process remote authentication checks')
if __name__=='__main__':main()
