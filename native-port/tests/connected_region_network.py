"""Real native login, seed negotiation and UDP scene lifecycle; no .NET server."""
import argparse,json,queue,subprocess,threading,time,urllib.request,urllib.error,xmlrpc.client
from pathlib import Path
import tempfile
import sqlite3
import contextlib
import xml.etree.ElementTree as ET
import concurrent.futures
import re
import http.client
import urllib.parse

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--viewer',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--touch',action='store_true');p.add_argument('--assets',action='store_true');p.add_argument('--inventory',action='store_true');p.add_argument('--folders',action='store_true');p.add_argument('--library',action='store_true');p.add_argument('--legacy',action='store_true');p.add_argument('--bakes',action='store_true');p.add_argument('--physics-library',type=Path);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 if a.touch:a.assets=True # A configured script override must store the exact executed asset.
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 with tempfile.TemporaryDirectory(dir=a.work) as folder:
  root=Path(folder).resolve();config=root/'region.ini';config.write_text('''[NativeServer]
ListenAddress = 127.0.0.1
EnableLocalRegion = true
UserAccountDatabase = users.db
AuthenticationDatabase = users.db
InventoryDatabase = inventory.db
GridUserDatabase = gridusers.db
EnableAvatarService = true
AvatarDatabase = avatars.db
HttpTimeoutSeconds = 15
[Network]
http_listener_port = 0
[NativeRegion]
UDPPort = 0
BindAddress = 127.0.0.1
ExternalHost = 127.0.0.1
Name = Connected Native
[GridService]
DefaultRegionFlags = +DefaultRegion +FallbackRegion
''',encoding='utf-8')
  if a.assets:
   source=Path(__file__).resolve().parents[2]
   asset_config='\n[Modules]\nAssetServices=LocalAssetServicesConnector\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:'+str(root/'assets.db')+';version=3;Pooling=false\nDefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll\nAssetLoaderArgs='+str(source/'bin/assets/AssetSets.xml')+'\n[ClientStack.LindenCaps]\nCap_GetTexture=localhost\nCap_GetMesh=localhost\nCap_GetMesh2=localhost\nCap_GetAsset=localhost\n'
   config.write_text(config.read_text(encoding='utf-8').replace('EnableLocalRegion = true','EnableLocalRegion = true\nEnableRegionAssets = true')+asset_config,encoding='utf-8')
  if a.bakes:
   cache_default_id='cacacaca-caca-4aca-8aca-cacacacacaca';cache_default_bytes=b'Region-loaded cache default\x00\xff';(root/'cache-default.bin').write_bytes(cache_default_bytes)
   default_set=ET.Element('Nini');entry=ET.SubElement(default_set,'Section',Name='Cache lifecycle default')
   for name,value in [('assetID',cache_default_id),('name','Cache lifecycle default'),('assetType','0'),('fileName','cache-default.bin')]:ET.SubElement(entry,'Key',Name=name,Value=value)
   ET.ElementTree(default_set).write(root/'cache-default-set.xml',encoding='utf-8',xml_declaration=True)
   original_control=source/'bin/assets/AssetSets.xml';control=ET.parse(original_control)
   for entry in control.getroot().iter('Key'):
    if entry.get('Name')=='file':entry.set('Value',str((original_control.parent/entry.get('Value')).resolve()))
   section=ET.SubElement(control.getroot(),'Section',Name='Cache lifecycle fixture');ET.SubElement(section,'Key',Name='file',Value=str(root/'cache-default-set.xml'));control.write(root/'cache-default-control.xml',encoding='utf-8',xml_declaration=True)
   config.write_text(config.read_text(encoding='utf-8').replace('AssetLoaderArgs='+str(original_control),'AssetLoaderArgs='+str(root/'cache-default-control.xml')),encoding='utf-8')
   check(a.assets and a.legacy,'Bake integration requires actual asset and legacy viewer flows');text=config.read_text(encoding='utf-8').replace('[Modules]','[Modules]\nAssetCaching=FlotsamAssetCache').replace('[ClientStack.LindenCaps]','[ClientStack.LindenCaps]\nCap_UploadBakedTexture=localhost');config.write_text(text+'\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\nMemoryCacheTimeout=0.25\n',encoding='utf-8')
  if a.inventory:
   text=config.read_text(encoding='utf-8');text=text.replace('[ClientStack.LindenCaps]','[ClientStack.LindenCaps]\nCap_FetchInventory2=localhost') if '[ClientStack.LindenCaps]' in text else text+'\n[ClientStack.LindenCaps]\nCap_FetchInventory2=localhost\n';config.write_text(text,encoding='utf-8')
  if a.folders:
   check(a.inventory,'Folder fixture requires inventory fixture');config.write_text(config.read_text(encoding='utf-8').replace('[ClientStack.LindenCaps]','[ClientStack.LindenCaps]\nCap_FetchInventoryDescendents2=localhost'),encoding='utf-8')
  if a.library:
   check(a.folders and a.inventory and a.assets,'Library fixture requires inventory, folders and assets');source=Path(__file__).resolve().parents[2];text=config.read_text(encoding='utf-8').replace('[ClientStack.LindenCaps]','[ClientStack.LindenCaps]\nCap_FetchLibDescendents2=localhost');config.write_text(text+'\n[LibraryService]\nDefaultLibrary='+str(source/'bin/inventory/Libraries.xml')+'\n',encoding='utf-8')
  if a.touch:
   fixture=Path(__file__).resolve().parents[2]/'bin/assets/ScriptsAssetSet/DefaultSayAndTouch.lsl'
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native', 'Name = Connected Native\nDefaultScriptFile = '+str(fixture)),encoding='utf-8')
  if a.physics_library:
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native', 'Name = Connected Native\nPhysics = ubODE\nPhysicsLibrary = '+str(a.physics_library.resolve())),encoding='utf-8')
  cmd=[str(Path(a.native).resolve()),'--config',str(config)]
  for args,password in [(['--init-db'],None),(['--create-user','Native','Viewer'],'viewer-test-password\n')]:
   r=subprocess.run(cmd+args,input=password,text=True,capture_output=True,timeout=30);check(r.returncode==0,'CLI '+args[0]+': '+r.stderr)
  proc=subprocess.Popen(cmd+['--console'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8');lines=queue.Queue();errors=[]
  def read():
   for line in proc.stdout:lines.put(line)
   lines.put(None)
  def err():
   for line in proc.stderr:errors.append(line)
  threading.Thread(target=read,daemon=True).start();threading.Thread(target=err,daemon=True).start()
  try:
   url=None;deadline=time.monotonic()+20
   while time.monotonic()<deadline:
    line=lines.get(timeout=20)
    if line is None:raise RuntimeError('Server startup failed: '+''.join(errors))
    if line.startswith('LISTENING '):url=line.split(' ',1)[1].strip();break
   check(bool(url),'Native region starts HTTP and UDP')
   proxy=xmlrpc.client.ServerProxy(url)
   credentials=dict(first='Native',last='Viewer',passwd='viewer-test-password',start='home',channel='Native test',version='1.0')
   check(proxy.login_to_simulator({**credentials,'passwd':'wrong'})['login']=='false','Wrong password rejected')
   login=proxy.login_to_simulator(credentials);check(login.get('login')=='true','Actual local scene accepts login: '+str(login))
   seed=login['seed_capability'];check(seed.startswith(url),'Seed points to actual local HTTP listener')
   request=urllib.request.Request(seed,b'<llsd><array><string>EventQueueGet</string></array></llsd>',{'Content-Type':'application/llsd+xml'})
   with urllib.request.urlopen(request,timeout=10) as response:body=response.read();check(response.status==200 and b'EventQueueGet' in body,'Active source seed negotiates real EventQueueGet')
   asset_urls={}
   if a.assets:
    names=['GetTexture','GetMesh','GetMesh2','ViewerAsset']
    asset_seed=urllib.request.Request(seed,('<llsd><array>'+''.join('<string>'+n+'</string>' for n in names)+'</array></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
    with urllib.request.urlopen(asset_seed,timeout=10) as response:values=list(ET.fromstring(response.read()).find('map'))
    asset_urls={values[i].text:values[i+1].text for i in range(0,len(values),2)}
    check(all(n in asset_urls for n in names),'Actual login seed discovers all four asset capabilities')
    expected=(source/'bin/assets/TexturesAssetSet/plywood.jp2').read_bytes()
    def asset_fetch(name,query='texture_id=89556747-24cb-43ed-920b-47caed15465f',headers=None):
     try:
      with urllib.request.urlopen(urllib.request.Request(asset_urls[name]+'?'+query,headers=headers or {}),timeout=15) as r:
       data=bytearray()
       try:
        while block:=r.read1(65536):data.extend(block)
       except TimeoutError as e:raise AssertionError('Asset body stalled '+name+' after '+str(len(data))+' bytes; headers='+str(dict(r.headers))+'; server='+''.join(errors)) from e
       return r.status,bytes(data),r.headers
     except urllib.error.HTTPError as e:return e.code,e.read(),e.headers
    for n in names:
     status,data,headers=asset_fetch(n);check(status==200 and data==expected,n+' fetches exact original plywood bytes through local asset pipeline')
    if a.bakes:
     with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:
      check(db.execute('SELECT count(*) FROM assets WHERE UUID=?',(cache_default_id,)).fetchone()[0]==1,'Default asset exists in SQL before cache-only retrieval proof');db.execute('DELETE FROM assets WHERE UUID=?',(cache_default_id,));db.commit()
     status,data,_=asset_fetch('ViewerAsset','texture_id='+cache_default_id);check(status==200 and data==cache_default_bytes,'Actual region-loaded default survives removal of SQL copy before first HTTP retrieval')
     with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:check(db.execute('SELECT count(*) FROM assets WHERE UUID=?',(cache_default_id,)).fetchone()[0]==0,'Default cache hit does not resurrect deleted SQL row')
     late_id='fdfdfdfd-fdfd-4dfd-8dfd-fdfdfdfdfdfd';status,_,_=asset_fetch('ViewerAsset','texture_id='+late_id);check(status==404,'Actual missing asset creates configured negative cache entry');time.sleep(5)
     with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:
      db.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) VALUES(?,?,?,?,?,?,?,?,?)',(late_id,'Late out-of-band asset','Negative-cache timing evidence',0,0,0,0,login['agent_id'],b'Late SQL texture'));db.commit()
     status,_,_=asset_fetch('ViewerAsset','texture_id='+late_id);check(status==404,'Configured120-second negative cache survives past five seconds despite out-of-band SQL asset creation')
    status,data,headers=asset_fetch('GetTexture',headers={'Range':'bytes=127-255'});check(status==206 and data==expected[127:256] and headers['Content-Range']=='bytes 127-255/'+str(len(expected)),'Real asset range framing and bytes')
    status,data,_=asset_fetch('ViewerAsset','texture_id=c228d1cf-4b5d-4ba8-84f4-899a0796aa97');check(status==200 and data==(source/'bin/assets/TexturesAssetSet/default_avatar.jp2').read_bytes(),'Original default avatar texture delivered')
    status,data,_=asset_fetch('ViewerAsset','script_id=2074003b-8d5f-40e5-8b20-581c1c50aedb');check(status==200 and data==(source/'bin/assets/ScriptsAssetSet/DefaultScript.lsl').read_bytes(),'Original default script asset delivered')
    check(asset_fetch('GetTexture','texture_id=bad')[0]==400,'Invalid asset UUID rejected')
    check(asset_fetch('GetMesh','mesh_id=00000000-0000-0000-0000-000000000001')[0]==404,'Missing mesh returns404')
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:responses=list(pool.map(lambda _:asset_fetch('GetTexture'),range(16)))
    check(all(status==200 and data==expected for status,data,_ in responses),'Concurrent real scene asset requests complete')
   inventory_url=None
   if a.inventory:
    with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db, db:
     parent=db.execute('SELECT folderID FROM inventoryfolders WHERE agentID=? AND type=8',(login['agent_id'],)).fetchone()[0]
     item_id='dddddddd-dddd-4ddd-8ddd-dddddddddddd'
     db.execute('INSERT INTO inventoryitems(assetID,assetType,inventoryName,inventoryDescription,inventoryNextPermissions,inventoryCurrentPermissions,invType,creatorID,inventoryBasePermissions,inventoryEveryOnePermissions,salePrice,saleType,creationDate,groupID,groupOwned,flags,inventoryID,parentFolderID,avatarID,inventoryGroupPermissions) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',('2074003b-8d5f-40e5-8b20-581c1c50aedb',10,'Viewer <item> & text','Native inventory description',0x4321,0x4321,10,login['agent_id'],0x7fffffff,0,0,0,123456,'00000000-0000-0000-0000-000000000000',0,0x1234,item_id,parent,login['agent_id'],0))
     if a.folders:
      target_folder='eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee';target_id='bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb';link_id='cccccccc-cccc-4ccc-8ccc-cccccccccccc'
      db.execute('INSERT INTO inventoryfolders(folderName,type,version,folderID,agentID,parentFolderID) VALUES(?,?,?,?,?,?)',('Linked targets',-1,2,target_folder,login['agent_id'],parent))
      columns=[r[1] for r in db.execute('PRAGMA table_info(inventoryitems)')];original=dict(zip(columns,db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(item_id,)).fetchone()))
      for changes in [dict(inventoryID=target_id,parentFolderID=target_folder,inventoryName='Linked target',flags=0x100),dict(inventoryID=link_id,assetID=target_id,assetType=24,inventoryName='Viewer link')]:
       row={**original,**changes};db.execute('INSERT INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[row[c] for c in columns])
      category_ids={r[0] for r in db.execute('SELECT folderID FROM inventoryfolders WHERE parentFolderID=?',(parent,))}

    req=urllib.request.Request(seed,b'<llsd><array><string>FetchInventory2</string></array></llsd>',{'Content-Type':'application/llsd+xml'})
    with urllib.request.urlopen(req,timeout=5) as response:nodes=list(ET.fromstring(response.read()).find('map'))
    found={nodes[i].text:nodes[i+1].text for i in range(0,len(nodes),2)};inventory_url=found.get('FetchInventory2');check(bool(inventory_url),'Real seed discovers native FetchInventory2')
    def inventory_request(body,method='POST'):
     try:
      with urllib.request.urlopen(urllib.request.Request(inventory_url,body,{'Content-Type':'application/json'},method=method),timeout=5) as r:return r.status,r.read()
     except urllib.error.HTTPError as e:return e.code,e.read()
    status,data=inventory_request(json.dumps(dict(agent_id='00000000-0000-0000-0000-000000000001',items=[dict(item_id=item_id),dict(item_id='00000000-0000-0000-0000-000000000002')])).encode())
    check(status==200,'Native item capability reads actual SQLite inventory')
    def decode(node):
     if node.tag=='map':
      values=list(node);return {values[i].text:decode(values[i+1]) for i in range(0,len(values),2)}
     if node.tag=='array':return [decode(x) for x in node]
     if node.tag=='integer':return int(node.text or 0)
     return node.text or ''
    result=decode(ET.fromstring(data)[0]);check(result['agent_id']==login['agent_id'] and len(result['items'])==1,'Item route captures actual agent and omits absent items')
    item=result['items'][0];check(item['item_id']==item_id and item['name']=='Viewer <item> & text' and item['flags']==0x34,'Source inventory item fields, escaping and flags mask')
    check(item['permissions']['base_mask']==0x4321 and item['permissions']['owner_mask']==0x4321,'Source serialized base mask follows current permissions')
    check(inventory_request(b'{}','GET')[0]==404 and inventory_request(b'<llsd><array /></llsd>')[0]==400 and inventory_request(b'{}')[0]==500,'Source item HTTP method/parser/handler status branches')
   folder_url=None
   if a.folders:
    req=urllib.request.Request(seed,b'<llsd><array><string>FetchInventoryDescendents2</string></array></llsd>',{'Content-Type':'application/llsd+xml'})
    with urllib.request.urlopen(req,timeout=5) as response:nodes=list(ET.fromstring(response.read()).find('map'))
    found={nodes[i].text:nodes[i+1].text for i in range(0,len(nodes),2)};folder_url=found.get('FetchInventoryDescendents2');check(bool(folder_url),'Real seed discovers queued folder capability')
    def folder_request(ids,fetch_folders=True,fetch_items=True):
     parts=['<map><key>folder_id</key><uuid>'+id+'</uuid><key>owner_id</key><uuid>'+login['agent_id']+'</uuid><key>sort_order</key><integer>99</integer><key>fetch_folders</key><boolean>'+str(int(fetch_folders))+'</boolean><key>fetch_items</key><boolean>'+str(int(fetch_items))+'</boolean></map>' for id in ids]
     request=urllib.request.Request(folder_url,('<llsd><map><key>folders</key><array>'+''.join(parts)+'</array></map></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
     with urllib.request.urlopen(request,timeout=10) as response:check(response.status==200,'Queued folder response succeeds');return decode(ET.fromstring(response.read())[0])['folders']
    folders=folder_request([parent,parent]);check(len(folders)==1 and folders[0]['folder_id']==parent,'Folder batching deduplicates real root folder')
    folder=folders[0];check({c['category_id'] for c in folder['categories']}==category_ids,'Actual SQLite child categories and source folder serialization')
    check({i['item_id'] for i in folder['items']}=={item_id,link_id,target_id} and folder['items'][0]['item_id']==target_id,'Folder worker resolves and prepends actual linked inventory target')
    check(folder['descendents']==len(category_ids)+2 and next(i for i in folder['items'] if i['item_id']==item_id)['flags']==0x1234,'Source descendant count excludes prepended target and retains full item flags')
    folder=folder_request([parent],False,True)[0];check(folder['categories']==[] and len(folder['items'])==3 and folder['descendents']==2,'Source item-only folder fetch retains links')
    folder=folder_request([parent],True,False)[0];check(len(folder['categories'])==len(category_ids) and folder['items']==[] and folder['descendents']==len(category_ids),'Source category-only folder fetch')
    folder=folder_request(['00000000-0000-0000-0000-000000000000'])[0];check(folder['categories']==[] and folder['items']==[] and folder['descendents']==0,'Zero folder request does not expose every root folder')
    folder=folder_request(['aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'])[0];check(folder['categories']==[] and folder['items']==[] and folder['version']==0,'Source empty collection behavior for absent SQLite folder')
   library_urls={}
   if a.library:
    names=['FetchLib2','FetchLibDescendents2']
    req=urllib.request.Request(seed,('<llsd><array>'+''.join('<string>'+n+'</string>' for n in names)+'</array></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
    with urllib.request.urlopen(req,timeout=5) as response:nodes=list(ET.fromstring(response.read()).find('map'))
    library_urls={nodes[i].text:nodes[i+1].text for i in range(0,len(nodes),2)};check(all(n in library_urls for n in names),'Actual seed discovers library item and separate library folder capabilities')
    lib_owner=login['inventory-lib-owner'][0]['agent_id'];lib_root=login['inventory-lib-root'][0]['folder_id'];skeleton=login['inventory-skel-lib'];check(len(skeleton)>=10,'Login loads original shipped library skeleton')
    def lib_folder_request(endpoint,ids,owner=lib_owner):
     parts=['<map><key>folder_id</key><uuid>'+id+'</uuid><key>owner_id</key><uuid>'+owner+'</uuid><key>sort_order</key><integer>0</integer><key>fetch_folders</key><boolean>0</boolean><key>fetch_items</key><boolean>0</boolean></map>' for id in ids]
     req=urllib.request.Request(endpoint,('<llsd><map><key>folders</key><array>'+''.join(parts)+'</array></map></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
     with urllib.request.urlopen(req,timeout=10) as response:check(response.status==200,'Actual library folder response succeeds');return ET.fromstring(response.read())
    ids=list(dict.fromkeys(f['folder_id'] for f in skeleton));tree=lib_folder_request(library_urls['FetchLibDescendents2'],ids);folders=decode(tree[0])['folders'];all_items=[item for f in folders for item in f['items']]
    check(len(folders)==len(ids) and len(all_items)==123,'All original library folders and123 shipped items delivered through worker')
    check(all(f['agent_id']==login['agent_id'] and f['owner_id']==lib_owner and f['descendents']==len(f['categories'])+len(f['items']) for f in folders),'Separate library capability preserves requesting agent, owner and counts while ignoring fetch flags')
    lib_root_data=next(f for f in folders if f['folder_id']==lib_root);check(lib_root_data['version']==next(f['version'] for f in skeleton if f['folder_id']==lib_root) and bool(lib_root_data['categories']),'Library root version and category tree match login skeleton')
    generic=decode(lib_folder_request(folder_url,[lib_root])[0])['folders'][0];check(generic['agent_id']==lib_owner and generic['categories']==lib_root_data['categories'],'Ordinary folder capability routes library owner through its distinct source branch')
    sample=all_items[0];req=urllib.request.Request(library_urls['FetchLib2'],json.dumps(dict(items=[dict(item_id=sample['item_id']),dict(item_id='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa')])).encode(),{'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=5) as response:result=decode(ET.fromstring(response.read())[0])
    check(result['agent_id']==login['agent_id'] and result['items']==[sample],'Actual library item provider returns source metadata and omits absent item')
    missing=lib_folder_request(library_urls['FetchLibDescendents2'],['aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa']);check(decode(missing[0])['folders']==[] and decode(missing[1])['bad_folders'][0]['folder_id']=='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa','Library missing folder source bad-folder response')
    again=lib_folder_request(library_urls['FetchLibDescendents2'],['aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa']);check(decode(again[0])['folders']==[] and len(again)==2,'Library negative-cache branch retains source response shape')
    zero=decode(lib_folder_request(library_urls['FetchLibDescendents2'],['00000000-0000-0000-0000-000000000000'])[0])['folders'][0];check(zero['version']==-1 and zero['descendents']==0,'Library zero folder uses source minus-one version')
    ignored=decode(lib_folder_request(library_urls['FetchLibDescendents2'],[lib_root],login['agent_id'])[0])['folders'];check(ignored==[],'Library folder handler filters ordinary owner requests')
    assets={}
    control=source/'bin/assets/AssetSets.xml'
    for section in ET.parse(control).getroot().findall('Section'):
     asset_set={x.attrib['Name']:x.attrib['Value'] for x in section.findall('Key')};index=control.parent/asset_set['file']
     if not index.is_file():continue
     for entry in ET.parse(index).getroot().findall('Section'):
      row={x.attrib['Name']:x.attrib['Value'] for x in entry.findall('Key')}
      if 'assetID' in row and row.get('fileName') and (index.parent/row['fileName']).is_file():assets[row['assetID'].lower()]=index.parent/row['fileName']
    keys={0:'texture_id',1:'sound_id',5:'clothing_id',6:'object_id',7:'notecard_id',10:'script_id',13:'bodypart_id',20:'animatn_id',21:'gesture_id'}
    item=next(i for i in all_items if i['asset_id'] in assets and i['type'] in keys);status,data,_=asset_fetch('ViewerAsset',keys[item['type']]+'='+item['asset_id']);check(status==200 and data==assets[item['asset_id']].read_bytes(),'Library inventory asset reference resolves to exact shipped data')
   try:urllib.request.urlopen(url+'ready',timeout=5);raise AssertionError('Readiness incorrectly claims full simulation')
   except urllib.error.HTTPError as e:check(e.code==503,'Incomplete simulation retains honest readiness status')
   legacy_metadata=None
   if a.legacy:
    check(a.library and bool(a.physics_library),'Legacy fixture requires library and physical profile')
    with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:version=db.execute('SELECT version FROM inventoryfolders WHERE folderID=?',(parent,)).fetchone()[0]
    script_source_id='abababab-abab-4aba-8aba-abababababab'
    with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db,db:
     columns=[r[1] for r in db.execute('PRAGMA table_info(inventoryitems)')];row=dict(zip(columns,db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(item_id,)).fetchone()));row.update(inventoryID=script_source_id,parentFolderID=target_folder,inventoryName='Existing no-copy script',inventoryDescription='Actual stored script',inventoryCurrentPermissions=0x7fff7fff,inventoryBasePermissions=0x7fffffff,inventoryNextPermissions=0x82000,inventoryEveryOnePermissions=0x2000,inventoryGroupPermissions=0x4000,flags=0x12345678,creationDate=111,creatorID=login['agent_id']+';https://creator.example;Native Creator')
     db.execute('INSERT INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[row[c] for c in columns])
    library_script=next(i for i in all_items if i['type']==10 and i['inv_type']==10 and i['asset_id'] in assets)
    inventory_rez=dict(destination=target_folder,user=dict(item_id=script_source_id,asset_id='2074003b-8d5f-40e5-8b20-581c1c50aedb',name='Existing no-copy script',creator_id=login['agent_id'],owner_mask=0x7fff7fff,group_mask=0x4000,flags=0x12345678),library=dict(item_id=library_script['item_id'],asset_id=library_script['asset_id'],name=library_script['name'],creator_id=library_script['permissions']['creator_id'],owner_mask=library_script['permissions']['owner_mask'],group_mask=library_script['permissions']['group_mask'],flags=library_script['flags']))
    drop_row={**row,'inventoryID':'acacacac-acac-4aca-8aca-acacacacacac','inventoryName':'Dropped no-copy script'};inventory_rez['drop_item']=drop_row['inventoryID']
    user_operations=dict(item='adadadad-adad-4ada-8ada-adadadadadad',landmark='aeaeaeae-aeae-4aea-8aea-aeaeaeaeaeae',foreign='afafafaf-afaf-4afa-8afa-afafafafafaf',trash_folder='b0b0b0b0-b0b0-40b0-80b0-b0b0b0b0b0b0',trash_item='b1b1b1b1-b1b1-41b1-81b1-b1b1b1b1b1b1',group='b2b2b2b2-b2b2-42b2-82b2-b2b2b2b2b2b2')
    with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
     user_operations['destination']=db.execute('SELECT folderID FROM inventoryfolders WHERE agentID=? AND type=10',(login['agent_id'],)).fetchone()[0]
     user_operations['trash_parent']=db.execute('SELECT folderID FROM inventoryfolders WHERE agentID=? AND type=14',(login['agent_id'],)).fetchone()[0]
    inventory_rez['user_operations']=user_operations
    operations_rows=[]
    for key,name in [('item','User source'),('landmark','HG Old'),('foreign','Foreign source'),('trash_item','Trash child item')]:
     operations_rows.append({**row,'inventoryID':user_operations[key],'inventoryName':name,'inventoryDescription':'old @ http://grid' if key=='landmark' else 'Actual stored script','inventoryBasePermissions':0x9e000,'inventoryCurrentPermissions':0x9e000,'inventoryNextPermissions':0x82000,'inventoryEveryOnePermissions':0x2000,'inventoryGroupPermissions':0x4000,'flags':0x20000070,'creationDate':222,'salePrice':10,'saleType':1,'assetType':3 if key=='landmark' else 10,'invType':3 if key=='landmark' else 10,'avatarID':'99999999-9999-4999-8999-999999999999' if key=='foreign' else login['agent_id'],'parentFolderID':user_operations['trash_folder'] if key=='trash_item' else target_folder})
    copy_rows=[]
    for key,name,permission,inv_type,next_mask in [('cdcdcdcd-cdcd-4cdc-8cdc-cdcdcdcdcdcd','Copy source',0x9e000,10,0x82000),('cececece-cece-4ece-8ece-cececececece','No-copy source',0x7fff7fff,10,0x82000),('cfcfcfcf-cfcf-4fcf-8fcf-cfcfcfcfcfcf','No-transfer source',0x9c000,10,0x82000),('cacacaca-caca-4aca-8aca-cacacacacaca','Animation permission source',0x9e000,96,0xc000)]:
     copy_rows.append({**row,'inventoryID':key,'inventoryName':name,'inventoryBasePermissions':0x9e000,'inventoryCurrentPermissions':permission,'invType':inv_type,'inventoryNextPermissions':next_mask,'inventoryEveryOnePermissions':0x2000,'inventoryGroupPermissions':0x4000,'creationDate':222,'groupID':user_operations['group'],'groupOwned':1,'salePrice':99,'saleType':2})
    def copy_expected(source_row,name,folder,cross=False,animation=False):
     expected={key:source_row[key] for key in ['assetID','assetType','invType','inventoryDescription','inventoryBasePermissions','inventoryCurrentPermissions','inventoryEveryOnePermissions','inventoryNextPermissions','inventoryGroupPermissions','flags','creatorID']};expected.update(inventoryName=name,parentFolderID=folder,avatarID=login['agent_id'],groupID='00000000-0000-0000-0000-000000000000',groupOwned=0,salePrice=0,saleType=0)
     if cross:expected.update(inventoryBasePermissions=source_row['inventoryNextPermissions'],inventoryCurrentPermissions=source_row['inventoryNextPermissions'],inventoryEveryOnePermissions=source_row['inventoryEveryOnePermissions']&source_row['inventoryNextPermissions'])
     if animation:expected.update(inventoryBasePermissions=expected['inventoryBasePermissions']&0x8e000,inventoryCurrentPermissions=expected['inventoryCurrentPermissions']&0x8e000,inventoryNextPermissions=expected['inventoryNextPermissions']&0xc000,inventoryEveryOnePermissions=0,inventoryGroupPermissions=0)
     return expected
    copy_profiles=[]
    for phase,source_row,old_agent,new_name,new_folder,denied in [(0,copy_rows[0],login['agent_id'],'Copied user',target_folder,False),(1,copy_rows[0],'99999999-9999-4999-8999-999999999999','',target_folder,False),(2,copy_rows[1],login['agent_id'],'Denied copy',target_folder,True),(3,copy_rows[2],'99999999-9999-4999-8999-999999999999','Denied transfer',target_folder,True),(4,copy_rows[0],login['agent_id'],'System folder copy','00000000-0000-0000-0000-000000000000',False)]:
     copy_profiles.append(dict(old_item=source_row['inventoryID'],old_agent=old_agent,new_name=new_name,new_folder=new_folder,denied=denied,expected=copy_expected(source_row,new_name or source_row['inventoryName'],user_operations['destination'] if phase==4 else new_folder,phase==1)))
    library_config=None
    for path in (source/'bin/inventory').rglob('*.xml'):
     for section in ET.parse(path).getroot().findall('Section'):
      values={key.attrib['Name']:key.attrib['Value'] for key in section.findall('Key')}
      if values.get('inventoryID','').lower()==library_script['item_id']:library_config=values;break
     if library_config is not None:break
    check(library_config is not None,'Copy fixture resolves original library XML configuration')
    library_copy=dict(assetID=library_script['asset_id'],assetType=10,invType=10,inventoryDescription=library_script['desc'],inventoryBasePermissions=int(library_config.get('basePermissions',0x9e000)),inventoryCurrentPermissions=int(library_config.get('currentPermissions',0x9e000)),inventoryNextPermissions=int(library_config.get('nextPermissions',0x9e000)),inventoryEveryOnePermissions=int(library_config.get('everyonePermissions',0x9a000)),inventoryGroupPermissions=int(library_config.get('basePermissions',0)),flags=int(library_config.get('flags',0)),creatorID=lib_owner)
    copy_profiles.append(dict(old_item=library_script['item_id'],old_agent=lib_owner,new_name='Copied library',new_folder=target_folder,denied=False,expected=copy_expected(library_copy,'Copied library',target_folder)))
    copy_profiles.append(dict(old_item=copy_rows[3]['inventoryID'],old_agent='99999999-9999-4999-8999-999999999999',new_name='Animation permission copy',new_folder=target_folder,denied=False,expected=copy_expected(copy_rows[3],'Animation permission copy',target_folder,True,True)))
    inventory_rez['copy_operations']=copy_profiles
    upload_rows=[]
    for index,(name,it,at,permission) in enumerate([('Upload note',7,7,0x9e000),('Upload script',10,10,0x9e000),('Upload gesture',20,21,0x9e000),('Upload settings',25,56,0x9e000),('Upload material',26,57,0x9e000),('Upload animation',96,10,0x9e000),('Denied note',7,7,0x4000),('Foreign upload',7,7,0x9e000),('Denied gesture',20,21,0x8000)]):
     upload_rows.append({**row,'inventoryID':'d0d0d0d0-d0d0-40d0-80d0-'+str(index).zfill(12),'inventoryName':name,'inventoryDescription':'Upload description','assetType':at,'invType':it,'inventoryCurrentPermissions':permission,'inventoryBasePermissions':0x9e000,'avatarID':'99999999-9999-4999-8999-999999999999' if index==7 else login['agent_id'],'parentFolderID':user_operations['destination']})
    inventory_rez['asset_update_items']=[r['inventoryID'] for r in upload_rows]

    legacy_metadata=root/'legacy-inventory.json';legacy_metadata.write_text(json.dumps(dict(item_id=item_id,asset_id='2074003b-8d5f-40e5-8b20-581c1c50aedb',parent=parent,owner=login['agent_id'],version=version,categories=sorted(category_ids),linked_item=target_id,link_id=link_id,library_folder=lib_root,library_owner=lib_owner,library_version=lib_root_data['version'],library_categories=[c['category_id'] for c in lib_root_data['categories']],verify_partial_batch=True,inventory_rez=inventory_rez)),encoding='utf-8')
   legacy_root_ids={item_id,link_id} if a.legacy else set()
   root_version_expected=version if a.legacy else None
   baked_ids=[]
   def probe(session):
    nonlocal root_version_expected
    if a.legacy:
     with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db,db:
      db.execute('INSERT OR REPLACE INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[drop_row[c] for c in columns])
      db.execute('INSERT OR REPLACE INTO inventoryfolders(folderName,type,version,folderID,agentID,parentFolderID) VALUES(?,?,?,?,?,?)',('User operations trash',-1,1,user_operations['trash_folder'],session['agent_id'],user_operations['trash_parent']))
      for row in operations_rows:db.execute('INSERT OR REPLACE INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[row[c] for c in columns])
      for row in copy_rows:db.execute('INSERT OR REPLACE INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[row[c] for c in columns])
      for row in upload_rows:db.execute('INSERT OR REPLACE INTO inventoryitems('+','.join(columns)+') VALUES('+','.join('?' for _ in columns)+')',[row[c] for c in columns])
      check(db.execute('SELECT version FROM inventoryfolders WHERE folderID=?',(parent,)).fetchone()[0]==root_version_expected,'Root folder version reflects exact source add/move increments across sessions')
      check({r[0] for r in db.execute('SELECT inventoryID FROM inventoryitems WHERE parentFolderID=?',(parent,))}==legacy_root_ids,'Root fixture retains exact original two-item set after real link move')
     legacy_state=json.loads(legacy_metadata.read_text(encoding='utf-8'));legacy_state['version']=root_version_expected;legacy_metadata.write_text(json.dumps(legacy_state),encoding='utf-8')
    names=['EventQueueGet']+(['GetTexture','GetMesh','GetMesh2','ViewerAsset','UpdateScriptTask','UpdateScriptTaskInventory','UpdateNotecardAgentInventory','UpdateNotecardTaskInventory','UpdateAnimSetAgentInventory','UpdateScriptAgent','UpdateScriptAgentInventory','UpdateSettingsAgentInventory','UpdateSettingsTaskInventory','UpdateMaterialAgentInventory','UpdateMaterialTaskInventory','UpdateGestureAgentInventory','UpdateGestureTaskInventory'] if a.assets else [])
    if a.bakes:names.append('UploadBakedTexture')
    discovery=urllib.request.Request(session['seed_capability'],('<llsd><array>'+''.join('<string>'+n+'</string>' for n in names)+'</array></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
    with urllib.request.urlopen(discovery,timeout=10) as response:values=list(ET.fromstring(response.read()).find('map'))
    session_caps={values[i].text:values[i+1].text for i in range(0,len(values),2)}
    if a.assets:
     asset_urls.clear();asset_urls.update({name:session_caps[name] for name in ['GetTexture','GetMesh','GetMesh2','ViewerAsset']})
     check(session_caps['UpdateScriptTask']==session_caps['UpdateScriptTaskInventory'],'Both real script editor aliases share one source path')
     for first,second in [('UpdateNotecardAgentInventory','UpdateNotecardTaskInventory'),('UpdateScriptAgent','UpdateScriptAgentInventory'),('UpdateSettingsAgentInventory','UpdateSettingsTaskInventory'),('UpdateMaterialAgentInventory','UpdateMaterialTaskInventory'),('UpdateGestureAgentInventory','UpdateGestureTaskInventory')]:check(session_caps[first]==session_caps[second],'Generic source alias pair shares real URL: '+first)
    extra=['I am Alive!','Touched.'] if a.touch else ['Script running','']
    extra += ['--walk' if a.physics_library else '',str(legacy_metadata) if legacy_metadata else '']
    extra+=['--verify-script-assets' if a.assets else '--verify-script-events']
    command=[str(Path(a.viewer).resolve()),session['agent_id'],session['session_id'],str(session['circuit_code']),session['sim_ip'],str(session['sim_port']),*extra]
    lines=[];inbox=queue.Queue();process=subprocess.Popen(command,text=True,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    def read_probe():
     for line in process.stdout:inbox.put(line)
     inbox.put(None)
    reader=threading.Thread(target=read_probe,daemon=True);reader.start();deadline=time.monotonic()+90;script_ids=[];edit_phase=0;phase_payload_seen=0;failed_item=None;copy_ids_snapshot=None
    default_script=(source/'bin/assets/ScriptsAssetSet'/('DefaultSayAndTouch.lsl' if a.touch else 'DefaultScript.lsl')).read_bytes() if a.assets else b''
    expected_script=default_script
    def decode(node):
     if node.tag=='map':
      children=list(node);return {children[i].text:decode(children[i+1]) for i in range(0,len(children),2)}
     if node.tag=='array':return [decode(child) for child in node]
     if node.tag=='boolean':return node.text in ['1','true']
     if node.tag=='integer':return int(node.text or 0)
     return node.text or ''
    def post(url,data,content_type='application/llsd+xml'):
     try:
      with urllib.request.urlopen(urllib.request.Request(url,data,{'Content-Type':content_type}),timeout=15) as response:return response.status,response.read()
     except urllib.error.HTTPError as error:return error.code,error.read()

    try:
     while True:
      if time.monotonic()>=deadline:raise AssertionError('Connected viewer probe exceeded timeout: '+''.join(lines))
      try:line=inbox.get(timeout=.2)
      except queue.Empty:continue
      if line is None:break
      lines.append(line)
      if a.assets and (match:=re.fullmatch(r'Task script asset ([0-9a-f-]{36})\s*',line)):
       script_id=match[1]
       status,data,_=asset_fetch('ViewerAsset','script_id='+script_id)
       check(status==200 and data in [default_script,expected_script],'Contents script asset matches actual executed text while session is active: '+script_id+' status='+str(status)+' bytes='+str(len(data)))
       if data==expected_script:phase_payload_seen+=1
       script_ids.append(script_id);process.stdin.write('OK\n');process.stdin.flush()
      if a.bakes and line.strip()=='Generic agent uploads':
       endpoint=session_caps['UploadBakedTexture']
       for previous in baked_ids:
        status,data,_=asset_fetch('ViewerAsset','texture_id='+previous);check(status==200 and data==b'Actual native bake\x00\xff','Baked cache survives logout and fresh seed/session')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:inventory_count=db.execute('SELECT count(*) FROM inventoryitems').fetchone()[0]
       with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:asset_count=db.execute('SELECT count(*) FROM assets').fetchone()[0]
       def bake_metadata():
        status,payload=post(endpoint,b'not LLSD');value=decode(ET.fromstring(payload)[0]);check(status==200 and value['state']=='upload' and value['upload_price']==0 and value['uploader'].endswith('-BK'),'Baked metadata accepts arbitrary raw body and source path/price');return value['uploader']
       uploader=bake_metadata();target=urllib.parse.urlsplit(uploader);connection=http.client.HTTPConnection(target.hostname,target.port,timeout=5,source_address=('127.0.0.2',0))
       try:
        connection.request('POST',target.path,b'wrong IP');response=connection.getresponse();response.read();check(response.status==401,'Real baked uploader enforces source IP')
       finally:connection.close()
       status,_=post(uploader,b'correct IP');check(status==404,'Baked IP denial consumes globally registered child')
       uploader=bake_metadata();status,_=post(uploader,b'');check(status==400,'Empty baked body retains uploader');status,_=post(uploader,b'x'*6000001);check(status==413,'Real baked6MB limit retains uploader');body=b'Actual native bake\x00\xff';status,payload=post(uploader,body,'application/octet-stream');reply=decode(ET.fromstring(payload)[0]);check(status==200 and reply['state']=='complete' and reply['new_inventory_item']=='00000000-0000-0000-0000-000000000000' and all(reply[k]==0 for k in ['new_next_owner_mask','new_group_mask','new_everyone_mask','inventory_item_flags']),'Baked completion has source zero inventory/masks/flags');baked_ids.append(reply['new_asset']);status,data,_=asset_fetch('ViewerAsset','texture_id='+reply['new_asset']);check(status==200 and data==body,'Actual cache-only baked raw bytes resolve through production asset connector');status,_=post(uploader,body);check(status==404,'Actual baked upload URL consumed once')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:check(db.execute('SELECT count(*) FROM inventoryitems').fetchone()[0]==inventory_count,'Baked upload creates no inventory row')
       with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:check(db.execute('SELECT count(*) FROM assets').fetchone()[0]==asset_count and db.execute('SELECT count(*) FROM assets WHERE UUID=?',(reply['new_asset'],)).fetchone()[0]==0,'Temporary local bake is not persisted as an ordinary asset')
      if line.strip()=='Generic agent uploads':
       def upload_metadata(name,metadata):
        status,payload=post(session_caps[name],json.dumps(metadata).encode(),'application/json');check(status==200,'Generic upload metadata status');data=decode(ET.fromstring(payload)[0]);check(data.get('state')=='upload' and data.get('upload_price')==0 and bool(data.get('uploader')),'Generic upload response exact source fields');return data['uploader']
       status,payload=post(session_caps['UpdateAnimSetAgentInventory'],b'<llsd><map /></llsd>');check(status==200 and payload==b'','Original animation-set wrapper remains an actual no-op')
       status,payload=post(session_caps['UpdateNotecardAgentInventory'],b'<llsd><map /></llsd>');check(status==200 and b'failed to recode request' in payload,'Generic zero item-ID source error')
       status,payload=post(session_caps['UpdateNotecardTaskInventory'],json.dumps({'item_id':upload_rows[0]['inventoryID'],'task_id':'12345678-1234-4234-8234-123456789abc'}).encode(),'application/json');check(status==200 and decode(ET.fromstring(payload)[0])['message']=='object not found','Real task upload metadata rejects missing object')
       endpoint=upload_metadata('UpdateNotecardAgentInventory',{'item_id':upload_rows[0]['inventoryID']});target=urllib.parse.urlsplit(endpoint)
       connection=http.client.HTTPConnection(target.hostname,target.port,timeout=5,source_address=('127.0.0.2',0))
       try:
        connection.request('POST',target.path,b'wrong origin',{'Content-Type':'application/octet-stream'});response=connection.getresponse();response.read();check(response.status==401,'Real generic uploader enforces original client IP')
       finally:connection.close()
       status,_=post(endpoint,b'correct origin');check(status==404,'IP rejection consumes actual generic uploader before validation')
       payloads=[]
       for index,item in enumerate(upload_rows):
        body=b'Raw generic '+str(index).encode()+b'\x00\xff\x00';payloads.append(body);name=['UpdateNotecardAgentInventory','UpdateScriptAgentInventory','UpdateGestureTaskInventory','UpdateSettingsAgentInventory','UpdateMaterialTaskInventory','UpdateNotecardAgentInventory','UpdateNotecardAgentInventory','UpdateNotecardAgentInventory','UpdateGestureAgentInventory'][index];endpoint=upload_metadata(name,{'item_id':item['inventoryID']})
        if index==0:
         status,_=post(endpoint,b'');check(status==400,'Empty binary request retains generic uploader')
         status,_=post(endpoint,b'x'*10000001);check(status==413,'Generic10MB limit rejects oversized data before consuming uploader')
        status,payload=post(endpoint,body,'application/octet-stream');check(status==200,'Generic binary status');reply=decode(ET.fromstring(payload)[0]);denied=index in [6,7,8]
        if denied:check(reply.get('identifier')==item['inventoryID'] and reply.get('message')=='Failed to update inventory item asset','Generic permission/owner failure uses actual item identifier')
        else:
         check(reply['state']=='complete' and reply['new_inventory_item']==item['inventoryID'] and reply['new_asset']!='00000000-0000-0000-0000-000000000000' and 'compiled' not in reply and all(reply[k]==0 for k in ['new_next_owner_mask','new_group_mask','new_everyone_mask','inventory_item_flags']),'Generic completion preserves source UUID string/masks and no invented compile status')
         status,data,_=asset_fetch('ViewerAsset',({7:'notecard_id',10:'script_id',21:'gesture_id',56:'settings_id',57:'material_id'}[item['assetType']])+'='+reply['new_asset']);check(status==200 and data==body,'Actual generic upload raw bytes resolve through native asset capability')
        with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
         db.row_factory=sqlite3.Row;stored=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(item['inventoryID'],)).fetchone();check(stored['assetID']==(item['assetID'] if denied else reply['new_asset']) and all(stored[k]==v for k,v in item.items() if k!='assetID'),'Generic edit changes only actual asset reference, retaining metadata and permissions')
        status,_=post(endpoint,body);check(status==404,'Generic uploader is one-shot in actual HTTP registry')
       endpoint=upload_metadata('UpdateScriptAgent',{'item_id':'12345678-1234-4234-8234-123456789abc'});status,payload=post(endpoint,b'raw');reply=decode(ET.fromstring(payload)[0]);check(status==200 and reply['identifier']=='12345678-1234-4234-8234-123456789abc','Metadata stage does not invent an early unknown-item rejection')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Generic task upload ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       object_id,item_id=match[1],match[2];body=b'Actual task notecard\x00\xff\x00';status,payload=post(session_caps['UpdateNotecardTaskInventory'],json.dumps({'item_id':item_id,'task_id':object_id}).encode(),'application/json');metadata=decode(ET.fromstring(payload)[0]);check(status==200 and metadata['state']=='upload','Real task generic upload admission');status,payload=post(metadata['uploader'],body,'application/octet-stream');reply=decode(ET.fromstring(payload)[0]);check(status==200 and reply['state']=='complete' and reply['new_inventory_item']==item_id,'Real generic task completion');status,data,_=asset_fetch('ViewerAsset','notecard_id='+reply['new_asset']);check(status==200 and data==body,'Actual task uploaded bytes stored and retrievable');process.stdin.write('OK '+reply['new_asset']+'\n');process.stdin.flush()
      if match:=re.fullmatch(r'Script running query ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([01])\s*',line):
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       try:
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
       except Exception as error:raise AssertionError('GetScriptRunning reply failed for '+match[2]+'; probe tail: '+''.join(lines[-6:])) from error
       check(response.status==200 and b'ScriptRunningReply' in event_body and match[1].encode() in event_body and match[2].encode() in event_body and ('<key>Running</key><boolean>'+match[3]+'</boolean>').encode() in event_body and b'<key>Mono</key><boolean>1</boolean>' in event_body,'GetScriptRunning produces actual EventQueue script state')
       if failed_item:check(failed_item.encode() not in event_body,'Failed compilation removed old instance without a false running reply')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.assets and (match:=re.fullmatch(r'Script save query ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([1-4])\s*',line)):
       object_id,item_id,mode=match[1],match[2],int(match[3]);edit_phase=mode;phase_payload_seen=0;failed_item=item_id if mode==2 else None
       sources={1:b'// original asset bytes retain invalid UTF8: \xff\ndefault { state_entry() { llSay(0, "Saved native"); } }',2:b'default { state_entry() { llDoesNotExist(); } }',3:b'default { state_entry() { llSay(0, "Stopped native should not execute"); } }',4:b'default { state_entry() { llSay(0, "Recovered native"); } }\0'}
       expected_script=sources[mode]
       metadata={'item_id':item_id,'task_id':object_id,'is_script_running':mode!=3}
       if mode==1:
        try:urllib.request.urlopen(session_caps['UpdateScriptTask'],timeout=5);raise AssertionError('Script editor GET unexpectedly accepted')
        except urllib.error.HTTPError as error:check(error.code==404,'Script editor requires POST')
        status,payload=post(session_caps['UpdateScriptTask'],b'<llsd><map /></llsd>');check(status==200 and b'failed to recode request' in payload,'Script editor preserves zero-ID source error')
       request_body=json.dumps(metadata).encode() if mode==3 else ('<llsd><map><key>item_id</key><uuid>'+item_id+'</uuid><key>task_id</key><uuid>'+object_id+'</uuid><key>is_script_running</key><boolean>'+('0' if mode==3 else '1')+'</boolean></map></llsd>').encode()
       status,payload=post(session_caps['UpdateScriptTaskInventory' if mode==2 else 'UpdateScriptTask'],request_body,'application/json' if mode==3 else 'application/llsd+xml');upload=decode(ET.fromstring(payload)[0]);check(status==200 and upload['state']=='upload','Real script editor returns upload URL')
       uploader=upload['uploader'];status,_=post(uploader,b'','application/octet-stream');check(status==400,'Empty upload retains one-shot URL')
       status,payload=post(uploader,expected_script,'application/octet-stream');complete=decode(ET.fromstring(payload)[0]);check(status==200 and complete['state']=='complete' and complete['new_asset']==item_id,'Source script completion returns actual item UUID')
       check(complete['compiled']==(mode!=2) and bool(complete['errors'])==(mode==2),'Actual native compiler success/failure and diagnostics')
       status,_=post(uploader,expected_script,'application/octet-stream');check(status==404,'Script upload URL consumed exactly once')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.assets and (match:=re.fullmatch(r'Existing library script asset ([0-9a-f-]{36})\s*',line)):
       status,data,_=asset_fetch('ViewerAsset','script_id='+match[1]);check(status==200 and data==assets[match[1]].read_bytes(),'Rezzed library script retains actual shipped asset bytes')
       process.stdin.write('OK\n');process.stdin.flush()
      if line.startswith('Task inventory persisted '):
       expected=json.loads(line[len('Task inventory persisted '):]);phase=expected.pop('phase')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row;row=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(expected['inventoryID'],)).fetchone()
        check(row is not None and all(row[key]==value for key,value in expected.items()),'Viewer transfer persisted exact source identity, asset, creator, flags, masks and destination')
        folder=db.execute('SELECT type FROM inventoryfolders WHERE folderID=?',(row['parentFolderID'],)).fetchone()
        check(folder is not None and ((phase==0 and folder[0]==10) or (phase==1 and row['parentFolderID']==target_folder) or (phase>=2 and folder[0]==14)),'Real system-folder fallback / explicit destination / Trash selection')
        check(row['assetType']==10 and row['invType']==10 and row['creationDate']>111,'Transferred script types and fresh creation time')
       status,data,_=asset_fetch('ViewerAsset','script_id='+expected['assetID']);check(status==200 and bool(data),'Transferred item retains retrievable actual asset')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Task inventory dropped ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([01])\s*',line):
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        row=db.execute('SELECT inventoryID FROM inventoryitems WHERE inventoryID=?',(match[1],)).fetchone()
        check(row is None,'Actual no-copy user drop consumes user item; library fallback creates no user original')
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
       check(response.status==200 and match[3].encode() in event_body and match[2].encode() not in event_body and b'ScriptRunningReply' in event_body,'Inventory drop creates no running instance; existing edited script remains running')
       check(b'RemoveInventoryItem' not in event_body,'Source drop removes stored no-copy item without an invented viewer-removal notification')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'User inventory operation ([0-8]) ([0-9a-f-]{36})\s*',line):
       phase=int(match[1]);events=[];event_bodies=[];ack=None;event_deadline=time.monotonic()+15
       while True:
        check(time.monotonic()<event_deadline,'User inventory owner-thread barrier deadline')
        event_request=urllib.request.Request(session_caps['EventQueueGet'],('<llsd><map><key>ack</key>'+('<undef/>' if ack is None else '<integer>'+str(ack)+'</integer>')+'<key>done</key><boolean>0</boolean></map></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
        event_bodies.append(event_body);event_data=decode(ET.fromstring(event_body)[0]);events.extend(event_data['events']);ack=event_data['id']
        if any(e['message']=='ScriptRunningReply' and any(script['ItemID']==match[2] for script in e['body']['Script']) for e in events):break
       bulk=[e for e in events if e['message']=='BulkUpdateInventory']
       check(bool(bulk)==(phase in [2,3,5]),'Source bulk-notification conditions for real user inventory operation '+str(phase))
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row
        def stored(key):return db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(user_operations[key],)).fetchone()
        item,landmark,foreign=stored('item'),stored('landmark'),stored('foreign')
        check(foreign is not None and foreign['inventoryName']=='Foreign source' and foreign['avatarID']!=session['agent_id'],'Foreign-owner inventory update is rejected before mutation')
        if phase<=5:
         check(item is not None and item['inventoryName']=='Edited user' and item['inventoryDescription']=='Updated' and item['inventoryNextPermissions']==0x8e000 and item['inventoryEveryOnePermissions']==0x1e000 and item['inventoryGroupPermissions']==0x8000 and item['flags']==0x201c10ff,'Actual user update preserves export rules, low flags and overwrite/sale flags; zero-next update leaves storage unchanged')
         check(item['inventoryBasePermissions']==0x9e000 and item['inventoryCurrentPermissions']==0x9e000 and item['invType']==10 and item['assetType']==10 and item['creatorID'].startswith(session['agent_id']+';') and item['creationDate']>222,'Scene and SQLite provider preserve protected fields, creator identification and real creation time')
         check(item['salePrice']==99 and item['saleType']==2 and item['groupOwned']==1 and item['groupID']==user_operations['group'],'Actual sale and group metadata persisted')
         check(item['parentFolderID']==(user_operations['destination'] if phase==5 else target_folder),'Update ignores wire folder; move changes real parent')
        else:check(item is None,'RemoveInventoryItem deletes actual SQLite row')
        if phase<7:
         check(landmark is not None and landmark['inventoryName']==('HG Edited landmark' if phase>=3 else 'HG Old') and landmark['inventoryDescription']==('edited @ http://grid' if phase>=3 else 'old @ http://grid'),'Landmark bulk-only versus persisted update preserves HG prefix and remote suffix')
         if phase>=3:check(landmark['invType']==3 and landmark['parentFolderID']==(user_operations['destination'] if phase>=5 else target_folder),'Provider retains landmark type and move ignores rename')
        else:check(landmark is None,'RemoveInventoryObjects invokes item deletion even when protected folder removal fails')
        check(db.execute('SELECT folderID FROM inventoryfolders WHERE folderID=?',(user_operations['destination'],)).fetchone() is not None,'Protected system folder remains stored')
        if phase==8:check(db.execute('SELECT folderID FROM inventoryfolders WHERE folderID=?',(user_operations['trash_folder'],)).fetchone() is None and stored('trash_item') is None,'Actual Trash-child folder deletion purges its persisted item')
       all_events=b''.join(event_bodies)
       if phase in [2,3]:check(b'HG Edited landmark' in all_events and b'edited @ http://grid' in all_events,'Landmark source correction reaches actual EventQueue bulk update')
       if phase==5:check(all(key.encode() in all_events for key in [user_operations['item'],user_operations['landmark']]),'Both moved items reach actual EventQueue bulk notifications')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory link moved ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       events=[];ack=None;limit=time.monotonic()+15
       while True:
        check(time.monotonic()<limit,'Root link move owner-thread barrier')
        event_request=urllib.request.Request(session_caps['EventQueueGet'],('<llsd><map><key>ack</key>'+('<undef/>' if ack is None else '<integer>'+str(ack)+'</integer>')+'<key>done</key><boolean>0</boolean></map></llsd>').encode(),{'Content-Type':'application/llsd+xml'})
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
        payload=decode(ET.fromstring(event_body)[0]);events.extend(payload['events']);ack=payload['id']
        if any(e['message']=='ScriptRunningReply' and any(row['ItemID']==match[3] for row in e['body']['Script']) for e in events):break
       check(any(e['message']=='BulkUpdateInventory' and match[1] in str(e['body']) for e in events),'Real root link move reaches source bulk notification')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:check(db.execute('SELECT parentFolderID,inventoryName FROM inventoryitems WHERE inventoryID=?',(match[1],)).fetchone()==(match[2],'Viewer link 7'),'Actual root link moves into destination and ignores rename')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:check(db.execute('SELECT version FROM inventoryfolders WHERE folderID=?',(parent,)).fetchone()[0]==root_version_expected+2,'Source move increments old root folder version once')
       root_version_expected+=2
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory link persisted (\d+) ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       phase=int(match[1]);at=25 if phase in [2,9] else 7 if phase==5 else 24;it=8 if phase==2 else 7 if phase==5 else 96 if phase==6 else -1 if phase==9 else 10
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row;row=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(match[2],)).fetchone()
        check(row is not None and row['assetID']==match[3] and row['parentFolderID']==match[4] and row['avatarID']==session['agent_id'] and row['creatorID']==session['agent_id'] and row['creationDate']>222,'Actual link identity/owner/creator/date/storage and target-as-asset')
        check(row['inventoryName']==('Duplicate link' if phase<2 else 'Viewer link '+str(phase)) and row['inventoryDescription']=='Linked from viewer' and row['assetType']==at and row['invType']==it and row['flags']==0,'Actual source-supplied link metadata and arbitrary wire types')
        check(row['inventoryBasePermissions']==(0x8e000 if phase==6 else 0x9e000) and row['inventoryCurrentPermissions']==(0x8e000 if phase==6 else 0x9e000) and row['inventoryNextPermissions']==(0xc000 if phase==6 else 0x9e000) and row['inventoryEveryOnePermissions']==(0 if phase==6 else 0x8e000) and row['inventoryGroupPermissions']==(0 if phase==6 else 0x9e000),'Actual link permission construction and animation special case')
        check(row['groupID']=='00000000-0000-0000-0000-000000000000' and row['groupOwned']==0 and row['salePrice']==0 and row['saleType']==0,'Actual link fresh group/sale defaults')
        if phase==1:check(db.execute('SELECT COUNT(*) FROM inventoryitems WHERE inventoryName=? AND assetID=? AND parentFolderID=?',('Duplicate link',match[3],match[4])).fetchone()[0]>=2,'Source explicitly permits duplicate links')
        if phase==7:check(db.execute('SELECT parentFolderID FROM inventoryfolders WHERE folderID=?',(match[4],)).fetchone()[0]=='00000000-0000-0000-0000-000000000000','Zero link folder uses source root fallback because asset type24 is not FolderType')
        if phase==7:check(db.execute('SELECT version FROM inventoryfolders WHERE folderID=?',(parent,)).fetchone()[0]==root_version_expected+1,'Source root link creation increments actual folder version once')
        if phase==8:check(db.execute('SELECT folderID FROM inventoryfolders WHERE folderID=?',(match[4],)).fetchone() is None,'Link source performs no folder validation before actual store')
        if phase==10:check(db.execute('SELECT assetType FROM inventoryitems WHERE inventoryID=?',(match[3],)).fetchone()[0]==24,'Link-to-link retains intermediate item UUID instead of flattening')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory new persisted (\d+) ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       phase=int(match[1]);types=[10,7,57,5,3,56,10,10,-1,13,10];invs=[10,7,26,18,3,10,27,3,10,18,10];asset_id=match[3]
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row;row=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(match[2],)).fetchone()
        check(row is not None and row['avatarID']==session['agent_id'] and row['creatorID']==session['agent_id'] and row['assetID']==asset_id and row['parentFolderID']==match[4] and row['inventoryName']=='Viewer new '+str(phase) and row['inventoryDescription']=='Created from viewer','Actual viewer-created item metadata and storage')
        check(row['assetType']==types[phase] and row['invType']==invs[phase] and row['inventoryBasePermissions']==0x9e000 and row['inventoryCurrentPermissions']==0x9e000 and row['inventoryNextPermissions']==0x82000 and row['creationDate']>222,'Actual new item types, masks and handler creation date')
        check(row['inventoryEveryOnePermissions']==(0x9a000 if phase in [4,7] else 0) and row['inventoryGroupPermissions']==(0x9e000 if phase in [4,7] else 0) and row['flags']==(255 if phase==3 else 9 if phase==9 else 0),'Actual landmark and wearable permission/flag rules')
        check(row['groupID']=='00000000-0000-0000-0000-000000000000' and row['groupOwned']==0 and row['salePrice']==0 and row['saleType']==0,'Actual new item group and sale defaults')
        if phase==1:check(db.execute('SELECT type FROM inventoryfolders WHERE folderID=?',(match[4],)).fetchone()[0]==7,'Zero requested folder falls through source AddInventoryItem to real system notecard folder')
        if phase==10:check(db.execute('SELECT folderID FROM inventoryfolders WHERE folderID=?',(match[4],)).fetchone() is None,'Source fallback validation preserves original nonzero folder ID, including orphan storage')
       defaults={0:'2074003b-8d5f-40e5-8b20-581c1c50aedb',1:'4b6a777d-7bcd-4fc4-b06f-929b21e32925',2:'968cbad0-4dad-d64e-71b5-72bf13ad051a',10:'2074003b-8d5f-40e5-8b20-581c1c50aedb'}
       if phase in defaults:check(asset_id==defaults[phase],'Actual source default asset reference')
       if phase in [0,1,2,4,7,10]:
        status,data,_=asset_fetch('ViewerAsset','notecard_id='+asset_id)
        check(status==200 and bool(data),'Actual new inventory asset resolves through asset service')
        if phase in [4,7]:check(data.startswith(b'Landmark version 2\nregion_id ') and b'\nlocal_pos ' in data and b'\nregion_handle ' in data and data.endswith(b'\n'),'Real generated landmark text and stored bytes; inventory type precedence over script asset type')
       else:check(asset_id=='00000000-0000-0000-0000-000000000000','Source creates wearable/unknown metadata without an invented asset')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory new denied ([56]) ([0-9a-f-]{36})\s*',line):
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
       check(match[2].encode() in event_body and b'ScriptRunningReply' in event_body,'New inventory owner-thread denial barrier')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:check(db.execute('SELECT inventoryID FROM inventoryitems WHERE inventoryName=?',('Viewer new '+match[1],)).fetchone() is None,'Source rejected inventory creation does not store an item')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory copy persisted ([0-6]) ([0-9a-f-]{36})\s*',line):
       phase=int(match[1]);profile=copy_profiles[phase];expected=profile['expected']
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row;row=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(match[2],)).fetchone()
        check(row is not None and all(row[key]==value for key,value in expected.items()) and row['creationDate']>222,'Actual copy persists exact source identity, creator data, masks, full flags, types, fresh group/sale defaults and destination')
        check(db.execute('SELECT inventoryID FROM inventoryitems WHERE inventoryID=?',(profile['old_item'],)).fetchone() is not None or phase==5,'Copy retains actual original user item')
        copy_ids_snapshot={row[0] for row in db.execute('SELECT inventoryID FROM inventoryitems')}
       status,data,_=asset_fetch('ViewerAsset','script_id='+expected['assetID']);check(status==200 and data==assets[expected['assetID']].read_bytes(),'Actual copy asset reference resolves to exact original stored bytes')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Inventory copy denied ([0-6]) ([0-9a-f-]{36})\s*',line):
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
       check(response.status==200 and b'ScriptRunningReply' in event_body and match[2].encode() in event_body,'Copy-denial owner-thread barrier')
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:check({row[0] for row in db.execute('SELECT inventoryID FROM inventoryitems')}==copy_ids_snapshot,'Copy/Transfer denial creates no stored item and removes no original')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Retained script trash ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        db.row_factory=sqlite3.Row;row=db.execute('SELECT i.* FROM inventoryitems i JOIN inventoryfolders f ON i.parentFolderID=f.folderID WHERE i.avatarID=? AND i.inventoryID=? AND i.inventoryName=? AND f.type=14 AND i.invType=7',(session['agent_id'],match[3],'Edited script')).fetchone()
        check(row is not None and row['inventoryID']!=match[2] and row['assetType']==10 and row['assetID']==match[4],'Source non-LSL-inventory-type deletion still stores an actual new Trash item')
        asset_id=row['assetID']
       status,data,_=asset_fetch('ViewerAsset','script_id='+asset_id);check(status==200 and data==default_script,'Retained engine uses the same actual stored default-touch script asset as its Trash copy')
       process.stdin.write('OK\n');process.stdin.flush()
      if match:=re.fullmatch(r'Script lifecycle missing ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([0-9a-f-]{36})\s*',line):
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
       check(response.status==200 and b'ScriptRunningReply' in event_body and match[3].encode() in event_body and match[2].encode() not in event_body,'Ordinary LSL inventory removal terminates its engine; retyped removed script remains queryable')
       process.stdin.write('OK\n');process.stdin.flush()
      if edit_phase and line.startswith('Verified task Contents serial '):check(phase_payload_seen>=1,'Updated Contents points to exact newly stored script bytes')

     process.wait(timeout=5);check(process.returncode==0,'Connected viewer probe: '+''.join(lines)+''.join(errors))
     if a.assets:check(len(set(script_ids))>=1,'Actual task script asset IDs returned through Contents/Xfer')
     if a.legacy:
      with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:row=db.execute('SELECT inventoryCurrentPermissions,creatorID FROM inventoryitems WHERE inventoryID=?',(script_source_id,)).fetchone()
      check(row is not None and row[0]==0x7fff7fff and row[1].startswith(session['agent_id']+';'),'Existing no-copy user item remains stored exactly as source rez path')
    finally:
     if process.poll() is None:process.kill();process.wait(timeout=5)
     reader.join(timeout=5);process.stdout.close();process.stdin.close()

   probe(login)
   # Scene logout disposes seed routes and removes session state; same account can log in again.
   deadline=time.monotonic()+5
   while True:
    try:
     with urllib.request.urlopen(request,timeout=5) as response:response.read()
    except urllib.error.HTTPError as e:
     if e.code==404:break
     raise
    if time.monotonic()>deadline:raise AssertionError('Logout retained seed route')
    time.sleep(.02)
   checks.append('Logout deregisters seed capability')
   for name,endpoint in library_urls.items():
    try:urllib.request.urlopen(urllib.request.Request(endpoint,b'<llsd><map /></llsd>'),timeout=5);raise AssertionError('Logout retained '+name)
    except urllib.error.HTTPError as e:check(e.code==404,'Logout removes '+name+' route')
   if folder_url:
    try:urllib.request.urlopen(urllib.request.Request(folder_url,b'<llsd><map /></llsd>'),timeout=5);raise AssertionError('Logout retained folder route')
    except urllib.error.HTTPError as e:check(e.code==404,'Logout removes folder capability route')
   if inventory_url:
    try:urllib.request.urlopen(urllib.request.Request(inventory_url,b'{"items":[]}'),timeout=5);raise AssertionError('Logout retained item route')
    except urllib.error.HTTPError as e:check(e.code==404,'Logout removes FetchInventory2 route')
   if a.assets:
    for name,endpoint in asset_urls.items():
     try:urllib.request.urlopen(endpoint+'?texture_id=89556747-24cb-43ed-920b-47caed15465f',timeout=5);raise AssertionError('Logout retained '+name)
     except urllib.error.HTTPError as e:check(e.code==404,'Logout removes '+name+' route')
   again=proxy.login_to_simulator(credentials);check(again.get('login')=='true' and again['session_id']!=login['session_id'],'Logout removes presence and permits fresh session')
   if a.physics_library:probe(again)
  finally:
   if proc.poll() is None:
    proc.stdin.write('quit\n');proc.stdin.flush()
    try:proc.wait(timeout=15)
    except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
   check(proc.returncode==0,'Clean region/socket/capability shutdown: '+''.join(errors))
 report=dict(passed=True,checks=checks,scope='Native executable HTTP login/seed plus UDP handshake/root/terrain/avatar/wearables, primitive create/edit, actual default LSL state_entry and optional touch_start, optional real ubODE walk/turn/run/jump/fly/hover and animation UUIDs, optional shipped SQLite assets/capabilities/ranges/concurrent requests, actual region-loaded default XML cache retrieval after deleting its SQL copy before the first HTTP request, actual SQLite FetchInventory2 item retrieval/source serialization/statuses, before-root legacy item/folder/library fetches, real task Contents/Xfer cache and mutation, script assets, GetScriptRunning EventQueue and two-stage script save/compile/error/stopped/recovery plus viewer start/stop/reset and stopped-reset continuation with exact stored invalid-UTF8/trailing-NUL bytes and real ObjectProperties serials, logout/relogin. Actual viewer rendering, complete physics/collision engines, complete objects/permissions, full YEngine language/runtime, asset/inventory transfer and Unix execution remain pending.')
 (a.work/'connected-region-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(f'PASS: {len(checks)} connected native region network checks')
if __name__=='__main__':main()
