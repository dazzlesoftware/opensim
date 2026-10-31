"""Native seed, capability, asset worker, polling response and TCP/TLS composition."""
import argparse,concurrent.futures,http.client,json,sqlite3,ssl,subprocess,urllib.parse
from pathlib import Path
from simulator_features_network import decode
def main():
 p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--tls',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);(w/'assets.ini').write_text('[Modules]\nAssetServices=LocalAssetServicesConnector\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:assets-fixture.db;version=3\nDefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll\nAssetLoaderArgs=default-assets.xml\n[ClientStack.LindenCaps]\nCap_GetTexture=localhost\nCap_GetMesh=localhost\nCap_GetMesh2=localhost\nCap_GetAsset=localhost\n',encoding='utf-8');command=[str(Path(a.server).resolve())]
 if a.tls:command += [str(Path(__file__).parent.resolve()/'fixtures/localhost-cert.pem'),str(Path(__file__).parent.resolve()/'fixtures/localhost-key.pem')]
 database=w/'assets-fixture.db'
 if database.exists():database.unlink()
 (w/'seed.bin').write_bytes(bytes(range(16)));(w/'empty.bin').write_bytes(b'')
 (w/'default-assets.xml').write_text('<Nini><Section Name="Default"><Key Name="file" Value="default-set.xml"/></Section></Nini>',encoding='utf-8')
 (w/'default-set.xml').write_text('<Nini><Section Name="Texture"><Key Name="assetID" Value="00000000-0000-0000-0000-000000000001"/><Key Name="fileName" Value="seed.bin"/></Section><Section Name="Empty"><Key Name="assetID" Value="00000000-0000-0000-0000-000000000002"/><Key Name="fileName" Value="empty.bin"/></Section></Nini>',encoding='utf-8')
 proc=subprocess.Popen(command,cwd=w,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);checks=[]
 def check(value,label):assert value,label;checks.append(label)
 try:
  line=proc.stdout.readline();assert line.startswith('LISTENING '),line+proc.stderr.read();port=int(line.split()[1])
  with sqlite3.connect(database) as db:check(db.execute('SELECT name,version FROM migrations ORDER BY name').fetchall()==[('AssetStore',6),('migrations',1)],'asset startup runs original migration and stamps versions')
  def request(path,method='GET',body=None,headers=None):
   c=http.client.HTTPSConnection('127.0.0.1',port,context=ssl._create_unverified_context(),timeout=20) if a.tls else http.client.HTTPConnection('127.0.0.1',port,timeout=20)
   try:c.request(method,path,body,headers or {});r=c.getresponse();return r.status,dict(r.getheaders()),r.read()
   finally:c.close()
  names=['GetTexture','GetMesh','GetMesh2','ViewerAsset'];body='<llsd><array>'+''.join('<string>'+n+'</string>' for n in names)+'</array></llsd>';status,headers,data=request('/CAPS/assets0000','POST',body,{'Content-Type':'application/llsd+xml'});caps=decode(data);check(status==200 and all(n in caps for n in names),'seed discovers four asset capabilities');paths={k:urllib.parse.urlsplit(caps[k]).path for k in names};check(len(set(paths.values()))==4,'independent local capability routes')
  uuid='00000000-0000-0000-0000-000000000001';expected=bytes(range(16))
  for name,key in zip(names,['texture_id','mesh_id','mesh_id','material_id']):
   path=paths[name]+'?'+key+'='+uuid
   for method in ['GET','POST']:
    status,headers,data=request(path,method);check(status==200 and data==expected,name+' '+method+' delivers source bytes')
   for value,status_want,selected in [('bytes=2-5',206,expected[2:6]),('bytes=-2',206,expected[:3]),('bytes=14-',206,expected[14:]),('bytes=99-',200,expected),('bytes=1-0',200,expected),('bytes=9-3',206,expected[3:4])]:
    status,headers,data=request(path,headers={'Range':value});check(status==status_want and data==selected,name+' '+value)
    if status==206:check(headers.get('Content-Range','').endswith('/16'),'partial response content range')
  for query,status_want in [('unknown=x',400),('Texture_id='+uuid,400),('texture_id=bad&mesh_id='+uuid,400),('texture_id=00000000-0000-0000-0000-000000000000',404),('texture_id=00000000-0000-0000-0000-000000000002',404)]:
   status,headers,data=request(paths['GetTexture']+'?'+query);check(status==status_want and data==b'','query or missing asset error '+query)
  before=int(request('/state')[2].decode().split('|')[0])
  for _ in range(3):check(request(paths['ViewerAsset']+'?texture_id='+uuid)[2]==expected,'memory cache hit delivers bytes')
  check(int(request('/state')[2].decode().split('|')[0])==before,'memory cache avoids provider calls')
  missing='00000000-0000-0000-0000-000000000003'
  check(request(paths['GetTexture']+'?texture_id='+missing)[0]==404,'provider missing asset')
  after=int(request('/state')[2].decode().split('|')[0])
  check(after==before+1,'missing asset fetches provider once')
  for _ in range(3):check(request(paths['GetTexture']+'?texture_id='+missing)[0]==404,'negative cache returns missing asset')
  check(int(request('/state')[2].decode().split('|')[0])==after,'negative cache avoids provider calls')
  with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
   replies=list(pool.map(lambda _:request(paths['GetTexture']+'?texture_id='+uuid),range(24)))
  check(all(s==200 and b==expected for s,h,b in replies),'24 concurrent asset requests complete through worker pool')
  stored='00000000-0000-0000-0000-000000000004';binary=bytes(range(256));stored_path=paths['ViewerAsset']+'?texture_id='+stored
  check(request('/store','POST',binary)[2].decode()==stored,'connector stores binary asset')
  check(request(stored_path)[2]==binary,'stored asset served from cache')
  request('/expire','POST',stored);check(request(stored_path)[2]==binary,'stored binary asset reloaded from SQLite')
  request('/store','POST',b'cache-only replacement');check(request(stored_path)[2]==b'cache-only replacement','existing asset store updates cache')
  request('/expire','POST',stored);check(request(stored_path)[2]==binary,'existing asset store preserves database data')
  status,headers,data=request('/state');check(status==200 and data.decode().split('|')[1:]==['1','0'],'local connector forwards empty sender without worker errors');check(request('/ready')[0]==503,'incomplete world remains unready');request('/dispose');check(request(paths['GetMesh']+'?mesh_id='+uuid)[0]==404,'capability disposal removes asset routes');proc.stdin.write('stop\n');proc.stdin.flush();proc.wait(timeout=15);assert proc.returncode==0,proc.stderr.read();check(True,'scene and asset worker teardown')
  (w/'seed.bin').write_bytes(b'updated-default')
  proc=subprocess.Popen(command,cwd=w,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  line=proc.stdout.readline();assert line.startswith('LISTENING '),line+proc.stderr.read();port=int(line.split()[1]);status,headers,data=request('/CAPS/assets0000','POST',body,{'Content-Type':'application/llsd+xml'});reopened=decode(data);check(status==200,'seed discovery after process restart');path=urllib.parse.urlsplit(reopened['ViewerAsset']).path
  with sqlite3.connect(database) as db:check(db.execute('SELECT name,version FROM migrations ORDER BY name').fetchall()==[('AssetStore',6),('migrations',1)],'restart preserves migration versions without duplicate stamps')
  check(request(path+'?texture_id='+uuid)[2]==b'updated-default','changed default asset overwrites existing database data at restart')
  check(request(path+'?texture_id='+stored)[2]==binary,'all 256 byte values persist across process restart')
  check(request(path+'?texture_id='+stored,headers={'Range':'bytes=127-255'})[2]==binary[127:],'persisted asset range after restart')
  proc.stdin.write('stop\n');proc.stdin.flush();proc.wait(timeout=15);assert proc.returncode==0,proc.stderr.read();check(True,'reopened storage and worker teardown')
  (w/'get-assets-network-results.json').write_text(json.dumps({'passed':True,'checks':checks,'tls':a.tls,'scope':'Real native seed/Caps/registry, asset module/worker, local connector, StoredAssetService and real SQLite persistence, queued callbacks, HTTP polling response and TCP/TLS, including complete process restart; controlled cache/user/world providers. Native configuration factory, builtin module registry, SQLite initialization and filesystem default assets are exercised, including changed-default overwrite at restart. The native migration engine and exact embedded v6 resource execute and version stamps survive restart. Additional provider/resource/connection options, legacy codecs, production world/cache and other-platform execution pending.'},indent=2),encoding='utf-8');print(f'PASS: {len(checks)} asset network observations')
 finally:
  if proc.poll() is None:proc.kill();proc.wait(timeout=5)
if __name__=='__main__':main()
