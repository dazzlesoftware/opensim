# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
import argparse,subprocess,tempfile,socket,http.client,xml.etree.ElementTree as ET,urllib.parse,queue,threading,time,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);executable=str(Path(a.native).resolve());checks=[]
def check(value,name):
 assert value,name
 checks.append(name)
with tempfile.TemporaryDirectory(dir=a.work) as temporary:
 root=Path(temporary)
 for profile in ["shared","separate","region"]:
  dedicated=profile=="separate";region=profile=="region"
  with socket.socket() as s:s.bind(('127.0.0.1',0));console_port=s.getsockname()[1] if dedicated else 0
  config=root/(profile+'.ini');config.write_text('[NativeServer]\nListenAddress=127.0.0.1\nUserAccountDatabase='+str(root/(profile+'.db'))+'\nAuthenticationDatabase='+str(root/(profile+'.db'))+'\n[Network]\nhttp_listener_port=0\nConsolePort='+str(console_port)+'\nConsoleUser=user\nConsolePass=pass\nConsoleAllowedOrigin=https://console.example\n[Startup]\nconsole=rest\n[LoginService]\nRequireInventory=false\n',encoding='utf-8')
  if region:
   settings=config.read_text(encoding='utf-8').replace('[NativeServer]','[NativeServer]\nEnableLocalRegion=true\nEnableRegionAssets=true\nInventoryDatabase='+str(root/'inventory.db')+'\nGridUserDatabase='+str(root/'gridusers.db'))
   settings+='\n[NativeRegion]\nUDPPort=0\nBindAddress=127.0.0.1\nExternalHost=127.0.0.1\nName=Remote log fixture\n[Modules]\nAssetServices=LocalAssetServicesConnector\nAssetCaching=FlotsamAssetCache\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\nDefaultAssetLoader=\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:'+str(root/'assets.db')+';version=3;Pooling=false\nDefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll\nAssetLoaderEnabled=false\n'
   config.write_text(settings,encoding='utf-8')
  initialized=subprocess.run([executable,'--config',str(config),'--init-db'],capture_output=True,text=True,timeout=20);check(initialized.returncode==0,'database initialization '+str(dedicated))
  proc=subprocess.Popen([executable,'--config',str(config)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8');lines=queue.Queue();output=[]
  def drain():
   for line in proc.stdout:output.append(line);lines.put(line)
  reader=threading.Thread(target=drain,daemon=True);reader.start()
  try:
   end=time.monotonic()+20;main_port=None
   while time.monotonic()<end:
    line=lines.get(timeout=20)
    if line.startswith('LISTENING '):main_port=urllib.parse.urlparse(line.split()[1]).port;break
   check(main_port is not None,'application starts '+str(dedicated));port=console_port or main_port
   def call(path,body='',selected_port=None):
    c=http.client.HTTPConnection('127.0.0.1',selected_port or port,timeout=30)
    try:c.request('POST',path,body,{'Content-Type':'application/x-www-form-urlencoded'});r=c.getresponse();return r.status,r.read()
    finally:c.close()
   check(call('/StartSession','USER=user&PASS=wrong')[0]==401,'reject invalid password '+str(dedicated))
   status,body=call('/sTaRtSeSsIoN?fixture=1','USER=user&PASS=pass');check(status==200,'authenticated session '+str(dedicated));doc=ET.fromstring(body);session=doc.findtext('SessionID');check(doc.find('HelpTree') is not None,'application command tree '+str(dedicated))
   if region:
    status,body=call('/ReadResponses/'+session);entries=ET.fromstring(body).findall('Line')
    check(status==200 and any(entry.get('Level')=='normal' and entry.text=='[ASSET SERVICE]: Local asset service enabled' for entry in entries),'region startup logs reach remote scrollback with normal level and original text')
   if dedicated:check(call('/StartSession','USER=user&PASS=pass',main_port)[0]!=200,'console restricted to selected port')
   def command(text,expected):
    status,body=call('/SessionCommand',urllib.parse.urlencode({'ID':session,'COMMAND':text}));check(status==200,'submit '+text)
    received=''
    for i in range(3):
     status,body=call('/ReadResponses/'+session);check(status==200,'poll '+text);received+=body.decode()
     if expected in received:return
    raise AssertionError((text,received))
   if region:command('fcache status','[FLOTSAM ASSET CACHE] Memory Cache:')
   command('login level 123','Set minimum login level to 123')
   command('login reset','Reset min login level to 0')
   command('login text "Welcome & hello"','Welcome &amp; hello')
   command('unknown native command','Invalid command')
   command('help login level','Set the minimum user level to log in')
   status,body=call('/',b'<?xml version="1.0"?><methodCall><methodName>unknown</methodName><params/></methodCall>',main_port);check(status==200 and b'methodResponse' in body,'main RPC remains available '+str(dedicated))
   check(call('/SessionCommand',urllib.parse.urlencode({'ID':session,'COMMAND':'quit'}))[0]==200,'remote quit accepted '+str(dedicated));proc.wait(timeout=15);reader.join(timeout=2);check(proc.returncode==0 and any('STOPPED' in x for x in output),'clean application shutdown '+str(dedicated))
  finally:
   if proc.poll() is None:proc.terminate()
   try:proc.wait(timeout=10)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
   if proc.returncode:print(''.join(output));print(proc.stderr.read())
report={'passed':True,'checks':len(checks),'observations':checks,'applicationParityVerified':False};(a.work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print('PASS: remote console application;',len(checks),'checks')
