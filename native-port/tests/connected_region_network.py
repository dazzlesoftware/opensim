"""Real native login, seed negotiation and UDP scene lifecycle; no .NET server."""
import argparse,json,queue,subprocess,threading,time,urllib.request,urllib.error,xmlrpc.client
from pathlib import Path
import tempfile
import math
import sqlite3
import contextlib
import xml.etree.ElementTree as ET
import base64
import concurrent.futures
import re
import uuid
import http.client
import http.server
import socket
import ssl
import ssl
import struct
import zlib
import xmlrpc.server
import uuid
import gzip
import urllib.parse
import os

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--profiles-disabled',action='store_true');p.add_argument('--advanced-profiles',action='store_true');p.add_argument('--viewer',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--touch',action='store_true');p.add_argument('--assets',action='store_true');p.add_argument('--inventory',action='store_true');p.add_argument('--folders',action='store_true');p.add_argument('--library',action='store_true');p.add_argument('--legacy',action='store_true');p.add_argument('--bakes',action='store_true');p.add_argument('--physics-library',type=Path);p.add_argument('--block-flight',action='store_true');p.add_argument('--disable-scripts',action='store_true');p.add_argument('--raw-fps',action='store_true');p.add_argument('--remote-mutes',action='store_true')
 p.add_argument('--presence-provider', choices=('MySQL', 'PGSQL'))
 p.add_argument('--script-https', action='store_true')
 p.add_argument('--script-https-external', action='store_true')
 p.add_argument('--script-url-denied', action='store_true')
 p.add_argument('--script-url-hostname', action='store_true')
 p.add_argument('--script-url-quota', action='store_true')
 p.add_argument('--primary-https', action='store_true')
 p.add_argument('--grid-user-provider', choices=('MySQL', 'PGSQL'))
 p.add_argument('--telehub-parcel-access', action='store_true')
 p.add_argument('--group-parcel-access', action='store_true')
 p.add_argument('--script-parcel-access', action='store_true')
 p.add_argument('--script-parcel-denied', action='store_true')
 p.add_argument('--script-group-parcel-access', action='store_true')
 p.add_argument('--script-group-parcel-ban', action='store_true')
 p.add_argument('--script-parcel-reset', action='store_true')
 p.add_argument('--parcel-profile-access', action='store_true')
 p.add_argument('--estate-profile-access', action='store_true')
 p.add_argument('--parcel-relocation', action='store_true')
 p.add_argument('--varregion-relocation', action='store_true')
 p.add_argument('--group-terrain', action='store_true')
 p.add_argument('--public-terrain', action='store_true')
 p.add_argument('--sl-landing', action='store_true')
 p.add_argument('--os-landing', action='store_true')
 p.add_argument('--pbr-terrain', action='store_true')
 p.add_argument('--late-pbr-terrain', action='store_true')
 p.add_argument('--extended-pbr-terrain', action='store_true')
 a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);checks=[]
 a.script_https = a.script_https or a.script_https_external or a.primary_https
 if a.late_pbr_terrain or a.extended_pbr_terrain:
  a.pbr_terrain = True
 if a.pbr_terrain:
  a.os_landing = True
 if a.group_terrain or a.public_terrain:
  a.varregion_relocation = True
 if a.script_group_parcel_access or a.script_group_parcel_ban:
  a.script_parcel_denied = True
  a.group_parcel_access = True
 if a.estate_profile_access:
  a.parcel_profile_access = True
 if (a.group_parcel_access or a.script_parcel_access or a.script_parcel_denied
     or a.script_parcel_reset or a.parcel_profile_access):
  a.telehub_parcel_access = True
 if a.script_group_parcel_access:
  parcel_member_powers = 1 << 31
 elif a.script_group_parcel_ban:
  parcel_member_powers = 1 << 30
 else:
  parcel_member_powers = 1 << 29
 if a.varregion_relocation:
  a.parcel_relocation = True
 if a.parcel_relocation:
  a.telehub_parcel_access = True
 if a.presence_provider:
  presence_connection = os.environ.get('OPENSIM_PRESENCE_TEST_CONNECTION', '')
  if not presence_connection or any(character in presence_connection for character in '\r\n'):
   p.error('Set OPENSIM_PRESENCE_TEST_CONNECTION to an isolated test database')
 if a.grid_user_provider:
  grid_user_connection = os.environ.get('OPENSIM_GRID_USER_TEST_CONNECTION', '')
  if not grid_user_connection or any(character in grid_user_connection for character in '\r\n"'):
   p.error('Set OPENSIM_GRID_USER_TEST_CONNECTION to an isolated test database')
 if a.touch:a.assets=True # A configured script override must store the exact executed asset.
 def check(ok,name):
  if not ok:raise AssertionError(name)
  checks.append(name)
 with tempfile.TemporaryDirectory(dir=a.work) as folder, contextlib.ExitStack() as cleanup:
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
[ClientStack.LindenUDP]
client_socket_rcvbuf_size = 262144
scene_throttle_max_bps = 0
[Terrain]
InitialTerrain = flat
[GridService]
DefaultRegionFlags = +DefaultRegion +FallbackRegion
ExportSupported = false
[LoginService]
DestinationGuide = https://example.invalid/guide/[USERID]
''',encoding='utf-8')
  if a.touch:
   grid_lifecycle=queue.Queue()
   grid_lookups=queue.Queue()
   transfer_requests=queue.Queue()
   transfer_updates=queue.Queue()
   estate_deliveries=queue.Queue()
   group_requests=queue.Queue()
   active_groups={}
   active_roles={}
   group_preferences={}
   remote_builder='3288a602-93c3-4b9c-8d36-02b5e84c2580'
   remote_host='a8db089d-15d3-4b15-a5da-22de8a147d86'
   estate_access_group='ac20e107-f888-403a-a07d-8f05a82ce03b'
   destination_id='82828282-8282-4282-8282-828282828282'
   estate_peer_alias='83838383-8383-4383-8383-838383838383'
   class FeatureGrid(http.server.BaseHTTPRequestHandler):
    def reply(self,body):
     self.send_response(200)
     self.send_header('Content-Length',str(len(body)))
     self.end_headers()
     self.wfile.write(body)
    def do_QUERYACCESS(self):
     self.rfile.read(int(self.headers['Content-Length']))
     transfer_requests.put((self.command,self.path))
     self.reply(b'{"success":true,"negotiated_inbound_version":0.4,"negotiated_outbound_version":0.4}')
    def do_PUT(self):
     payload=self.rfile.read(int(self.headers['Content-Length']))
     if payload.startswith(b'\x1f\x8b'):payload=gzip.decompress(payload)
     transfer_updates.put(json.loads(payload))
     transfer_requests.put((self.command,self.path))
     self.reply(b'{"success":true}')
    def do_DELETE(self):
     transfer_requests.put((self.command,self.path))
     self.reply(b'{}')
    def do_POST(self):
     if self.path.startswith('/agent/'):
      self.rfile.read(int(self.headers['Content-Length']))
      transfer_requests.put((self.command,self.path))
      self.reply(b'{"success":true}')
      return
     fields=urllib.parse.parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
     if self.path=='/groups':
      if self.headers.get('Authorization')!='Basic Z3JpZDpmaXh0dXJl':
       self.send_error(401)
       return
      group_requests.put(fields)
      body=b'<ServerResponse><RESULT>NULL</RESULT></ServerResponse>'
      group_agent=fields.get('AgentID',[''])[0]
      if fields.get('METHOD')==['SETACTIVE'] and fields.get('OP')==['GROUP']:
       active_groups[group_agent]=fields['GroupID'][0]
      elif fields.get('METHOD')==['SETACTIVE'] and fields.get('OP')==['ROLE']:
       active_roles[group_agent]=fields['RoleID'][0]
      elif fields.get('METHOD')==['UPDATEMEMBERSHIP']:
       if fields.get('RequestingAgentID')==[group_agent] and fields.get('GroupID')==['00000000-0000-4000-8000-000000000003']:
        group_preferences[group_agent]=(fields['AcceptNotices'][0],fields['ListInProfile'][0])
      elif fields.get('METHOD')==['GETAGENTROLES'] and group_agent==manager:
       body=(f'<ServerResponse><RESULT type="List"><r-0 type="List"><Name>Builders</Name><RoleID>{remote_builder}</RoleID></r-0>'
             f'<r-1 type="List"><Name>Hosts</Name><RoleID>{remote_host}</RoleID></r-1></RESULT></ServerResponse>').encode()
      elif not fields.get('ALL'):
       if active_groups.get(group_agent)=='00000000-0000-4000-8000-000000000003':
        active_role=active_roles.get(group_agent,remote_builder)
        title,powers=('Host',42) if active_role==remote_host else ('Builder',274877906944)
        body=(f'<ServerResponse><RESULT type="List"><GroupID>00000000-0000-4000-8000-000000000003</GroupID>'
              f'<GroupName>Parcel Group</GroupName><GroupTitle>{title}</GroupTitle><GroupPowers>{powers}</GroupPowers>'
              f'<ActiveRole>{active_role}</ActiveRole></RESULT></ServerResponse>').encode()
      elif fields.get('AgentID')==[str(cancel_target)]:
       body=(f'<ServerResponse><RESULT type="List"><m-0 type="List"><GroupID>{estate_access_group}</GroupID>'
             '</m-0></RESULT></ServerResponse>').encode()
      elif (a.group_parcel_access or a.group_terrain) and group_agent == parcel_observer:
       body = (
           '<ServerResponse><RESULT type="List"><m-0 type="List">'
           '<GroupID>00000000-0000-4000-8000-000000000003</GroupID>'
           f'<GroupPowers>{parcel_member_powers}</GroupPowers><GroupName>Parcel Group</GroupName>'
           '</m-0></RESULT></ServerResponse>'
       ).encode()
      elif fields.get('AgentID')==[manager]:
       notices,profile=group_preferences.get(group_agent,('True','False'))
       body=(b'<ServerResponse><RESULT type="List"><m-0 type="List">'
             b'<GroupID>00000000-0000-4000-8000-000000000003</GroupID><GroupPowers>274877906944</GroupPowers>'
             b'<GroupName>Parcel Group</GroupName><Contribution>42</Contribution>'+
             f'<AcceptNotices>{notices}</AcceptNotices><ListInProfile>{profile}</ListInProfile>'.encode()+
             b'</m-0></RESULT></ServerResponse>')
      self.reply(body)
      return
     if self.path=='/estate':
      estate_deliveries.put(fields)
      self.reply(b'<ServerResponse><RESULT>True</RESULT></ServerResponse>')
      return
     if self.path!='/grid':
      self.send_error(400)
      return
     if self.headers.get('Authorization')!='Basic Z3JpZDpmaXh0dXJl':
      self.send_error(401)
      return
     if fields=={'METHOD':['get_grid_extra_features']}:
      body=(b'<ServerResponse><ExportSupported>False</ExportSupported>'
            b'<destination-guide-url>https://example.invalid/guide/[USERID]</destination-guide-url></ServerResponse>')
     elif fields.get('METHOD') in (['register'],['deregister']):
      grid_lifecycle.put(fields)
      body=b'<ServerResponse><Result>Success</Result></ServerResponse>'
     elif fields.get('METHOD') in (['get_region_by_position'],['get_region_by_uuid']):
      by_position=fields.get('METHOD')==['get_region_by_position']
      if by_position:grid_lookups.put(fields)
      body=b'<ServerResponse><result>null</result></ServerResponse>'
      requested_region=fields.get('REGIONID',[''])[0]
      if (by_position and fields.get('X')==['1024']) or (
          not by_position and requested_region in (destination_id,estate_peer_alias)):
       port=self.server.server_port
       region_id=destination_id if by_position else requested_region
       body=(f'<ServerResponse><result type="List"><uuid>{region_id}</uuid>'
             '<locX>1024</locX><locY>768</locY><sizeX>256</sizeX><sizeY>256</sizeY>'
             '<regionName>Remote Test Region</regionName><serverIP>127.0.0.1</serverIP>'
             f'<serverHttpPort>{port}</serverHttpPort><serverURI>http://127.0.0.1:{port}/</serverURI>'
             '<serverPort>19000</serverPort></result></ServerResponse>').encode()
     else:
      self.send_error(400)
      return
     self.reply(body)
   grid_features=cleanup.enter_context(http.server.ThreadingHTTPServer(('127.0.0.1',0),FeatureGrid))
   threading.Thread(target=grid_features.serve_forever,daemon=True).start()
   cleanup.callback(grid_features.shutdown)
   config.write_text(config.read_text(encoding='utf-8').replace('UserAccountDatabase = users.db',
      'UserAccountDatabase = users.db\nPresenceDatabase = presence.db').replace('ExportSupported = false',
      f'SuppressConsoleCommands = true\nGridServerURI = http://127.0.0.1:{grid_features.server_port}\n'
      'AuthType = BasicHttpAuthentication\nHttpAuthUsername = grid\nHttpAuthPassword = fixture'),encoding='utf-8')
   if a.physics_library:
    preferences_config=root/'preferences-server.ini'
    preferences_config.write_text('''[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=preferences-users.db
AuthenticationDatabase=preferences-users.db
EnableAgentPreferencesEndpoint=true
AgentPreferencesDatabase=remote-preferences.db
[Network]
http_listener_port=0
[AgentPreferencesService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=grid
HttpAuthPassword=fixture
''',encoding='utf-8')
    preferences_command=[str(Path(a.native).resolve()),'--config',str(preferences_config)]
    initialized=subprocess.run(preferences_command+['--init-db'],capture_output=True,text=True,timeout=20)
    check(initialized.returncode==0,'Native preferences server database initializes: '+initialized.stderr)
    preferences_process=subprocess.Popen(preferences_command+['--console'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT,text=True,encoding='utf-8')
    def stop_preferences_server():
     if preferences_process.poll() is None:
      try:preferences_process.communicate('quit\n',timeout=10)
      except subprocess.TimeoutExpired:
       preferences_process.kill();preferences_process.communicate()
     else:preferences_process.communicate()
    cleanup.callback(stop_preferences_server)
    preferences_listening=queue.Queue()
    def read_preferences_startup():
     for line in preferences_process.stdout:
      if line.startswith('LISTENING '):
       preferences_listening.put(line.split(' ',1)[1].strip())
       return
     preferences_listening.put(None)
    threading.Thread(target=read_preferences_startup,daemon=True).start()
    preferences_uri=preferences_listening.get(timeout=20)
    check(bool(preferences_uri),'Native preferences endpoint starts')
    with config.open('a',encoding='utf-8') as settings:
     settings.write(f'\n[Modules]\nAgentPreferencesServices=RemoteAgentPreferencesServicesConnector\n[AgentPreferencesService]\nAgentPreferencesServerURI={preferences_uri}\nAuthType=BasicHttpAuthentication\nHttpAuthUsername=grid\nHttpAuthPassword=fixture\n')
  if a.profiles_disabled:config.write_text(config.read_text(encoding='utf-8')+'\n[UserProfiles]\n',encoding='utf-8')
  if a.advanced_profiles:
   offline_requests=queue.Queue()
   class OfflineReceiver(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
     if self.headers.get('Authorization')!='Basic b2ZmbGluZTpmaXh0dXJl':
      self.send_error(401)
      return
     fields=urllib.parse.parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
     offline_requests.put((self.path,fields))
     target=fields.get('PrincipalID',[''])[0]
     if fields.get('METHOD')==['STORE'] and fields.get('ToAgentID')==['95959595-9595-4595-8595-959595959595']:
      body=b'<ServerResponse><RESULT>False</RESULT><REASON>Mailbox full</REASON></ServerResponse>'
     elif fields.get('METHOD')==['STORE']:
      body=b'<ServerResponse><RESULT>True</RESULT></ServerResponse>'
     else:body=(f'<ServerResponse><RESULT type="List"><im0 type="List">'
           '<FromAgentID>93939393-9393-4393-8393-939393939393</FromAgentID>'
           f'<ToAgentID>{target}</ToAgentID><Dialog> +0 </Dialog><Timestamp> +42 </Timestamp><Message>Stored IM</Message>'
           '<BinaryBucket>00 01 FF</BinaryBucket></im0></RESULT></ServerResponse>').encode()
     self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
   offline=cleanup.enter_context(http.server.ThreadingHTTPServer(('127.0.0.1',0),OfflineReceiver))
   worker=threading.Thread(target=offline.serve_forever,daemon=True);worker.start()
   cleanup.callback(offline.shutdown)
   with socket.socket() as listener:
    listener.bind(('127.0.0.1',0));profile_port=listener.getsockname()[1]
   settings=config.read_text(encoding='utf-8').replace('http_listener_port = 0',f'http_listener_port = {profile_port}')
   settings=settings.replace('EnableLocalRegion = true','EnableLocalRegion = true\nEnableGridEndpoint=true\nEnablePresenceEndpoint=true')
   settings=settings.replace('[Network]','[Network]\nHttpAuthUsername=offline\nHttpAuthPassword=fixture')
   settings+=f'''
[UserProfiles]
ProfileServiceURL=http://127.0.0.1:{profile_port}
[Messaging]
AuthType=BasicHttpAuthentication
MaxOfflineIMs=1
Realm=fixture_offline
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=Data Source=offline.db
MessageKey=native-kick-fixture
OfflineMessageModule=Offline Message Module V2
OfflineMessageURL=http://127.0.0.1:{offline.server_port}/
[UserProfilesService]
Enabled=true
LocalServiceModule=OpenSim.Services.UserProfilesService.dll:UserProfilesService
UserAccountService=OpenSim.Services.UserAccountService.dll:UserAccountService
[DatabaseService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=Data Source={root/'profiles.db'}
[Modules]
UserProfilesServices=LocalUserProfilesServicesConnector
'''
   config.write_text(settings,encoding='utf-8')
  if a.assets:
   source=Path(__file__).resolve().parents[2]
   asset_config='\n[Modules]\nAssetServices=LocalAssetServicesConnector\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:'+str(root/'assets.db')+';version=3;Pooling=false\nDefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll\nAssetLoaderArgs='+str(source/'bin/assets/AssetSets.xml')+'\n[ClientStack.LindenCaps]\nCap_GetTexture=localhost\nCap_GetMesh=localhost\nCap_GetMesh2=localhost\nCap_GetAsset=localhost\n'
   if a.pbr_terrain:
    asset_config += 'Cap_EnvironmentSettings=localhost\n'
   config.write_text(config.read_text(encoding='utf-8').replace('EnableLocalRegion = true','EnableLocalRegion = true\nEnableRegionAssets = true')+asset_config,encoding='utf-8')
  if a.bakes:
   with socket.socket() as bakes_port_probe:
    bakes_port_probe.bind(('127.0.0.1',0))
    bakes_port=bakes_port_probe.getsockname()[1]
   settings=config.read_text(encoding='utf-8').replace('http_listener_port = 0',f'http_listener_port = {bakes_port}')
   settings=settings.replace('EnableLocalRegion = true','EnableLocalRegion = true\nEnableBakedTextureEndpoint = true')
   config.write_text(settings+f'\n[XBakes]\nURL = http://127.0.0.1:{bakes_port}/\n[BakedTextureService]\nBaseDirectory = xbakes\n',encoding='utf-8')
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
  rotating_prim = a.touch and not a.physics_library
  remote_language_probe = a.touch and bool(a.physics_library) and not a.legacy
  notecard_probe = remote_language_probe and a.assets
  notecard_id = 'e1856861-b452-44aa-9e8a-bf9ab0bf3d84'
  if a.touch:
   fixture=Path(__file__).resolve().parents[2]/'bin/assets/ScriptsAssetSet/DefaultSayAndTouch.lsl'
   if remote_language_probe:
    script=fixture.read_text(encoding='utf-8').replace('"Touched."',
     '"Touched. " + llGetAgentLanguage(llDetectedKey(0)) + "/" + llGetAgentLanguage("17495d6b-53c5-4ca7-a3fa-801ff116a0e1")')
    if notecard_probe:
     script = script.replace('llSay( 0, "I am Alive!");', '''
        llSay(0, "Notecard cold " + (string)(llGetNotecardLineSync(CARD_ID, 0) == NAK));
        count_query = llGetNumberOfNotecardLines(CARD_ID);
        line_query = llGetNotecardLine(CARD_ID, 2);
        llSay(0, "I am Alive!");
     ''')
     script = '''
       string CARD_ID = "TEST_NOTECARD_ID";
       key count_query;
       key line_query;
     ''' + script.rsplit('}', 1)[0] + '''
       dataserver(key query, string data) {
           if (query == count_query) llSay(0, "Notecard count " + data);
           if (query == line_query) {
               llSay(0, "Notecard lengths " + (string)llStringLength(data) + " " +
                   (string)llStringLength(llGetNotecardLineSync(CARD_ID, 2)));
               llSay(0, "Notecard edges " + (string)(llGetNotecardLineSync(CARD_ID, 3) == EOF) + " " +
                   (string)(llGetNotecardLineSync(CARD_ID, -1) == NAK) + " " +
                   (string)(llGetNotecardLineSync(CARD_ID, 1) == ""));
           }
       }
     }
     '''
     script = script.replace('TEST_NOTECARD_ID', notecard_id)
    fixture=root/'remote-language.lsl'
    fixture.write_text(script,encoding='utf-8')
   if rotating_prim:
    foreign_id = '3d80d6ea-257f-4ce8-87f7-284639eaf825'
    key_server = cleanup.enter_context(
        xmlrpc.server.SimpleXMLRPCServer(('127.0.0.1', 0), logRequests=False))
    foreign_key_calls = []

    def get_foreign_key(request):
     foreign_key_calls.append(request)
     if request == {'first': 'Foreign', 'last': 'Avatar'}:
      return {'UUID': foreign_id}
     return {'UUID': str(uuid.UUID(int=0))}

    key_server.register_function(get_foreign_key, 'get_uuid')
    key_worker = threading.Thread(target=key_server.serve_forever, daemon=True)
    key_worker.start()
    cleanup.callback(key_worker.join, 5)
    cleanup.callback(key_server.shutdown)
    fixture = root / 'rotating-prim.lsl'
    fixture.write_text('string fixtureName = "Rotating fixture"; default { state_entry() { llTargetOmega(<0,0,1>,2.5,1); llSay(0,fixtureName); llSay(0,"Root agents " + (string)llGetRegionAgentCount()); llSay(0,"Region agents " + llList2CSV(llGetAgentList(AGENT_LIST_REGION | AGENT_LIST_EXCLUDENPC, []))); llSay(0,"Parcel agents " + llList2CSV(llGetAgentList(AGENT_LIST_PARCEL, []))); llSay(0,"Parcel owner agents " + llList2CSV(llGetAgentList(AGENT_LIST_PARCEL_OWNER, []))); llSay(0,"Water " + (string)llWater(<1,2,3>) + " Ground " + (string)llGround(<1,2,3>)); llSay(0,"Region terraform flag " + (string)(llGetRegionFlags() & (REGION_FLAG_BLOCK_TERRAFORM | REGION_FLAG_ALLOW_DAMAGE | REGION_FLAG_BLOCK_FLY | REGION_FLAG_DISABLE_PHYSICS))); llSay(0,"Parcel flags " + (string)llGetParcelFlags(<128,128,25>) + " " + (string)llGetParcelFlags(<132,128,25>)); llSay(0,"Parcel details " + llList2CSV(llGetParcelDetails(<132,128,25>, [PARCEL_DETAILS_OWNER, PARCEL_DETAILS_AREA, PARCEL_DETAILS_ID, PARCEL_DETAILS_FLAGS, PARCEL_DETAILS_NAME, PARCEL_DETAILS_DESC, PARCEL_DETAILS_GROUP, PARCEL_DETAILS_GETCLAIMDATE, PARCEL_DETAILS_LANDING_POINT, PARCEL_DETAILS_LANDING_LOOKAT, PARCEL_DETAILS_TP_ROUTING, PARCEL_DETAILS_SEE_AVATARS, PARCEL_DETAILS_DWELL, PARCEL_DETAILS_PRIM_CAPACITY, PARCEL_DETAILS_PRIM_USED, PARCEL_DETAILS_SCRIPT_DANGER]))); llSay(0,"Script danger " + (string)llScriptDanger(<128,128,25>) + " " + (string)llScriptDanger(<132,128,25>) + " " + (string)llScriptDanger(<-1,128,25>)); llSay(0,"Parcel center " + (string)(llVecDist(llList2Vector(llGetParcelDetails(<132,128,25>, [PARCEL_DETAILS_GEOMETRICCENTER]), 0), <194,128,0>) < 0.001)); llSay(0,"Unix " + (string)llGetUnixTime()); llResetTime(); llSetTimerEvent(.1); } timer() { llSetTimerEvent(0); if(llGetTime() >= 1.75) { llSay(0,"Configured timer for " + llGetScriptName() + " type " + (string)llGetInventoryType(llGetScriptName()) + " missing " + (string)llGetInventoryType("Missing") + " count " + (string)llGetInventoryNumber(10) + " first " + llGetInventoryName(10,0)); llSay(0,"Inventory asset " + (string)llGetInventoryKey(llGetScriptName()) + " missing " + (string)llGetInventoryKey("Missing") + " owner mask " + (string)llGetInventoryPermMask(llGetScriptName(),MASK_OWNER)); } else llSay(0,"Timer fired early"); } touch_start(integer n) { llTargetOmega(<0,0,1>,2.5,0); llSetObjectName("Scripted Prim"); llSetObjectDesc("Edited by LSL"); llSetText("Script label", <1.2,.5,-1>, .5); llSetPayPrice(PAY_DEFAULT, [5, 10]); llCollisionSound("12345678-1234-4234-8234-123456789abc", .625); llSetSoundRadius(31.5); llSetSoundQueueing(1); llPreloadSound("12345678-1234-4234-8234-123456789abc"); llTriggerSound("12345678-1234-4234-8234-123456789abc", .625); llTriggerSoundLimited("22345678-1234-4234-8234-123456789abc", 1, <150,150,80>, <100,100,0>); llTriggerSoundLimited("32345678-1234-4234-8234-123456789abc", 1, <10,10,10>, <0,0,0>); llPlaySound("12345678-1234-4234-8234-123456789abc", 1.5); llPlaySoundSlave("12345678-1234-4234-8234-123456789abc", .25); llLoopSoundSlave("12345678-1234-4234-8234-123456789abc", .5); llSleep(.1); llLoopSoundMaster("12345678-1234-4234-8234-123456789abc", .75); llAdjustSoundVolume(1.5); llSetTouchText("Activate extra"); llSetSitText("Take seat extra"); llSetScale(<2,300,4>); llSetClickAction(2); llSay(0,"Touched."); llSay(0,"Touch avatar " + (string)llDetectedKey(0) + " face " + (string)llDetectedTouchFace(0) + " size " + (string)llGetAgentSize(llDetectedKey(0)) + " language " + llGetAgentLanguage(llDetectedKey(0)) + "/" + llGetAgentLanguage("17495d6b-53c5-4ca7-a3fa-801ff116a0e1")); llSay(0,"Surface " + (string)llDetectedTouchPos(0) + (string)llDetectedTouchNormal(0) + (string)llDetectedTouchBinormal(0) + (string)llDetectedTouchST(0) + (string)llDetectedTouchUV(0)); } touch_end(integer n) { llStopSound(); } changed(integer mask) { llSay(0,"Changed " + (string)mask); } }', encoding='utf-8')
    script=fixture.read_text(encoding='utf-8')
    script = 'integer seat_phase;\n' + script.replace('changed(integer mask) { llSay(0,"Changed " + (string)mask); }', '''
      changed(integer mask) {
          llSay(0, "Changed " + (string)mask);
          if (mask & CHANGED_LINK) {
              key sitter = llAvatarOnSitTarget();
              if (sitter) {
                  if (sitter == llAvatarOnLinkSitTarget(LINK_ROOT) &&
                      llAvatarOnLinkSitTarget(0) == NULL_KEY &&
                      sitter == llGetLinkKey(2) && sitter == llGetObjectLinkKey(llGetKey(), 2) &&
                      llGetObjectPrimCount(llGetKey()) == 1 && llGetNumberOfPrims() == 2 &&
                      llGetLinkName(2) == "Native Viewer" && llGetAnimation(sitter) == "Sitting" &&
                      osGetSitActiveRange() == 128 && osGetLinkSitActiveRange(LINK_ROOT) == 128 &&
                      osGetLinkSitActiveRange(LINK_SET) == -2147483648.0 &&
                      osGetStandTarget() == <0,0,1.75> &&
                      osGetLinkStandTarget(LINK_ROOT) == <0,0,1.75> &&
                      osGetLinkStandTarget(LINK_SET) == ZERO_VECTOR &&
                      llGetLinkSitFlags(LINK_THIS) == (SIT_FLAG_OPENSIMFORCED | SIT_FLAG_SIT_TARGET) &&
                      llGetLinkSitFlags(LINK_SET) == 0) {
                      seat_phase = 1;
                      llSay(0, "Seated link " + (string)sitter);
                  }
              } else if (seat_phase == 2 && llGetNumberOfPrims() == 2 && llGetLinkKey(2)) {
                  seat_phase = 3;
                  llSay(0, "Surface sitter " + (string)llGetLinkKey(2));
              } else if (llGetNumberOfPrims() == 1 && llGetLinkKey(2) == NULL_KEY) {
                  if (seat_phase == 1) {
                      seat_phase = 2;
                      llSitTarget(ZERO_VECTOR, ZERO_ROTATION);
                      if (llGetLinkSitFlags(0) == SIT_FLAG_OPENSIMFORCED)
                          llSay(0, "Seated link cleared");
                  } else if (seat_phase == 3) {
                      seat_phase = 0;
                      llSitTarget(<.5,1.25,2>, ZERO_ROTATION);
                      llSay(0, "Surface sitter cleared");
                  }
              }
          }
      }
    ''')
    script = script.replace('touch_start(integer n) { llTargetOmega',
                            'touch_start(integer n) { key sitter = llAvatarOnSitTarget(); '
                            'if (sitter) { llUnSit(sitter); return; } '
                            'llAllowInventoryDrop(-1); llTargetOmega')
    script = script.replace('llSay(0,fixtureName);',
                            'llPassTouches(-1); llSitTarget(<.5,1.25,2>, <0,0,0,0>); '
                            'osSetSitActiveRange(64); osSetLinkSitActiveRange(LINK_SET, 256); '
                            'osSetStandTarget(<0,0,1>); osSetLinkStandTarget(LINK_ROOT, <0,0,1.75>); '
                            'llSetCameraEyeOffset(<1,2,3>); llSetCameraAtOffset(<4,5,6>); '
                            'llSetLinkCamera(LINK_ROOT, <2,3,4>, <5,6,7>); llForceMouselook(TRUE); '
                            'llSetLinkSitFlags(LINK_ROOT, SIT_FLAG_SCRIPTED_ONLY); '
                            'llSay(0,fixtureName);')
    script = script.replace('llSay(0,"Surface "',
        'llSay(0, "Detector " + llDetectedName(0) + "|" + (string)llDetectedOwner(0) + "|" + '
        '(string)(llDetectedType(0) & 1) + "|" + (string)llDetectedGrab(0)); llSay(0,"Surface "')
    script = script.replace('llSay(0,fixtureName);', '''
      llSay(0, fixtureName);
      osRequestURL(["ignored", "allowXss"]);
      osRequestURL(["allowXss"]);
      llSay(0, "Empty linkset capacity " + (string)llLinksetDataAvailable());
      llSay(0, "Land owners " + (string)llGetLandOwnerAt(<132,128,1000>) + " " +
          (string)llGetLandOwnerAt(<-.5,128,25>) + " " + (string)llGetLandOwnerAt(<-1,128,25>));
      llSay(0, "Over land " + (string)llOverMyLand(llGetOwner()) + " " +
          (string)llOverMyLand(llGetKey()) + " " + (string)llOverMyLand(NULL_KEY) + " " +
          (string)llOverMyLand("invalid key"));
      llSay(0, "Identity " + llKey2Name(llGetOwner()) + "|" + llGetDisplayName(llGetOwner()) + "|" +
          llGetUsername(llGetOwner()) + "|" + (string)llName2Key("  nAtIvE.vIeWeR  "));
      llSay(0, "Identity edges " + (string)(llKey2Name(llGetKey()) == llGetObjectName()) + "|" +
          llGetDisplayName(llGetKey()) + "|" + llKey2Name("invalid") + "|" +
          (string)llName2Key("Native"));
      avatar_name_query = llRequestUsername(llGetOwner());
      display_name_query = llRequestDisplayName(llGetOwner());
      prim_name_query = llRequestUsername(llGetKey());
      account_name_query = llRequestDisplayName("OFFLINE_ACCOUNT_ID");
      unknown_name_query = llRequestDisplayName("7bc429bc-360b-47e3-8877-66d722174e35");
      live_key_query = llRequestUserKey("nAtIvE.vIeWeR");
      local_key_query = llRequestUserKey("Privacy Observer");
      missing_key_query = llRequestUserKey("Missing Resident");
      foreign_key_query = llRequestUserKey("Foreign.Avatar @127.0.0.1:FOREIGN_KEY_PORT");
      cached_key_query = llRequestUserKey("foreign.avatar @127.0.0.1:FOREIGN_KEY_PORT");
      online_data_query = llRequestAgentData(llGetOwner(), DATA_ONLINE);
      name_data_query = llRequestAgentData(llGetOwner(), DATA_NAME);
      rating_data_query = llRequestAgentData(llGetOwner(), DATA_RATING);
      maturity_query = llRequestSimulatorData(llGetRegionName(), DATA_SIM_RATING);
      llSay(0, "Invalid requests " + (string)(llRequestUsername(NULL_KEY) == "") + " " +
          (string)(llRequestDisplayName("invalid") == "") + " " +
          (string)(llRequestUserKey("Invalid") == NULL_KEY));
    '''.replace('FOREIGN_KEY_PORT', str(key_server.server_address[1])))
    script = '''
      key avatar_name_query;
      key display_name_query;
      key prim_name_query;
      key account_name_query;
      key unknown_name_query;
      key live_key_query;
      key local_key_query;
      key missing_key_query;
      key foreign_key_query;
      key cached_key_query;
      key online_data_query;
      key name_data_query;
      key rating_data_query;
      key maturity_query;
    ''' + script.rsplit('}', 1)[0] + '''
      dataserver(key query, string data) {
          if (query == avatar_name_query) llSay(0, "Async username " + data);
          if (query == display_name_query) llSay(0, "Async display " + data);
          if (query == prim_name_query) llSay(0, "Async prim " + (string)(data == llGetUsername(llGetKey())));
          if (query == account_name_query) llSay(0, "Async account " + data);
          if (query == unknown_name_query) llSay(0, "Async unknown [" + data + "]");
          if (query == live_key_query) llSay(0, "Async live key " + (string)((key)data == llGetOwner()));
          if (query == local_key_query) llSay(0, "Async local key " + (string)(data == "OFFLINE_ACCOUNT_ID"));
          if (query == missing_key_query) llSay(0, "Async missing key " + (string)((key)data == NULL_KEY));
          if (query == foreign_key_query) llSay(0, "Async foreign key " + data);
          if (query == cached_key_query) llSay(0, "Async cached key " + data);
          if (query == online_data_query) llSay(0, "Agent online " + data);
          if (query == name_data_query) llSay(0, "Agent name " + data);
          if (query == rating_data_query) llSay(0, "Agent rating " + data);
          if (query == maturity_query) llSay(0, "Saved maturity " + data);
      }
    }
    '''
    script = script.rsplit('}', 1)[0] + '''
      http_request(key id, string method, string body) {
          if (method == URL_REQUEST_GRANTED)
              llOwnerSay("Script HTTP URL " + body);
          if (method == URL_REQUEST_DENIED)
              llOwnerSay("Script HTTP denied " + body + "|" + (string)llGetFreeURLs());
          if (method == "PROPFIND" || method == "PURGE" || method == "TRACE" ||
              method == "CONNECT" || method == "PRI")
              llHTTPResponse(id, 202, method + "|" + body);
          if (method == "POST") {
              if (body == "free") {
                  llHTTPResponse(id, 200, (string)llGetFreeURLs());
                  return;
              }
              if (body == "timeout")
                  return;
              if (body == "release") {
                  llReleaseURL(llGetHTTPHeader(id, "x-script-url") + "///");
                  return;
              }
              if (body == "headers") {
                  llHTTPResponse(id, 200, llGetHTTPHeader(id, "host") + "|" +
                      llGetHTTPHeader(id, "connection") + "|" + llGetHTTPHeader(id, "content-length") + "|" +
                      llGetHTTPHeader(id, "accept") + "|" + llGetHTTPHeader(id, "x-requested-with") + "|" +
                      llGetHTTPHeader(id, "http_x_requested_with") + "|" + llGetHTTPHeader(id, "remote_addr") + "|" +
                      llGetHTTPHeader(id, "forwarded") + "|" + llGetHTTPHeader(id, "X-Forwarded-For") + "|" +
                      llGetHTTPHeader(id, "X-Test"));
                  return;
              }
              if (body == "html" || llGetHTTPHeader(id, "x-test") == "legacy")
                  llSetContentType(id, CONTENT_TYPE_HTML);
              else
                  llSetContentType(id, CONTENT_TYPE_JSON);
              llHTTPResponse(id, 201, body + "|" + llGetHTTPHeader(id, "X-Test") + "|" +
                  llGetHTTPHeader(id, "x-path-info") + "|" + llGetHTTPHeader(id, "x-query-string"));
          }
      }
    }
    '''
    if a.script_https:
     script = script.replace('osRequestURL(["allowXss"]);', 'osRequestSecureURL(["allowXss"]);')
    if a.script_url_quota:
     script = 'key released_url;\n' + script.replace(
         'osRequestURL(["ignored", "allowXss"]);',
         'released_url = llRequestURL();'
         ' if (llGetScriptState(llGetScriptName())) llResetOtherScript(llGetScriptName());'
         ' osRequestURL(["ignored", "allowXss"]);')
     script = script.replace('if (method == URL_REQUEST_GRANTED)',
                             'if (method == URL_REQUEST_GRANTED && id == released_url) {'
                             ' llOwnerSay("Script reset URL " + body); return; }'
                             ' if (method == URL_REQUEST_GRANTED)')
     script = script.replace('osRequestURL(["allowXss"]);',
                             'osRequestURL(["allowXss"]); osRequestURL([]);')
     script = script.replace('Script HTTP URL ', 'Script HTTP quota URL ')
     script = script.replace('Script HTTP denied ', 'Script HTTP quota denied ')
    fixture.write_text(script, encoding='utf-8')
    script_host = '0.0.0.0' if a.script_url_denied else 'localhost' if a.script_url_hostname else '127.0.0.1'
    config.write_text(config.read_text(encoding='utf-8').replace(
        '[Network]', f'[Network]\nExternalHostNameForLSL = {script_host}\nshard = Native Fixture\n'
                     'user_agent = Native Fixture Agent'), encoding='utf-8')
    with config.open('a', encoding='utf-8') as settings:
     settings.write('\n[Startup]\nLegacySitOffsets = false\n')
     settings.write('\n[YEngine]\nMinTimerInterval = 2\nLinksetDataLimit = 4096\nAutomaticLinkPermission = true\n')
     settings.write('\n[OSSL]\nAllow_osRequestURL = true\n')
     if a.pbr_terrain:
      settings.write('Allow_osSetRegionSunSettings = true\n')
     if a.script_https:
      settings.write('Allow_osRequestSecureURL = true\n')
     if a.script_url_quota:
      settings.write('\n[LL-Functions]\nmax_external_urls_per_simulator = 2\n')
    if a.script_https:
     tls_fixture = Path(__file__).resolve().parent / 'fixtures'
     settings = config.read_text(encoding='utf-8')
     if a.primary_https:
      settings = settings.replace('ExternalHost = 127.0.0.1', 'ExternalHost = localhost')
      settings = settings.replace('[Network]',
          '[Network]\nhttp_listener_ssl = true\nhttp_listener_sslport = 0\nhttp_listener_cn = localhost\n'
          f'http_listener_cert_path = {tls_fixture / "localhost-script-test.p12"}\n'
          'http_listener_cert_pass = opensim-test\nhttps_listener = true\nhttps_port = 0\n'
          f'cert_path = {tls_fixture / "localhost-script-test.p12"}\ncert_pass = opensim-test')
     else:
      settings = settings.replace('[Network]', '[Network]\nhttps_listener = true\nhttps_port = 0')
     if a.script_https_external:
      settings = settings.replace('[Network]', '[Network]\nhttps_external = true')
     elif not a.primary_https:
      settings = settings.replace('[Network]',
          f'[Network]\ncert_path = {tls_fixture / "localhost-script-test.p12"}\n'
          'cert_pass = opensim-test')
     config.write_text(settings, encoding='utf-8')
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native', 'Name = Connected Native\nDefaultScriptFile = '+str(fixture))+
                     '\n[Sounds]\nMaxDistance = 72\n[Economy]\nPriceUpload = 9\nEnergyEfficiency = .75\n',encoding='utf-8')
  if a.physics_library:
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native', 'Name = Connected Native\nPhysics = ubODE\nPhysicsLibrary = '+str(a.physics_library.resolve())),encoding='utf-8')
  if a.advanced_profiles:
   with socket.socket() as main_port_guard, socket.socket() as listener:
    main_port_guard.bind(('127.0.0.1',profile_port))
    listener.bind(('127.0.0.1',0));offline_port=listener.getsockname()[1]
   with config.open('a',encoding='utf-8') as settings:
    settings.write(f'\n[Const]\nPrivatePort={offline_port}\n'
                   '\n[ServiceList]\nPrivateMailbox=MailboxAuth@${Const|PrivatePort}/OpenSim.Addons.OfflineIM.dll:OfflineIMServiceRobustConnector\n'
                   '\n[MailboxAuth]\nAuthType=BasicHttpAuthentication\nHttpAuthUsername=mailbox\nHttpAuthPassword=fixture\n')
  if a.presence_provider:
   settings = config.read_text(encoding='utf-8')
   settings = settings.replace('[NativeServer]', '[NativeServer]\nEnablePresenceEndpoint = true', 1)
   settings += (f'\n[PresenceService]\nStorageProvider = OpenSim.Data.{a.presence_provider}.dll\n'
                f'ConnectionString = "{presence_connection}"\n')
   config.write_text(settings, encoding='utf-8')
  if a.grid_user_provider:
   settings = config.read_text(encoding='utf-8')
   settings = settings.replace('[NativeServer]', '[NativeServer]\nEnableGridUserEndpoint = true', 1)
   settings += (f'\n[GridUserService]\nStorageProvider = OpenSim.Data.{a.grid_user_provider}.dll\n'
                f'ConnectionString = "{grid_user_connection}"\n')
   config.write_text(settings, encoding='utf-8')
  cmd=[str(Path(a.native).resolve()),'--config',str(config)]
  if a.raw_fps or rotating_prim:
   settings=config.read_text(encoding='utf-8').replace('EnableLocalRegion = true',
    'EnableLocalRegion = true\nEnableAgentPreferencesService = true\nAgentPreferencesDatabase = preferences.db')
   config.write_text(settings,encoding='utf-8')
  if a.raw_fps:
   if not a.remote_mutes:
    settings = config.read_text(encoding='utf-8').replace(
        'Name = Connected Native', 'Name = Connected Native\nOwnerID = 8f677659-aecd-4a0c-bfa2-8b8953a18aba')
    config.write_text(settings, encoding='utf-8')
   with config.open('a',encoding='utf-8') as settings:
    settings.write('\n[Startup]\nNormalized55FPS = false\n\n[Messaging]\nMuteListModule=MuteListModule\n')
    if not a.remote_mutes:
     settings.write('\n[Modules]\nMuteListService=LocalMuteListServicesConnector\n'
                    '\n[MuteListService]\nLocalServiceModule=OpenSim.Services.MuteListService.dll:MuteListService\n'
                    '\n[MuteListStore]\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=mutes.db\n')
  if a.remote_mutes:
   remote_calls = []
   remote_entries = {}
   class RemoteMuteService(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
     if self.path != '/mutelist' or self.headers.get('Authorization') != 'Basic bXV0ZTpmaXh0dXJl':
      self.send_error(403)
      return
     body = self.rfile.read(int(self.headers['Content-Length'])).decode()
     fields = dict(urllib.parse.parse_qsl(body))
     method, agent = fields['METHOD'], fields['agentid']
     remote_calls.append(fields)
     if method == 'update':
      remote_entries[agent] = (f"{fields['mutetype']} {fields['muteid']} "
                               f"{fields['mutename']}|{fields['muteflags']}\n").encode()
      value = 'Success'
     elif method == 'delete':
      remote_entries.pop(agent)
      value = 'Success'
     else:
      data = remote_entries.get(agent, b'')
      if data and zlib.crc32(data) == int(fields['mutecrc']):
       data = b'\x01'
      value = base64.b64encode(data).decode()
     reply = f'<ServerResponse><result>{value}</result></ServerResponse>'.encode()
     self.send_response(200)
     self.send_header('Content-Type','text/xml')
     self.send_header('Content-Length',str(len(reply)))
     self.end_headers()
     self.wfile.write(reply)
    def log_message(self, *_):
     pass
   mute_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), RemoteMuteService)
   cleanup.callback(mute_server.server_close)
   threading.Thread(target=mute_server.serve_forever, daemon=True).start()
   cleanup.callback(mute_server.shutdown)
   with config.open('a',encoding='utf-8') as settings:
    settings.write('\n[Modules]\nMuteListService=RemoteMuteListServicesConnector\n'
                   f'\n[MuteListService]\nMuteListServerURI=http://127.0.0.1:{mute_server.server_port}\n'
                   'AuthType=BasicHttpAuthentication\nHttpAuthUsername=mute\nHttpAuthPassword=fixture\n')
  setup=[(['--init-db'],None),(['--create-user','Native','Viewer'],'viewer-test-password\n')]
  if a.physics_library:
   setup.append((['--create-user','Physics','Observer'],'viewer-test-password\n'))
  if a.touch and not a.physics_library and not a.disable_scripts:
   for last in ('Observer','Mover','BanTarget','HomeTarget','CancelTarget','TimeoutTarget'):
    setup.append((['--create-user','Privacy',last],'viewer-test-password\n'))
  if a.advanced_profiles:
   setup.extend([(['--create-user','Remote','Recipient'],'viewer-test-password\n'),
                 (['--create-user','Offline','Viewer'],'viewer-test-password\n')])
  for args,password in setup:
   r=subprocess.run(cmd+args,input=password,text=True,capture_output=True,timeout=30);check(r.returncode==0,'CLI '+args[0]+': '+r.stderr)
  if notecard_probe:
   text = b'alpha\n\n' + b'x' * 300 + b'\n'
   card = (b'Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\n'
           + f'Text length {len(text)}\n'.encode() + text + b'}\n')
   with contextlib.closing(sqlite3.connect(root/'assets.db')) as database, database:
    database.execute("CREATE TABLE IF NOT EXISTS assets(UUID NOT NULL PRIMARY KEY,Name,Description,Type,Local,"
                     "Temporary,asset_flags INTEGER NOT NULL DEFAULT 0,CreatorID varchar(128) default '',Data)")
    database.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) '
                     'VALUES(?,?,?,?,?,?,?,?,?)', (notecard_id, 'Script fixture', '', 7, 0, 0, 0, '', card))
  if a.touch and not a.physics_library and not a.disable_scripts:
   with contextlib.closing(sqlite3.connect(root/'users.db')) as database, database:
    if not a.telehub_parcel_access and not a.os_landing:
     database.execute("UPDATE UserAccounts SET UserLevel=200 WHERE FirstName='Privacy' AND LastName='Observer'")
    parcel_observer = database.execute(
        "SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='Observer'").fetchone()[0]
    if rotating_prim:
     fixture.write_text(fixture.read_text(encoding='utf-8').replace('OFFLINE_ACCOUNT_ID', parcel_observer),
                        encoding='utf-8')
    if a.parcel_profile_access:
     database.execute('UPDATE UserAccounts SET UserFlags=4 WHERE PrincipalID=?', (parcel_observer,))
     database.execute('UPDATE UserAccounts SET UserLevel=0 WHERE PrincipalID=?', (parcel_observer,))
    manager=database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='Mover'").fetchone()[0]
    owner=database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Native' AND LastName='Viewer'").fetchone()[0]
    ban_target=uuid.UUID(database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='BanTarget'").fetchone()[0])
    home_target=uuid.UUID(database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='HomeTarget'").fetchone()[0])
    cancel_target=uuid.UUID(database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='CancelTarget'").fetchone()[0])
    timeout_target=uuid.UUID(database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Privacy' AND LastName='TimeoutTarget'").fetchone()[0])
    if a.estate_profile_access:
     database.executemany('UPDATE UserAccounts SET UserFlags=? WHERE PrincipalID=?',
                          [(36, parcel_observer), (4, str(ban_target)), (32, str(home_target))])
   with contextlib.closing(sqlite3.connect(root/'gridusers.db')) as database, database:
    database.executemany('INSERT INTO GridUser (UserID,HomeRegionID,HomePosition,HomeLookAt) VALUES (?,?,?,?)',
                         ((str(target),destination_id,'<119, 125, 42>','<0, 1, 0>') for target in (home_target,cancel_target,timeout_target)))
    if a.os_landing:
     database.executemany('INSERT INTO GridUser (UserID,HomeRegionID,HomePosition,HomeLookAt) VALUES (?,?,?,?)',
                          ((agent,'11111111-1111-4111-8111-111111111111',position,'<0, 1, 0>')
                           for agent,position in ((parcel_observer,'<140, 144, 25>'),(manager,'<146, 148, 26>'))))
   with contextlib.closing(sqlite3.connect(root/'estate.db')) as database, database:
    database.execute('CREATE TABLE estate_settings (EstateID INTEGER PRIMARY KEY, EstateOwner VARCHAR(36), BillableFactor FLOAT, ParentEstateID INT, PricePerMeter INT, RedirectGridX INT, RedirectGridY INT)')
    database.execute('CREATE TABLE estate_map (RegionID CHAR(36) UNIQUE, EstateID INT NOT NULL)')
    database.execute('CREATE TABLE estate_managers (EstateID INT NOT NULL, uuid CHAR(36) NOT NULL)')
    database.execute('INSERT INTO estate_settings VALUES (100,?,0.75,1,9,1024,2048)',(owner,))
    database.execute("ALTER TABLE estate_settings ADD COLUMN EstateName TEXT DEFAULT 'Fixture Estate'")
    for column,value in (('TaxFree',1),('AllowVoice',0),('DenyMinors',1),('DenyAnonymous',1),
                         ('AllowEnviromentOverride',1)):
     database.execute(f'ALTER TABLE estate_settings ADD COLUMN {column} INTEGER NOT NULL DEFAULT {value}')
    if a.telehub_parcel_access:
     database.execute('UPDATE estate_settings SET TaxFree=0')
     database.execute('ALTER TABLE estate_settings ADD COLUMN AllowDirectTeleport INTEGER NOT NULL DEFAULT 0')
     if a.parcel_relocation:
      database.execute('UPDATE estate_settings SET AllowDirectTeleport=1')
    if a.parcel_profile_access:
     enabled = int(a.estate_profile_access)
     database.execute('UPDATE estate_settings SET DenyMinors=?, DenyAnonymous=?', (enabled, enabled))
    database.execute("INSERT INTO estate_map VALUES ('11111111-1111-4111-8111-111111111111',100)")
    database.executemany('INSERT INTO estate_map VALUES (?,100)',((destination_id,),(estate_peer_alias,)))
    database.execute('INSERT INTO estate_managers VALUES (100,?)',(manager,))
    database.execute('CREATE TABLE estate_users (EstateID INT NOT NULL, uuid CHAR(36) NOT NULL)')
    database.execute('CREATE TABLE estate_groups (EstateID INT NOT NULL, uuid CHAR(36) NOT NULL)')
    database.execute('CREATE TABLE estateban (EstateID INT,bannedUUID TEXT,bannedIp TEXT,bannedIpHostMask TEXT,bannedNameMask TEXT,banningUUID TEXT,banTime INT)')
    allowed=[uuid.UUID(int=number) for number in range(1,65)]
    database.executemany('INSERT INTO estate_users VALUES (100,?)',((str(agent),) for agent in allowed))
   with config.open('a',encoding='utf-8') as settings:
    settings.write('\n[Permissions]\nallow_grid_gods = true\nregion_manager_is_god = true\nimplicit_gods = true\n'
                   '\n[Messaging]\nMessageKey=parcel-moderation-fixture\n'
                   '\n[Estates]\nToken=estate-covenant-fixture\n'
                   '\n[Groups]\nEnabled=true\nServicesConnectorModule=Groups Remote Service Connector\n'
                   f'GroupsServerURI=http://127.0.0.1:{grid_features.server_port}\n'
                   'AuthType=BasicHttpAuthentication\nHttpAuthUsername=grid\nHttpAuthPassword=fixture\n')
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native',
                     'Name = Connected Native\nEstateDatabase = estate.db'),encoding='utf-8')
   if a.public_terrain:
    config.write_text(config.read_text(encoding='utf-8').replace(
        'region_manager_is_god = true', 'region_manager_is_god = false'), encoding='utf-8')
   with config.open('a', encoding='utf-8') as output:
    output.write('\n[EstateManagement]\n')
    if a.parcel_profile_access:
     output.write('IgnoreEstateMinorAccessControl=false\nIgnoreEstatePaymentAccessControl=false\n')
  if a.touch:
   with contextlib.closing(sqlite3.connect(root/'users.db')) as database:
    principal=database.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Native' AND LastName='Viewer'").fetchone()[0]
   config.write_text(config.read_text(encoding='utf-8').replace('Name = Connected Native',
                     'Name = Connected Native\nTerrainDatabase = terrain.db\nOwnerID = '+principal),encoding='utf-8')
   if a.varregion_relocation or a.extended_pbr_terrain:
    config.write_text(config.read_text(encoding='utf-8').replace(
        'TerrainDatabase = terrain.db', 'TerrainDatabase = terrain.db\nSizeX = 512\nSizeY = 512'),
        encoding='utf-8')
   if a.sl_landing:
    with config.open('a', encoding='utf-8') as output:
     output.write('\n[EntityTransfer]\nLandingPointBehavior=LandingPointBehavior_SL\n')
   if not a.physics_library:
    # A missing library makes accidental physics startup fail immediately.
    config.write_text(config.read_text(encoding='utf-8').replace('TerrainDatabase = terrain.db',
                      'TerrainDatabase = terrain.db\nPhysics = ubODE\nPhysicsLibrary = deliberately-missing-ubode'),encoding='utf-8')
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
     database.execute('CREATE TABLE land (UUID TEXT PRIMARY KEY, RegionUUID TEXT, LocalLandID INTEGER, OwnerUUID TEXT, Bitmap BLOB, LandFlags TEXT, Name TEXT, Desc TEXT, GroupUUID TEXT, ClaimDate INTEGER, LandingType TEXT, UserLocationX REAL, UserLocationY REAL, UserLocationZ REAL, UserLookAtX REAL, UserLookAtY REAL, UserLookAtZ REAL, SeeAVs BOOLEAN, Dwell INTEGER)')
     # Split at x=132, between the viewer and the scripted prim.
     for local_id, row in enumerate((bytes([255]*4+[1]+[0]*3), bytes([0]*4+[254]+[255]*3)), 1):
      database.execute('INSERT INTO land VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
                       (f'00000000-0000-4000-8000-{local_id:012d}',
                        '11111111-1111-4111-8111-111111111111',local_id,principal,row*64,str(0x08000042 if local_id==1 else 32),f"Parcel {local_id}",
                        "Fixture parcel","00000000-0000-4000-8000-000000000003",1700000000+local_id,"1",132.5,128.25,25.,0.,1.,0.,local_id==1,40+local_id))
     database.execute('CREATE TABLE terrain (RegionUUID varchar(255), Revision integer, Heightfield blob)')
     if a.pbr_terrain:
      database.execute('ALTER TABLE land ADD COLUMN environment TEXT')
      database.execute('UPDATE land SET OwnerUUID=?, environment=? WHERE LocalLandID=2',
                       (manager, "<? llsd/notation ?>\n{'day_length':i18000,'day_offset':i1200}"))
     if a.disable_scripts:
      database.execute('ALTER TABLE land ADD COLUMN AnyAVSounds BOOLEAN NOT NULL DEFAULT FALSE')
      database.execute('ALTER TABLE land ADD COLUMN GroupAVSounds BOOLEAN NOT NULL DEFAULT FALSE')
     else:
      database.execute('ALTER TABLE land ADD COLUMN AnyAVSounds BOOLEAN NOT NULL DEFAULT TRUE')
      database.execute('ALTER TABLE land ADD COLUMN GroupAVSounds BOOLEAN NOT NULL DEFAULT TRUE')
      database.execute('UPDATE land SET AnyAVSounds=FALSE WHERE LocalLandID=2')
     prim_columns=('UUID SceneGroupID RegionUUID Name Description OwnerID GroupID LastOwnerID CreatorID '
                   'CreationDate ObjectFlags BaseMask OwnerMask GroupMask EveryoneMask NextOwnerMask '
                   'SaleType SalePrice Material ClickAction GroupPositionX GroupPositionY GroupPositionZ '
                   'PositionX PositionY PositionZ RotationX RotationY RotationZ RotationW VelocityX Friction Restitution '
                   'Text ColorA ColorR ColorG ColorB TouchName SitName MediaURL LoopedSound LoopedSoundGain '
                   'CollisionSound CollisionSoundVolume PayPrice PayButton1 PayButton2 PayButton3 PayButton4').split()
     database.execute('CREATE TABLE prims ('+','.join(prim_columns)+')')
     stored_root='eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee'
     stored_child='dddddddd-dddd-4ddd-8ddd-dddddddddddd'
     for part in (stored_child,stored_root):
      database.execute('INSERT INTO prims VALUES ('+','.join('?' for _ in prim_columns)+')',
                       (part,stored_root,'11111111-1111-4111-8111-111111111111','Stored part','',
                        principal,'00000000-0000-0000-0000-000000000000',principal,principal,
                        1700000000,0,0x9e000,0x9e000,0,0,0x82000,0,0,3,0,-10,128,25,1,2,3,0,0,0,1,1.,.6,.5,
                        'Stored label',128,16,32,64,'','','https://example.invalid/prim-media',
                        '12345678-1234-4234-8234-123456789abc',.5,'00000000-0000-0000-0000-000000000000',0.,
                        -2,-2,-2,-2,-2))
     shape_columns=('UUID PCode State ScaleX ScaleY ScaleZ PathBegin PathEnd ProfileBegin ProfileEnd '
                    'ProfileHollow PathScaleX PathScaleY PathShearX PathShearY PathCurve PathRevolutions '
                    'ProfileCurve PathSkew PathRadiusOffset PathTaperX PathTaperY PathTwist PathTwistBegin Texture ExtraParams').split()
     database.execute('CREATE TABLE primshapes ('+','.join(shape_columns)+')')
     database.execute('INSERT INTO primshapes VALUES ('+','.join('?' for _ in shape_columns)+')',
                      (stored_child,9,0,1.,2.,3.,0,0,0,0,1500,100,100,0,0,16,0,1,0,0,0,0,-12,0,b'',
                       bytes([1,32,0,16,0,0,0,255,128,64,255])+struct.pack('<fff',5.,.75,1.)))
     item_columns=('itemID primID parentFolderID assetID ownerID lastOwnerID groupID creatorID '
                   'name description assetType invType creationDate flags nextPermissions currentPermissions '
                   'basePermissions everyonePermissions groupPermissions').split()
     database.execute('CREATE TABLE primitems ('+','.join(item_columns)+')')
     database.execute('INSERT INTO primitems VALUES ('+','.join('?' for _ in item_columns)+')',
                      ('cccccccc-cccc-4ccc-8ccc-cccccccccccc',stored_child,stored_child,
                       'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb',principal,principal,
                       '00000000-0000-0000-0000-000000000000',principal,'Stored note','',7,7,
                       1700000000,0,0x82000,0x9e000,0x9e000,0,0))
     stored_script='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
     script_part = stored_root if a.script_parcel_denied else stored_child
     script_owner = parcel_observer if a.script_parcel_denied else principal
     if a.script_parcel_denied:
      database.execute('UPDATE prims SET OwnerID=? WHERE SceneGroupID=?', (script_owner, stored_root))
     database.execute('INSERT INTO primitems VALUES ('+','.join('?' for _ in item_columns)+')',
                      ('99999999-9999-4999-8999-999999999999',script_part,script_part,
                       stored_script,script_owner,script_owner,'00000000-0000-0000-0000-000000000000',
                       principal,'Stored startup script','',10,10,1700000000,0,0x82000,0x9e000,0x9e000,0,0))
     if not a.disable_scripts:
      temporary_prim='77777777-7777-4777-8777-777777777777'
      saved_prim=list(database.execute('SELECT * FROM prims WHERE UUID=?',(stored_root,)).fetchone())
      saved_prim[0]=saved_prim[1]=temporary_prim
      saved_prim[prim_columns.index('ObjectFlags')]=0
      database.execute('INSERT INTO prims VALUES ('+','.join('?' for _ in prim_columns)+')',saved_prim)
      saved_shape=list(database.execute('SELECT * FROM primshapes WHERE UUID=?',(stored_child,)).fetchone())
      saved_shape[0]=temporary_prim
      database.execute('INSERT INTO primshapes VALUES ('+','.join('?' for _ in shape_columns)+')',saved_shape)
      saved_item=list(database.execute('SELECT * FROM primitems WHERE name=?',('Stored note',)).fetchone())
      saved_item[0]='66666666-6666-4666-8666-666666666666'
      saved_item[1]=saved_item[2]=temporary_prim
      database.execute('INSERT INTO primitems VALUES ('+','.join('?' for _ in item_columns)+')',saved_item)
      database.execute('ALTER TABLE prims ADD COLUMN lnkstBinData BLOB')
      database.execute('ALTER TABLE prims ADD COLUMN StartStr TEXT')
      database.execute('ALTER TABLE prims ADD COLUMN PassTouches BOOLEAN NOT NULL DEFAULT FALSE')
      database.execute('ALTER TABLE prims ADD COLUMN AllowedDrop INTEGER NOT NULL DEFAULT 0')
      for column in ('OffsetX','OffsetY','OffsetZ','OrientX','OrientY','OrientZ','OrientW'):
       database.execute(f'ALTER TABLE prims ADD COLUMN SitTarget{column} REAL')
      database.execute('ALTER TABLE prims ADD COLUMN sitactrange REAL')
      for axis in 'xyz':
       database.execute(f'ALTER TABLE prims ADD COLUMN standtarget{axis} REAL')
      for point in ('Eye','At'):
       for axis in 'XYZ':
        database.execute(f'ALTER TABLE prims ADD COLUMN Camera{point}Offset{axis} REAL')
      database.execute('ALTER TABLE prims ADD COLUMN ForceMouselook INTEGER')
     with contextlib.closing(sqlite3.connect(root/'assets.db')) as asset_db, asset_db:
      asset_db.execute("CREATE TABLE IF NOT EXISTS assets(UUID NOT NULL PRIMARY KEY,Name,Description,Type,Local,"
                       "Temporary,asset_flags INTEGER NOT NULL DEFAULT 0,CreatorID varchar(128) default '',Data)")
      startup_script = b'default { state_entry() { llSetObjectName("Restored script executed"); llCollisionSound("", -1); } }'
      if a.pbr_terrain:
       asset_db.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) '
                        'VALUES(?,?,?,?,?,?,?,?,?)',
                        ('390d83cf-91d3-4f76-9483-4ae3af6198f2', 'Sparse water', '', 56, 0, 0, 0, principal,
                         b"<? llsd/notation ?>\n{'type':'water','name':'Ignored asset name',"
                         b"'blur_multiplier':'0.125','wave1_direction':'<3, 4>',"
                         b"'normal_scale':[r1,r2,r3,r4],'extra':i1}"))
       asset_db.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) '
                        'VALUES(?,?,?,?,?,?,?,?,?)',
                        ('ff98b84a-cacf-459d-bf91-9e4498d68619', 'Sparse sky', '', 56, 0, 0, 0, principal,
                         b"<? llsd/notation ?>\n{'type':'sky','cloud_scale':'0.125',"
                         b"'sunlight_color':[r1,r0.5,r0.25],'reflection_probe_ambiance':r0.75,"
                         b"'legacy_haze':{'ambient':[r0.25,r0.5,r1]},"
                         b"'absorption_config':[{'width':r4},{'width':r8}],'extra':i1}"))
       startup_script = b'''default {
           state_entry() {
               if (osGetSunParam("DAY_LENGTH") != 36000 || osGetApparentRegionTime() <= 0)
                   return;
               if (llGetDayLength() != 36000 || llGetRegionDayLength() != 36000
                   || llGetDayOffset() != 3600 || llGetRegionDayOffset() != 3600
                   || llGetSunDirection() != <1, 0, 0> || llGetRegionSunDirection() != <1, 0, 0>
                   || llGetMoonDirection() != <-1, 0, 0> || llGetRegionMoonDirection() != <-1, 0, 0>
                   || llGetSunRotation() != ZERO_ROTATION || llGetRegionSunRotation() != ZERO_ROTATION
                   || llGetMoonRotation() != <0, 0, 1, 0> || llGetRegionMoonRotation() != <0, 0, 1, 0>)
                   return;
               osSetRegionSunSettings(FALSE, TRUE, 11.25);
               osSetEstateSunSettings(TRUE, 17.5);
               llSetObjectName("Restored script executed");
               llCollisionSound("", -1);
           }
       }'''
      if a.script_parcel_access:
       startup_script = f'''default {{
           state_entry() {{
               llResetLandBanList();
               llAddToLandBanList("{cancel_target}", 0);
               llRemoveFromLandBanList("{cancel_target}");
               llAddToLandBanList("{parcel_observer}", 0);
               llAddToLandBanList("{ban_target}", 0);
               llAddToLandPassList("{cancel_target}", 0);
               llRemoveFromLandPassList("{cancel_target}");
               llAddToLandPassList("{timeout_target}", 0);
               llResetLandPassList();
               llAddToLandPassList("{home_target}", 0);
           }}
       }}'''.encode()
      if a.script_parcel_reset:
       startup_script = b'default { state_entry() { llResetLandPassList(); llResetLandBanList(); } }'
      if a.script_parcel_denied:
       startup_script = f'''default {{
           touch_start(integer count) {{
               llResetLandBanList();
               llAddToLandBanList("{cancel_target}", 0);
               llRemoveFromLandBanList("{ban_target}");
               llResetLandPassList();
               llAddToLandPassList("{home_target}", 0);
               llRemoveFromLandPassList("{timeout_target}");
               llSay(0, "Unauthorized parcel edit script finished");
           }}
       }}'''.encode()
      asset_db.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) '
                       'VALUES(?,?,?,?,?,?,?,?,?)',
                       (stored_script,'Stored startup script','',10,0,0,0,principal,
                        startup_script))
     database.execute('CREATE TABLE regionsettings (regionUUID TEXT, terrain_raise_limit REAL, terrain_lower_limit REAL, block_terraform INTEGER, water_height REAL, object_bonus REAL, allow_damage INTEGER, block_fly INTEGER, disable_physics INTEGER, disable_scripts INTEGER)')
     database.execute('INSERT INTO regionsettings VALUES (?,?,?,?,?,?,?,?,?,?)',
                      ('11111111-1111-4111-8111-111111111111',75.,-10.,1,27.5,1.5,1,1,1,int(a.disable_scripts)))
     database.execute('ALTER TABLE regionsettings ADD COLUMN fixed_sun INTEGER NOT NULL DEFAULT 1')
     if a.pbr_terrain:
      database.execute('ALTER TABLE regionsettings ADD COLUMN use_estate_sun INTEGER NOT NULL DEFAULT 1')
      database.execute('ALTER TABLE regionsettings ADD COLUMN sun_position REAL NOT NULL DEFAULT 12.5')
      database.execute('CREATE TABLE regionenvironment (region_id TEXT PRIMARY KEY, llsd_settings TEXT)')
      database.execute('INSERT INTO regionenvironment VALUES (?,?)',
                       ('11111111-1111-4111-8111-111111111111',
                        "<? llsd/notation ?>\n{'day_length':i36000,'day_offset':i3600,"
                        "'track_altitudes':[r10,r20,r1000],"
                        "'day_cycle':{'type':'daycycle','frames':{'Fixture':{'type':'sky',"
                        "'sun_rotation':[r0,r0,r0,r1],'moon_rotation':[r0,r0,r1,r0]},"
                        "'Ground':{'type':'sky','sun_rotation':[r0,r0,r1,r0],'moon_rotation':[r0,r0,r0,r1]}},"
                        "'tracks':[[],[{'key_keyframe':r0,'key_name':'Ground'}],[],"
                        "[{'key_keyframe':r0,'key_name':'Fixture'}],[]]}}"))
     database.execute('ALTER TABLE regionsettings ADD COLUMN maturity INTEGER NOT NULL DEFAULT 1')
     if a.os_landing:
      database.execute('ALTER TABLE regionsettings ADD COLUMN agent_limit INTEGER NOT NULL DEFAULT 40')
      database.execute('ALTER TABLE regionsettings ADD COLUMN restrict_pushing INTEGER NOT NULL DEFAULT 0')
      database.execute('ALTER TABLE regionsettings ADD COLUMN allow_land_join_divide INTEGER NOT NULL DEFAULT 1')
      database.execute('ALTER TABLE regionsettings ADD COLUMN block_show_in_search INTEGER NOT NULL DEFAULT 0')
      database.execute('ALTER TABLE regionsettings ADD COLUMN disable_collisions INTEGER NOT NULL DEFAULT 0')
      for index, corner in enumerate(('sw', 'nw', 'se', 'ne')):
       database.execute(f'ALTER TABLE regionsettings ADD COLUMN elevation_1_{corner} REAL DEFAULT {11 + index}')
       database.execute(f'ALTER TABLE regionsettings ADD COLUMN elevation_2_{corner} REAL DEFAULT {61 + index}')
       asset = uuid.UUID(int=index + 21)
       database.execute(f"ALTER TABLE regionsettings ADD COLUMN terrain_texture_{index + 1} "
                        f"TEXT DEFAULT '{asset}'")
       material = uuid.UUID(int=index + 31)
       database.execute(f"ALTER TABLE regionsettings ADD COLUMN TerrainPBR{index + 1} "
                        f"TEXT DEFAULT '{material}'")
     database.execute('ALTER TABLE regionsettings ADD COLUMN allow_land_resell INTEGER NOT NULL DEFAULT 0')
     database.execute("ALTER TABLE regionsettings ADD COLUMN covenant CHAR(36) DEFAULT '99887766-5544-4332-8110-010203040506'")
     database.execute('ALTER TABLE regionsettings ADD COLUMN covenant_datetime INTEGER NOT NULL DEFAULT 1700000123')
     database.execute("ALTER TABLE regionsettings ADD COLUMN TelehubObject TEXT NOT NULL DEFAULT 'eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee'")
     database.execute('CREATE TABLE spawn_points (RegionID TEXT, Yaw FLOAT, Pitch FLOAT, Distance FLOAT)')
     database.execute('INSERT INTO spawn_points VALUES (?,?,?,?)',
                      ('11111111-1111-4111-8111-111111111111',0,0,5))
     if a.telehub_parcel_access:
      database.execute('UPDATE prims SET GroupPositionX=128 WHERE UUID=?', (stored_root,))
      database.execute('DELETE FROM spawn_points')
      database.executemany('INSERT INTO spawn_points VALUES (?,?,?,?)',
                           [('11111111-1111-4111-8111-111111111111', 0, 0, distance)
                            for distance in (0, 8)])
      database.execute('UPDATE land SET LandFlags=CAST(LandFlags AS INTEGER)|1024 WHERE LocalLandID=1')
      database.execute('UPDATE land SET LandFlags=CAST(LandFlags AS INTEGER)|512 WHERE LocalLandID=2')
      if a.parcel_relocation:
       database.execute('UPDATE land SET LandingType=2')
      if a.varregion_relocation:
       bitmap = bytearray(128 * 128 // 8)
       for y in range(64):
        for x in range(75 if 16 <= y < 48 else 77, 80):
         cell = y * 128 + x
         bitmap[cell // 8] |= 1 << (cell % 8)
       database.execute('UPDATE land SET Bitmap=? WHERE LocalLandID=2', (bitmap,))
       if a.group_terrain:
        database.execute("ALTER TABLE land ADD COLUMN IsGroupOwned TEXT NOT NULL DEFAULT 'False'")
        database.execute("UPDATE land SET IsGroupOwned='True', OwnerUUID=GroupUUID WHERE LocalLandID=2")
       elif a.public_terrain:
        database.execute('UPDATE land SET LandFlags=CAST(LandFlags AS INTEGER)|16 WHERE LocalLandID=2')
       else:
        database.execute('UPDATE land SET OwnerUUID=? WHERE LocalLandID=2', (parcel_observer,))
      database.execute("UPDATE land SET UUID='a0000000-0000-4000-8000-000000000001' WHERE LocalLandID=1")
      if a.group_parcel_access:
       database.execute("ALTER TABLE land ADD COLUMN IsGroupOwned TEXT NOT NULL DEFAULT 'False'")
       database.execute("UPDATE land SET IsGroupOwned='True', OwnerUUID=GroupUUID WHERE LocalLandID=1")
      database.execute('CREATE TABLE landaccesslist (LandUUID TEXT, AccessUUID TEXT, Flags INTEGER)')
      database.executemany('INSERT INTO landaccesslist VALUES (?,?,?)', [
          ('A0000000-0000-4000-8000-000000000001', parcel_observer, 2),
          ('A0000000-0000-4000-8000-000000000001', str(ban_target), 2),
          ('00000000-0000-4000-8000-000000000002', parcel_observer, 1),
      ])
      if a.script_parcel_reset or a.script_parcel_denied:
       seeded_pass = timeout_target if a.script_parcel_denied else home_target
       database.execute('INSERT INTO landaccesslist VALUES (?,?,?)',
                        ('A0000000-0000-4000-8000-000000000001', str(seeded_pass), 1))
      if a.parcel_profile_access:
       database.execute('DELETE FROM landaccesslist')
       database.execute('UPDATE land SET LandFlags=2147483648 WHERE LocalLandID=1')
       database.execute('UPDATE land SET LandFlags=4194304 WHERE LocalLandID=2')
     size = 512 if a.varregion_relocation or a.extended_pbr_terrain else 256
     samples = [21.] * (size * size)
     samples[107 * size + 107] = 24.
     if a.pbr_terrain:
      samples[132] = 23.
      samples[255 * size + 255] = 27.
     database.execute('INSERT INTO terrain VALUES (?,?,?)',
                      ('11111111-1111-4111-8111-111111111111', 22,
                       struct.pack(f'<II{len(samples)}f', size, size, *samples)))
   if a.block_flight:
    check(bool(a.physics_library),'Blocked flight requires a physics library')
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
     database.execute('CREATE TABLE regionsettings (regionUUID TEXT, terrain_raise_limit REAL, terrain_lower_limit REAL, block_terraform INTEGER, water_height REAL, block_fly INTEGER)')
     database.execute('INSERT INTO regionsettings VALUES (?,?,?,?,?,?)',
                      ('11111111-1111-4111-8111-111111111111',100.,-100.,0,20.,1))
   with contextlib.closing(sqlite3.connect(root/'gridusers.db')) as database, database:
    database.execute("INSERT INTO GridUser (UserID, HomeRegionID, HomePosition, HomeLookAt) VALUES (?, ?, ?, ?)",
                     (principal,'11111111-1111-4111-8111-111111111111','<128,128,25>','<0,1,0>'))
  if a.advanced_profiles:
   with socket.socket() as occupied:
    occupied.bind(('127.0.0.1',offline_port))
    occupied.listen()
    conflict=subprocess.run(cmd+['--console'],input='"quit"\n',capture_output=True,text=True,timeout=15)
    check(conflict.returncode!=0 and f'Cannot start OfflineIM listener on port {offline_port}' in conflict.stderr,
          'OfflineIM bind conflict reports the configured port')
   if json.loads(subprocess.check_output([cmd[0],'--features'],text=True,timeout=5))['tls']:
    certificates=Path(__file__).resolve().parent/'fixtures'
    tls_config=root/'offline-tls.ini'
    tls_settings=config.read_text(encoding='utf-8').replace(
        'EnableLocalRegion = true','EnableLocalRegion = false\n'
        f'TlsCertificate={certificates/"localhost-cert.pem"}\nTlsPrivateKey={certificates/"localhost-key.pem"}')
    tls_config.write_text(tls_settings,encoding='utf-8')
    with subprocess.Popen([cmd[0],'--config',str(tls_config),'--console'],stdin=subprocess.PIPE,
                          stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True) as mixed:
     try:
      deadline=time.monotonic()+10
      while True:
       try:
        with urllib.request.urlopen(f'https://127.0.0.1:{profile_port}/health',timeout=1,
                                    context=ssl._create_unverified_context()) as response:
         check(response.status==200,'Main listener serves HTTPS with a dedicated OfflineIM listener')
        break
       except urllib.error.URLError:
        if mixed.poll() is not None or time.monotonic()>=deadline:raise
        time.sleep(.05)
      request=urllib.request.Request(f'http://127.0.0.1:{offline_port}/offlineim',b'METHOD=GET',
                                     {'Authorization':'Basic bWFpbGJveDpmaXh0dXJl'})
      with urllib.request.urlopen(request,timeout=5) as response:
       check(ET.fromstring(response.read()).findtext('RESULT')=='NULL','Dedicated OfflineIM port serves HTTP with main TLS enabled')
     finally:
      if mixed.poll() is None:
       mixed.stdin.write('"quit"\n');mixed.stdin.flush()
       try:mixed.wait(timeout=15)
       except subprocess.TimeoutExpired:mixed.kill();mixed.wait();raise
  if a.primary_https:
   valid_settings = config.read_text(encoding='utf-8')
   rejected_config = root / 'rejected-certificate-host.ini'
   mismatches = [
       ('http_listener_cn = localhost', 'http_listener_cn = not-covered.example',
        'invalid main http server CN (ExternalHostName)'),
       ('ExternalHost = localhost', 'ExternalHost = 127.0.0.1',
        "main http cert CN doesn't match region External IP"),
   ]
   for setting, mismatch, expected in mismatches:
    check(setting in valid_settings, 'Certificate hostname rejection fixture selects a configured hostname')
    rejected_config.write_text(valid_settings.replace(setting, mismatch), encoding='utf-8')
    rejected = subprocess.run(
        [str(Path(a.native).resolve()), '--config', str(rejected_config)],
        capture_output=True, text=True, timeout=20)
    check(rejected.returncode != 0 and expected in rejected.stderr,
          'Primary TLS rejects the mismatched hostname before serving requests: ' + rejected.stderr)
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
    if line is None:
     proc.wait(timeout=5)
     raise RuntimeError(f'Server startup failed (exit {proc.returncode}): '+''.join(errors))
    if line.startswith('LISTENING '):url=line.split(' ',1)[1].strip();break
   check(bool(url),'Native region starts HTTP and UDP')
   primary_context = None
   if a.primary_https:
    primary = urllib.parse.urlsplit(url)
    check(primary.scheme == 'https', 'The main listener advertises HTTPS')
    primary_context = ssl._create_unverified_context()
    fixture_der = ssl.PEM_cert_to_DER_cert((tls_fixture / 'localhost-cert.pem').read_text(encoding='ascii'))
    with contextlib.closing(http.client.HTTPSConnection(
        primary.hostname, primary.port, timeout=8, context=primary_context)) as connection:
     connection.connect()
     check(connection.sock.getpeercert(binary_form=True) == fixture_der,
           'The primary TLS listener presents the configured certificate')
    urllib.request.install_opener(urllib.request.build_opener(urllib.request.HTTPSHandler(context=primary_context)))
   if a.advanced_profiles:
    recipient='92929292-9292-4292-8292-929292929292'
    offline_url=f'http://127.0.0.1:{offline_port}/offlineim'
    def offline_form(**fields):
     request=urllib.request.Request(offline_url,urllib.parse.urlencode(fields).encode(),
                                    {'Authorization':'Basic bWFpbGJveDpmaXh0dXJl'})
     with urllib.request.urlopen(request,timeout=5) as response:
      return ET.fromstring(response.read())
    message=dict(METHOD='STORE',ToAgentID=recipient,Message='Mailbox fixture & café\nSecond line',BinaryBucket='00ff')
    try:
     with urllib.request.urlopen(url.rstrip('/')+'/offlineim',b'METHOD=GET',timeout=5):
      raise AssertionError('Main listener exposes the private OfflineIM route')
    except urllib.error.HTTPError as error:
     check(error.code==404,'Main listener omits the dedicated OfflineIM route')
    unauthorized=urllib.request.Request(offline_url,urllib.parse.urlencode(message).encode())
    try:
     with urllib.request.urlopen(unauthorized,timeout=5):
      raise AssertionError('Offline mailbox accepted an unauthenticated STORE')
    except urllib.error.HTTPError as error:
     check(error.code==401,'Offline mailbox rejects an unauthenticated STORE')
    check(offline_form(**message,Dialog='256').findtext('RESULT')=='False','Native offline endpoint rejects an out-of-range dialog')
    check(offline_form(**{**message,'ToAgentID':'invalid'}).findtext('RESULT')=='False',
          'Native offline endpoint rejects an invalid recipient')
    check(offline_form(**message).findtext('RESULT')=='True','Native offline endpoint stores a message')
    with contextlib.closing(sqlite3.connect(root/'offline.db')) as database:
     record=ET.fromstring(database.execute('SELECT Message FROM fixture_offline WHERE PrincipalID=?',(recipient,)).fetchone()[0])
    check(record.tag=='GridInstantMessage' and record.findtext('binaryBucket')=='AP8=' and
          record.findtext('message')==message['Message'] and record.findtext('toAgentID')==recipient,
          'Native mailbox writes legacy XML element names and Base64')
    check(offline_form(**message).findtext('REASON')=='Number of offline IMs has maxed out','Native offline endpoint enforces the mailbox limit')
    drained=offline_form(METHOD='GET',PrincipalID=recipient)
    check(drained.findtext('RESULT/im-0/Message')==message['Message'] and drained.findtext('RESULT/im-0/BinaryBucket')=='00 FF','Native offline endpoint returns text and binary bucket')
    check(len(offline_form(METHOD='GET',PrincipalID=recipient).find('RESULT'))==0,'Native offline endpoint drains the mailbox')
    check(offline_form(**message).findtext('RESULT')=='True' and offline_form(METHOD='DELETE',UserID=recipient).findtext('RESULT')=='True','Native offline endpoint deletes a queued message')
    check(len(offline_form(METHOD='GET',PrincipalID=recipient).find('RESULT'))==0,'Deleted offline message stays absent')
    check(offline_form(**message).findtext('RESULT')=='True','Mailbox queues a valid row before a corrupt row')
    with contextlib.closing(sqlite3.connect(root/'offline.db')) as database, database:
     database.execute("INSERT INTO fixture_offline (PrincipalID, Message) VALUES (?, ?)",
                      (recipient,'<GridInstantMessage><fromGroup>invalid</fromGroup></GridInstantMessage>'))
    check(offline_form(METHOD='GET',PrincipalID=recipient).findtext('RESULT')=='False','Corrupt mailbox row aborts retrieval')
    with contextlib.closing(sqlite3.connect(root/'offline.db')) as database, database:
     count=database.execute("SELECT count(*) FROM fixture_offline WHERE PrincipalID=?", (recipient,)).fetchone()[0]
     check(count==2,'Failed retrieval preserves both mailbox rows')
    check(offline_form(METHOD='DELETE',UserID=recipient).findtext('RESULT')=='True','Explicit deletion clears a corrupt mailbox')
    with contextlib.closing(sqlite3.connect(root/'offline.db')) as database, database:
     database.execute('INSERT INTO fixture_offline (PrincipalID, Message) VALUES (?, ?)',
                      (recipient,'<GridInstantMessage><fromAgentID>invalid</fromAgentID></GridInstantMessage>'))
    check(offline_form(METHOD='GET',PrincipalID=recipient).findtext('RESULT')=='False',
          'Mailbox rejects a malformed legacy UUID')
    check(offline_form(METHOD='DELETE',UserID=recipient).findtext('RESULT')=='True','Mailbox can clear the malformed UUID row')
    legacy_xml=f'<GridInstantMessage><toAgentID>{recipient}</toAgentID><message>Legacy &amp; café</message><Position><X>1</X><Y>2</Y><Z>3</Z></Position><binaryBucket>AAH/</binaryBucket></GridInstantMessage>'
    with contextlib.closing(sqlite3.connect(root/'offline.db')) as database, database:
     database.execute('INSERT INTO fixture_offline (PrincipalID, Message) VALUES (?, ?)', (recipient,legacy_xml))
    legacy=offline_form(METHOD='GET',PrincipalID=recipient).find('RESULT/im-0')
    check(legacy is not None and legacy.findtext('Message')=='Legacy & café' and
          legacy.findtext('BinaryBucket')=='00 01 FF' and legacy.findtext('Position')=='<1, 2, 3>',
          'Mailbox reads legacy GridInstantMessage XML')
    proc.stdin.write('profiles status\n');proc.stdin.flush()
    status=None;deadline=time.monotonic()+10
    while time.monotonic()<deadline:
     line=lines.get(timeout=max(0.01,deadline-time.monotonic()))
     if line is None:break
     if line.startswith('Profile requests in '):status=line.strip();break
    check(status=="Profile requests in 'Connected Native' Local: 0 HG: 0",
          'Profile console reports local and Hypergrid queue counts')
   if a.bakes:check(bool(url),'Application Nini boolean aliases select real memory cache profile')
   proxy=xmlrpc.client.ServerProxy(url, context=primary_context)
   credentials=dict(first='Native',last='Viewer',passwd='viewer-test-password',start='home',channel='Native test',version='1.0')
   check(proxy.login_to_simulator({**credentials,'passwd':'wrong'})['login']=='false','Wrong password rejected')
   login=proxy.login_to_simulator(credentials);check(login.get('login')=='true','Actual local scene accepts login: '+str(login))
   if a.primary_https:
    seed_uri = urllib.parse.urlsplit(login['seed_capability'])
    check(seed_uri.scheme == 'https' and seed_uri.hostname == 'localhost' and
          seed_uri.port == urllib.parse.urlsplit(url).port,
          'Login advertises the region external host and primary TLS port for the seed')
   if a.presence_provider:
    def presence_request(**fields):
     body = urllib.parse.urlencode(fields).encode()
     with urllib.request.urlopen(url.rstrip('/') + '/presence', body, timeout=5) as response:
      return ET.fromstring(response.read())

    session_id = login['session_id']
    check(presence_request(METHOD='getagent', SessionID=session_id).findtext('result/UserID') ==
          login['agent_id'], f'{a.presence_provider} presence resolves the admitted login')
    check(presence_request(METHOD='report', SessionID=session_id, RegionID=str(uuid.UUID(int=0)))
          .findtext('result') == 'Failure', f'{a.presence_provider} presence rejects a zero region report')
   if a.grid_user_provider:
    def grid_user_record():
     body = urllib.parse.urlencode({'METHOD': 'getgriduserinfo', 'UserID': login['agent_id']}).encode()
     with urllib.request.urlopen(url.rstrip('/') + '/griduser', body, timeout=5) as response:
      return ET.fromstring(response.read()).find('result')

    check(grid_user_record().findtext('Online') == 'True',
          f'{a.grid_user_provider} grid-user store records viewer login')
   seed=login['seed_capability']
   if not a.primary_https:
    check(seed.startswith(url),'Seed points to actual local HTTP listener')
   request=urllib.request.Request(seed,b'<llsd><array><string>EventQueueGet</string><string>SimulatorFeatures</string><string>AgentPreferences</string><string>UpdateAgentLanguage</string><string>UpdateAgentInformation</string></array></llsd>',{'Content-Type':'application/llsd+xml'})
   with urllib.request.urlopen(request,timeout=10) as response:body=response.read();check(response.status==200 and b'EventQueueGet' in body,'Active source seed negotiates real EventQueueGet')
   caps=list(ET.fromstring(body).find('map'))
   cap_urls={caps[i].text:caps[i+1].text for i in range(0,len(caps),2)}
   if a.primary_https:
    event_uri = urllib.parse.urlsplit(cap_urls['EventQueueGet'])
    check(event_uri.scheme == 'https' and event_uri.hostname == 'localhost' and
          event_uri.port == urllib.parse.urlsplit(url).port,
          'Named capabilities advertise the configured TLS common name and port')
   preferences=cap_urls['AgentPreferences']
   check(preferences==cap_urls['UpdateAgentLanguage']==cap_urls['UpdateAgentInformation'],'Agent preference capabilities share the source route')
   preference_body=b'<llsd><map><key>hover_height</key><real>0.125</real><key>language</key><string>fr-ca</string><key>language_is_public</key><boolean>false</boolean></map></llsd>'
   with urllib.request.urlopen(urllib.request.Request(preferences,preference_body,{'Content-Type':'application/llsd+xml'}),timeout=5) as response:
    prefs=list(ET.fromstring(response.read()).find('map'))
   prefs={prefs[i].text:prefs[i+1].text for i in range(0,len(prefs),2)}
   check(float(prefs['hover_height'])==0.125 and prefs['language']=='fr-ca' and prefs['language_is_public'] in ('false','0'),
         'AgentPreferences returns submitted hover and language preferences')
   if rotating_prim:
    with contextlib.closing(sqlite3.connect(root/'preferences.db')) as db, db:
     db.execute('INSERT INTO AgentPrefs (PrincipalID,Language) VALUES (?,?)',
                ('17495d6b-53c5-4ca7-a3fa-801ff116a0e1','de-de'))
   if remote_language_probe:
    with contextlib.closing(sqlite3.connect(root/'remote-preferences.db')) as db, db:
     db.execute('INSERT INTO AgentPrefs (PrincipalID,Language) VALUES (?,?)',
                ('17495d6b-53c5-4ca7-a3fa-801ff116a0e1','de-de'))
   if a.raw_fps:
    partial=b'<llsd><map><key>language</key><string>de-de</string><key>default_object_perm_masks</key><map><key>Everyone</key><integer>8192</integer><key>Group</key><integer>32768</integer><key>NextOwner</key><integer>40960</integer></map></map></llsd>'
    with urllib.request.urlopen(urllib.request.Request(cap_urls['UpdateAgentLanguage'],partial,{'Content-Type':'application/llsd+xml'}),timeout=5) as response:
     fields=list(ET.fromstring(response.read()).find('map'))
    fields={fields[i].text:fields[i+1].text for i in range(0,len(fields),2)}
    check(float(fields['hover_height'])==0.125 and fields['language']=='de-de' and fields['language_is_public'] in ('false','0'),
          'Partial preference update preserves stored hover and privacy')
    with contextlib.closing(sqlite3.connect(root/'preferences.db')) as db:
     saved=db.execute('SELECT HoverHeight,Language,LanguageIsPublic FROM AgentPrefs WHERE PrincipalID=?',(login['agent_id'],)).fetchone()
    check(saved==(0.125,'de-de',0),'Agent preferences persist in the legacy SQLite schema')
   features_url=cap_urls['SimulatorFeatures']
   with urllib.request.urlopen(features_url,timeout=10) as response:
    features=list(ET.fromstring(response.read()).find('map'))
   extras=list({features[i].text:features[i+1] for i in range(0,len(features),2)}['OpenSimExtras'])
   scales={extras[i].text:extras[i+1] for i in range(0,len(extras),2)}
   check(abs(float(scales['MinPrimScale'].text)-0.001)<1e-9 and float(scales['MaxPrimScale'].text)==256,
         'SimulatorFeatures advertises the region prim size limits')
   expected_fps_factor=1 if a.raw_fps else 55*(0.005 if a.physics_library or a.touch else 0.05)
   check(abs(float(scales['SimulatorFPSFactor'].text)-expected_fps_factor)<1e-6,
         'SimulatorFeatures advertises the native region FPS normalization factor')
   check(abs(float(scales['MinPhysPrimScale'].text)-0.01)<1e-9 and float(scales['MaxPhysPrimScale'].text)==10,
         'SimulatorFeatures advertises the physical prim size limits')
   check(scales['ExportSupported'].tag=='boolean' and scales['ExportSupported'].text in ('0','false'),
         'SimulatorFeatures advertises the grid export policy as a boolean')
   check(scales['destination-guide-url'].text=='https://example.invalid/guide/'+login['agent_id'],
         'SimulatorFeatures substitutes the logged-in viewer in the grid destination guide')
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
     def cache_console_output(command,completion):
      proc.stdin.write('fcache '+command+'\n');proc.stdin.flush();result=[];deadline=time.monotonic()+15
      while time.monotonic()<deadline:
       try:line=lines.get(timeout=max(.01,deadline-time.monotonic()))
       except queue.Empty:break
       if line is None:raise AssertionError('Server exited during '+command)
       result.append(line.strip())
       if line.startswith(completion):return result
      raise AssertionError('No console output for '+command+': '+str(result)+' '+''.join(errors))
     status_lines=cache_console_output('status','Requests overlap during file writing:')
     check(any(line.startswith('[FLOTSAM ASSET CACHE] Memory Cache:') for line in status_lines) and any(line.startswith('Total requests:') for line in status_lines),'Actual console status reports memory count and original hit report')
     status,_,_=asset_fetch('ViewerAsset','texture_id=abababab-abab-4bab-8bab-abababababab');check(status==404,'Negative cache fixture misses before console clear')
     cleared=cache_console_output('clearnegatives','Flotsam cache of negatives cleared (');check(any('1 entries)' in line for line in cleared),'Actual console clears and counts negative entry')
     cleared=cache_console_output('clear "memory"','Memory cache cleared.');check(cleared[-1]=='Memory cache cleared.','Actual console clears only memory and retains weak dictionary semantics')
     def cache_command(command,completion):
      cache_console_output(command,completion)
     cache_command('deletedefaultassets','[FLOTSAM ASSET CACHE] deleted ')
     status,_,_=asset_fetch('ViewerAsset','texture_id='+cache_default_id);check(status==404,'Actual console delete expires a default whose SQL row is absent')
     replacement_default=b'Console reloaded cache default\x00\xfe';(root/'cache-default.bin').write_bytes(replacement_default)
     cache_command('cachedefaultassets','[FLOTSAM ASSET CACHE] loaded ')
     status,data,_=asset_fetch('ViewerAsset','texture_id='+cache_default_id);check(status==200 and data==replacement_default,'Actual console reload replaces default bytes and clears negative entry')
     with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:check(db.execute('SELECT count(*) FROM assets WHERE UUID=?',(cache_default_id,)).fetchone()[0]==0,'Console default reload remains cache-only without SQL writes')
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
    def binary_items(ids):
     def n(value):return struct.pack('>i',value)
     def key(value):return b'k'+n(len(value))+value
     return b'<? llsd/binary ?>\n{'+n(1)+key(b'items')+b'['+n(len(ids))+b''.join(b'{'+n(1)+key(b'item_id')+b'u'+uuid.UUID(value).bytes+b'}' for value in ids)+b']}'
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
    for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\x00\x00'),('utf-32-be',b'\x00\x00\xfe\xff')]:
     encoded_status,encoded_data=inventory_request(bom+json.dumps(dict(items=[dict(item_id=item_id),dict(item_id='00000000-0000-0000-0000-000000000002')])).encode(encoding))
     check(encoded_status==200 and decode(ET.fromstring(encoded_data)[0])==result,'BOM '+encoding+' item request matches JSON through real inventory provider')
    def notation_items(ids):return ("<? llsd/notation ?>{'items':["+','.join("{'item_id':u"+value+"}" for value in ids)+"]}").encode()
    notation_status,notation_data=inventory_request(notation_items([item_id,'00000000-0000-0000-0000-000000000002']))
    check(notation_status==200 and decode(ET.fromstring(notation_data)[0])==result,'Notation LLSD item request matches JSON through real inventory provider')
    binary_status,binary_data=inventory_request(binary_items([item_id,'00000000-0000-0000-0000-000000000002']))
    binary_result=decode(ET.fromstring(binary_data)[0]);check(binary_status==200 and binary_result==result,'Binary LLSD item request matches JSON through real inventory provider')
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
    req=urllib.request.Request(library_urls['FetchLib2'],binary_items([sample['item_id'],'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa']),{'Content-Type':'application/llsd+binary'})
    with urllib.request.urlopen(req,timeout=5) as response:binary_result=decode(ET.fromstring(response.read())[0])
    check(binary_result==result,'Binary LLSD library request matches JSON through actual library provider')
    req=urllib.request.Request(library_urls['FetchLib2'],notation_items([sample['item_id'],'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa']),{'Content-Type':'application/llsd+notation'})
    with urllib.request.urlopen(req,timeout=5) as response:notation_result=decode(ET.fromstring(response.read())[0])
    check(notation_result==result,'Notation LLSD library request matches JSON through actual library provider')
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
   server_lines=lines
   removed_task_items=set()
   primary_viewer_prim=None
   def probe(session,appearance_sender=None,verify_hover=False,warm_notecard=False):
    nonlocal primary_viewer_prim
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
    if a.advanced_profiles:
     with contextlib.closing(sqlite3.connect(root/'profiles.db')) as db, db:
      db.execute('INSERT INTO usersettings(useruuid,imviaemail,visible,email) VALUES(?,?,?,?)',
                 (session['agent_id'],'False','True','viewer@example.test'))
     pick='91919191-9191-4191-8191-919191919191'
     body=json.dumps(dict(jsonrpc='2.0',id='seed-pick',method='picks_update',params=dict(
      PickId=pick,CreatorId=session['agent_id'],Name='Native profile pick',TopPick=True,Enabled=True)))
     request=urllib.request.Request(url.rstrip('/')+'/profiles',body.encode(),{'Content-Type':'application/json-rpc'})
     with urllib.request.urlopen(request,timeout=10) as response:
      check('result' in json.load(response),'Native profile service stores the seeded pick')
     body=json.dumps(dict(jsonrpc='2.0',id='create-properties',method='avatar_properties_request',
                          params=dict(UserId=session['agent_id'])))
     request=urllib.request.Request(url.rstrip('/')+'/profiles',body.encode(),{'Content-Type':'application/json-rpc'})
     with urllib.request.urlopen(request,timeout=10) as response:
      check('result' in json.load(response),'Native profile service initializes the avatar row')
     body=json.dumps(dict(jsonrpc='2.0',id='seed-properties',method='avatar_properties_update',
                          params=dict(UserId=session['agent_id'],AboutText='Stored native profile')))
     request=urllib.request.Request(url.rstrip('/')+'/profiles',body.encode(),{'Content-Type':'application/json-rpc'})
     with urllib.request.urlopen(request,timeout=10) as response:
      check('result' in json.load(response),'Native profile service stores avatar properties')
    extra=['I am Alive!','Touched.'] if a.touch else ['Script running','']
    if remote_language_probe:extra[1]='Touched. en-us/de-de'
    if rotating_prim:
     extra[0] = 'Disabled script fixture' if a.disable_scripts else 'Rotating fixture'
    extra += [('--walk-block-flight' if a.block_flight else '--walk') if a.physics_library else '',str(legacy_metadata) if legacy_metadata else '']
    extra+=['--verify-script-assets' if a.assets else '--verify-script-events']
    if a.assets:
     extra+=['--udp-texture='+str(source/'bin/assets/TexturesAssetSet/default_avatar.jp2')]
     script_fixture = source/'bin/assets/ScriptsAssetSet'/('DefaultSayAndTouch.lsl' if a.touch else 'DefaultScript.lsl')
     if remote_language_probe:
      script_fixture = root/'remote-language.lsl'
     if rotating_prim:
      script_fixture = root/'rotating-prim.lsl'
     extra+=['--udp-task-script='+str(script_fixture)]
    if a.profiles_disabled:extra+=['--no-profiles']
    if notecard_probe:
     extra+=['--verify-warm-notecard' if warm_notecard else '--verify-notecard']
    if a.physics_library and a.touch and not a.assets and not a.block_flight:
     extra+=['--verify-estate-debug']
    if a.advanced_profiles:extra+=['--advanced-profiles']
    if a.touch and not a.physics_library:extra+=['--stored-linkset']
    if appearance_sender:extra+=['--appearance-sender='+appearance_sender]
    if verify_hover:extra+=['--verify-appearance-hover']
    if a.raw_fps:extra+=['--verify-default-object-permissions','--verify-mute-list','--verify-denied-home']
    if a.raw_fps and not a.remote_mutes:
     extra.append('--verify-mute-owner')
    if a.bakes:extra+=['--verify-baked-cache']
    if a.bakes and baked_ids:extra+=['--restored-bake='+baked_ids[-1]]
    command=[str(Path(a.viewer).resolve()),session['agent_id'],session['session_id'],str(session['circuit_code']),session['sim_ip'],str(session['sim_port']),*extra]
    lines=[];inbox=queue.Queue();process=subprocess.Popen(command,text=True,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    def read_probe():
     for line in process.stdout:inbox.put(line)
     inbox.put(None)
    reader=threading.Thread(target=read_probe,daemon=True);reader.start();deadline=time.monotonic()+90;script_ids=[];edit_phase=0;phase_payload_seen=0;failed_item=None;copy_ids_snapshot=None
    default_script=script_fixture.read_bytes() if a.assets else b''
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
      if line.startswith('Purchased object asset '):
       asset_id,item_id=line.split()[3:]
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
        item=db.execute('SELECT assetID,assetType,invType,avatarID FROM inventoryitems WHERE inventoryID=?',(item_id,)).fetchone()
       check(item==(asset_id,6,6,session['agent_id']),'Purchased object inventory row references the stored object asset')
       coalesced_asset,coalesced_item=str(uuid.uuid4()),str(uuid.uuid4())
       linked_asset,linked_item=str(uuid.uuid4()),str(uuid.uuid4())
       with contextlib.closing(sqlite3.connect(root/'assets.db')) as db, db:
        row=db.execute('SELECT * FROM assets WHERE UUID=?',(asset_id,))
        stored=dict(zip((field[0] for field in row.description),row.fetchone()))
        linked=ET.fromstring(stored['Data'])
        root_part=linked.find('./RootPart/SceneObjectPart')
        child=ET.fromstring(ET.tostring(root_part))
        for part in (root_part,child):
         part.find('RotationOffset/W').text='1'
         for axis in 'XYZ':part.find(f'RotationOffset/{axis}').text='0'
        child.find('OffsetPosition/Z').text='-6'
        child.find('Flags').text='Phantom CastShadows'
        for field in child.find('OffsetPosition'):field.tag=field.tag.lower()
        for field in child.find('RotationOffset'):field.tag=field.tag.lower()
        child.find('UUID/UUID').tag='Guid'
        child.find('UUID/Guid').text=str(uuid.uuid4())
        child.remove(child.find('TaskInventory'))
        ET.SubElement(linked.find('OtherParts'),'Part').append(child)
        script_asset=str(uuid.uuid4())
        task=root_part.find('TaskInventory/TaskInventoryItem')
        task.find('AssetID/UUID').text=script_asset
        animation=ET.fromstring(ET.tostring(task))
        animation.find('ItemID/UUID').text=str(uuid.uuid4())
        animation.find('AssetID/UUID').text='18181818-1818-4818-8818-181818181818'
        animation.find('Name').text='Inventory wave'
        animation.find('Type').text='20'
        animation.find('InvType').text='19'
        animation.find('CreatorID/UUID').text='12345678-1234-4234-8234-123456789abc'
        animation.find('CreationDate').text='4294967295'
        root_part.find('TaskInventory').append(animation)
        data=bytes([1,0x80,0x80,0x10,2])+b'\x06public\x05hello\x00\x06secret\x05world\x04pass'
        ET.SubElement(linked,'lnkstdt').text=base64.b64encode(data).decode('ascii')
        ET.SubElement(linked,'StartStr').text='asset-only'
        script=b'default { state_entry() { if (llLinksetDataRead("persist") == "value") { llSay(0, "Permission reset " + (string)llGetPermissions()); return; } llSay(0, "Inventory metadata " + (string)llGetInventoryCreator("Inventory wave") + "/" + llGetInventoryAcquireTime("Inventory wave") + "/" + (string)llGetGeometricCenter()); string missing = llGetInventoryDesc("Missing description fixture"); if (llGetRegionFPS() > 0) llSay(0, "Region FPS sampled"); llSay(0, "Linkset data " + (string)llGetStartParameter() + "/" + llGetStartString() + ":" + llLinksetDataRead("public") + "/" + llLinksetDataRead("secret") + "/" + llLinksetDataReadProtected("secret", "wrong") + "/" + llLinksetDataReadProtected("secret", "pass") + "/" + (string)llLinksetDataCountKeys() + "/" + (string)llLinksetDataAvailable()); integer added = llLinksetDataWrite("new", "value"); integer unchanged = llLinksetDataWrite("new", "value"); integer denied = llLinksetDataDelete("secret"); integer deleted = llLinksetDataDeleteProtected("secret", "pass"); if (added == LINKSETDATA_OK && unchanged == LINKSETDATA_NOUPDATE && denied == LINKSETDATA_EPROTECTED) llSay(0, "Linkset mutation " + (string)added + "/" + (string)unchanged + "/" + (string)denied + "/" + (string)deleted + "/" + llLinksetDataRead("new")); integer protected_result = llLinksetDataWriteProtected("hidden", "private", "pass"); llSay(0, "Linkset keys " + llList2CSV(llLinksetDataListKeys(1, 1)) + "/" + llList2CSV(llLinksetDataFindKeys("^h", 0, 0)) + "/" + (string)llLinksetDataCountFound("^h")); llSay(0, "Linkset bulk " + llList2CSV(llLinksetDataDeleteFound("^(new|hidden|public)$", "wrong"))); integer last = llLinksetDataDeleteProtected("hidden", "pass"); llSay(0, "Linkset last " + (string)last + "/" + (string)llLinksetDataAvailable()); integer restored = llLinksetDataWrite("new", "value"); llLinksetDataReset(); llLinksetDataReset(); llSay(0, "Linkset reset " + (string)llLinksetDataCountKeys() + "/" + (string)llLinksetDataAvailable()); integer persisted = llLinksetDataWrite("persist", "value"); integer more = llLinksetDataWrite("extra", "yes"); integer final_key = llLinksetDataWrite("z", "x"); } on_rez(integer parameter) { llSay(0, "On rez " + (string)parameter + "/" + (string)llGetPermissions() + "/" + (string)llGetPermissionsKey()); llRequestPermissions(llGetOwner(), PERMISSION_TRIGGER_ANIMATION | PERMISSION_OVERRIDE_ANIMATIONS); } run_time_permissions(integer mask) { llSay(0, "Runtime permissions " + (string)mask + "/" + (string)llGetPermissions() + "/" + (string)llGetPermissionsKey()); llStartAnimation("hello"); llSay(0, "Animation list " + llList2CSV(llGetAnimationList(llGetOwner())) + "/" + llGetAnimation(llGetOwner()) + "/" + llGetAnimationOverride("wAlKiNg") + "/" + llGetAnimationOverride("invalid")); llStopAnimation("hello"); llStartAnimation("Inventory wave"); llStopAnimation("Inventory wave"); llSetAnimationOverride("Standing", "Inventory wave"); llSay(0, "Override " + llGetAnimationOverride("standing") + "/" + llList2CSV(llGetAnimationList(llGetOwner()))); llResetAnimationOverride("ALL"); llStopAnimation("stand"); llResetScript(); } touch_start(integer n) { llSay(0, "Permission after replay " + (string)llGetPermissions() + "/" + (string)llGetListLength(llGetAnimationList(llGetOwner()))); } linkset_data(integer action, string name, string value) { if (action >= LINKSETDATA_RESET && action <= LINKSETDATA_MULTIDELETE) llSay(0, "Linkset event " + (string)action + "/" + name + "/" + value); } }'
        script_row=dict(stored,UUID=script_asset,Type=10,Data=script)
        db.execute(f"INSERT INTO assets ({','.join(script_row)}) VALUES ({','.join('?' for _ in script_row)})",tuple(script_row.values()))
        stopped_asset,stopped_item=str(uuid.uuid4()),str(uuid.uuid4())
        stopped=ET.fromstring(ET.tostring(task))
        stopped.find('AssetID/UUID').text=stopped_asset
        stopped.find('ItemID/UUID').text=stopped_item
        ET.SubElement(child,'TaskInventory').append(stopped)
        saved=ET.SubElement(ET.SubElement(linked,'GroupScriptStates'),'SavedScriptState',UUID=stopped_item)
        ET.SubElement(ET.SubElement(saved,'ScriptState',Engine='YEngine'),'Running').text='False'
        stopped_row=dict(stored,UUID=stopped_asset,Type=10,
          Data=b'default { state_entry() { llSay(0, "Stopped script ran"); } on_rez(integer param) { llSay(0, "Stopped script ran"); } }')
        db.execute(f"INSERT INTO assets ({','.join(stopped_row)}) VALUES ({','.join('?' for _ in stopped_row)})",tuple(stopped_row.values()))
        collection=ET.Element('CoalescedObject',x='8',y='2',z='4')
        for offset in ('2','6'):
         wrapper=ET.SubElement(collection,'SceneObjectGroup',offsetx=offset,offsety='1',offsetz='2')
         group=ET.fromstring(stored['Data'])
         group.find('./RootPart/SceneObjectPart/Name').text='Coalesced '+offset
         wrapper.append(group)
        stored.update(UUID=coalesced_asset,Data=ET.tostring(collection))
        db.execute(f"INSERT INTO assets ({','.join(stored)}) VALUES ({','.join('?' for _ in stored)})",tuple(stored.values()))
        stored.update(UUID=linked_asset,Data=ET.tostring(linked))
        db.execute(f"INSERT INTO assets ({','.join(stored)}) VALUES ({','.join('?' for _ in stored)})",tuple(stored.values()))
       with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db, db:
        row=db.execute('SELECT * FROM inventoryitems WHERE inventoryID=?',(item_id,))
        stored=dict(zip((field[0] for field in row.description),row.fetchone()))
        stored.update(inventoryID=coalesced_item,assetID=coalesced_asset)
        db.execute(f"INSERT INTO inventoryitems ({','.join(stored)}) VALUES ({','.join('?' for _ in stored)})",tuple(stored.values()))
        stored.update(inventoryID=linked_item,assetID=linked_asset)
        db.execute(f"INSERT INTO inventoryitems ({','.join(stored)}) VALUES ({','.join('?' for _ in stored)})",tuple(stored.values()))
       process.stdin.write(coalesced_item+'\n'+linked_item+'\n');process.stdin.flush()
      if line.startswith('Linkset backup query '):
       prim=line.split()[3]
       expected=bytes([1,0x80,0x20,3])+b'\x07persist\x05value\x00\x05extra\x03yes\x00\x01z\x01x\x00'
       backup_deadline=time.monotonic()+10
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as db:
        while True:
         row=db.execute('SELECT typeof(lnkstBinData),lnkstBinData,PassTouches,AllowedDrop,'
                        'SitTargetOffsetX,SitTargetOffsetY,SitTargetOffsetZ,SitTargetOrientW,sitactrange,'
                        'standtargetx,standtargety,standtargetz,CameraEyeOffsetX,CameraAtOffsetZ,ForceMouselook '
                        'FROM prims WHERE UUID=?',(prim,)).fetchone()
         stopped=db.execute('SELECT p.UUID,i.itemID FROM prims p JOIN primitems i ON i.primID=p.UUID '
           'WHERE p.SceneGroupID=? AND p.UUID<>? AND i.assetID=?',(prim,prim,stopped_asset)).fetchone()
         if row==('blob',expected,1,1,.5,1.25,2,1,128,0,0,1.75,2,7,1) and stopped:break
         check(time.monotonic()<backup_deadline,f'Linkset data backup mismatch: {row!r}')
         time.sleep(.05)
       process.stdin.write(' '.join(stopped)+'\n');process.stdin.flush()
      if line.strip() in ('Temporary backup removed','Temporary backup restored'):
       expected=int(line.strip().endswith('restored'))
       backup_deadline=time.monotonic()+10
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        while True:
         counts=database.execute('SELECT (SELECT count(*) FROM prims WHERE UUID=:id), '
                                 '(SELECT count(*) FROM primshapes WHERE UUID=:id), '
                                 '(SELECT count(*) FROM primitems WHERE primID=:id)',{'id':temporary_prim}).fetchone()
         if counts==(expected,)*3:break
         check(time.monotonic()<backup_deadline,f'Temporary backup transition persisted: {counts!r}')
         time.sleep(.05)
        if expected:
         restored=database.execute('SELECT ExtraParams FROM primshapes WHERE UUID=?',(temporary_prim,)).fetchone()
         check(restored==(saved_shape[-1],),'Permanent backup restores the original binary extra parameters')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.advanced_profiles and line.strip() == 'Ready for grid kick':
       path,fields=offline_requests.get(timeout=5)
       check(path=='/offlineim' and fields=={'METHOD':['GET'],'PrincipalID':[session['agent_id']]},
             'Viewer retrieval calls the OfflineIM V2 service for its authenticated principal')
       path,fields=offline_requests.get(timeout=5)
       check(path=='/offlineim' and fields.get('METHOD')==['STORE'] and
             fields.get('FromAgentID')==[session['agent_id']] and
             fields.get('ToAgentID')==['94949494-9494-4494-8494-949494949494'] and
             fields.get('Message')==['Local IM'] and fields.get('BinaryBucket')==['00 01 FF'],
             'Undelivered viewer message and binary bucket are stored through OfflineIM V2')
       path,fields=offline_requests.get(timeout=5)
       check(path=='/offlineim' and fields.get('METHOD')==['STORE'] and
             fields.get('ToAgentID')==['95959595-9595-4595-8595-959595959595'],
             'Viewer attempts offline storage before reporting service rejection')
       remote=proxy.login_to_simulator({**credentials,'first':'Remote','last':'Recipient'})
       check(remote.get('login')=='true','Remote message recipient has a grid session')
       received=queue.Queue()
       class GridReceiver(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
         received.put(xmlrpc.client.loads(self.rfile.read(int(self.headers['Content-Length']))))
         body=xmlrpc.client.dumps(({'success':'TRUE'},),methodresponse=True).encode()
         self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
       def grid_form(path,**fields):
        with urllib.request.urlopen(url.rstrip('/')+path,urllib.parse.urlencode(fields).encode(),timeout=10) as reply:
         return ET.fromstring(reply.read())
       with http.server.ThreadingHTTPServer(('127.0.0.1',0),GridReceiver) as receiver:
        worker=threading.Thread(target=receiver.serve_forever,daemon=True);worker.start()
        try:
         remote_region='92929292-9292-4292-8292-929292929292'
         grid_form('/grid',METHOD='register',VERSIONMIN='1',VERSIONMAX='1',uuid=remote_region,regionName='Remote IM Region',
                   locX='25856',locY='25600',sizeX='256',sizeY='256',serverPort='9000',serverIP='127.0.0.1',
                   serverURI=f'http://127.0.0.1:{receiver.server_port}/')
         grid_form('/presence',METHOD='report',SessionID=remote['session_id'],RegionID=remote_region)
         process.stdin.write(remote['agent_id']+'\n');process.stdin.flush()
         params,method=received.get(timeout=10)
         outgoing=params[0]
         check(method=='grid_instant_message' and outgoing['from_agent_id']==session['agent_id'] and
               outgoing['to_agent_id']==remote['agent_id'] and outgoing['message']=='Local IM' and
               outgoing['message_key']=='native-kick-fixture','Viewer IM routes through grid presence to the remote region')
        finally:receiver.shutdown();worker.join(timeout=5)
       kick=dict(from_agent_id='6571e388-6218-4574-87db-f9379718315e',
                 to_agent_id=session['agent_id'],dialog='+g==',message='New login detected',message_key='wrong-key',
                 im_session_id='00000000-0000-0000-0000-000000000000',timestamp='0',from_agent_name='GRID',
                 from_group='FALSE',offline='AA==',parent_estate_id='0',position_x='0',position_y='0',
                 position_z='0',region_id='00000000-0000-0000-0000-000000000000',binary_bucket='AA==')
       check(proxy.grid_instant_message(kick)['success']=='FALSE','Wrong grid message key is rejected')
       kick['message_key']='native-kick-fixture'
       incomplete=dict(kick);del incomplete['binary_bucket']
       check(proxy.grid_instant_message(incomplete)['success']=='FALSE','Incomplete grid kick is rejected')
       message=dict(kick,dialog='AA==',message='Grid message',binary_bucket='AAH/')
       check(proxy.grid_instant_message(message)['success']=='TRUE','Authenticated grid message reaches the viewer circuit')
       check(proxy.grid_instant_message(kick)['success']=='TRUE','Authenticated grid kick disconnects the admitted viewer')
       check(proxy.grid_instant_message(kick)['success']=='FALSE','Disconnected viewer no longer accepts a grid kick')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.disable_scripts and line.strip()=='Enable scene scripting':
       proc.stdin.write('debug scene set scripting true\n');proc.stdin.flush()
       deadline=time.monotonic()+5
       while True:
        output=server_lines.get(timeout=max(.1,deadline-time.monotonic()))
        if 'Scene scripting enabled' in output:break
        check(time.monotonic()<deadline,'Console acknowledges scripting enable')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.disable_scripts and line.strip()=='Disable scene scripting':
       proc.stdin.write('debug scene set scripting false\n');proc.stdin.flush()
       deadline=time.monotonic()+5
       while True:
        output=server_lines.get(timeout=max(.1,deadline-time.monotonic()))
        if 'Scene scripting disabled' in output:break
        check(time.monotonic()<deadline,'Console acknowledges scripting disable')
       process.stdin.write('OK\n');process.stdin.flush()
      if a.assets and (match:=re.fullmatch(r'Task script asset ([0-9a-f-]{36})\s*',line)):
       script_id=match[1]
       status,data,_=asset_fetch('ViewerAsset','script_id='+script_id)
       check(status==200 and data in [default_script,expected_script],'Contents script asset matches actual executed text while session is active: '+script_id+' status='+str(status)+' bytes='+str(len(data)))
       if data==expected_script:phase_payload_seen+=1
       script_ids.append(script_id);process.stdin.write('OK\n');process.stdin.flush()
      if a.bakes and line.strip()=='Baked cache fixture':
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
       process.stdin.write(reply['new_asset']+'\n');process.stdin.flush()
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
      if match:=re.fullmatch(r'Prim (material|selection) query (\d+)\s*',line):
       status,payload=post(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>','application/llsd+xml')
       events=decode(ET.fromstring(payload)[0])['events']
       parts=[part for event in events if event['message']=='ObjectPhysicsProperties' for part in event['body']['ObjectData']]
       custom=[part for part in parts if part['LocalID']==int(match[2]) and part['PhysicsShapeType']==2
               and float(part['Density'])==1234 and float(part['GravityMultiplier'])==-.5]
       if match[1]=='material':check(status==200 and any(float(part['Friction'])==.25 and float(part['Restitution'])==.75 for part in custom),
             'Extra physics edit reports custom properties through EventQueueGet')
       check(any(abs(float(part['Friction'])-.2)<1e-6 and abs(float(part['Restitution'])-.7)<1e-6 for part in custom),
             match[1]+' reply preserves custom shape, density and gravity')
       process.stdin.write('OK\n');process.stdin.flush()
      if match := re.fullmatch(r'Top script query ([0-9a-f-]{36})\s*', line):
       ack = '<undef/>'
       script_row = None
       for _ in range(5):
        status, payload = post(session_caps['EventQueueGet'],
            f'<llsd><map><key>ack</key>{ack}<key>done</key><boolean>0</boolean></map></llsd>'.encode())
        check(status == 200, 'Top scripts arrive through the event queue')
        batch = decode(ET.fromstring(payload)[0])
        ack = f'<integer>{batch["id"]}</integer>'
        for event in batch['events']:
         if event['message'] != 'LandStatReply':
          continue
         for row, extended in zip(event['body'].get('ReportData', []), event['body'].get('DataExtended', [])):
          if row['TaskID'] != match[1]:
           continue
          rez_time = int.from_bytes(base64.b64decode(extended['TimeStamp']), 'big')
          check(abs(time.time() - rez_time) < 120, 'The top script report includes a recent rez timestamp')
          check(float(extended['Size']) >= 32, 'The top script report includes the script global heap in bytes')
          check(extended['PublicURLs'] == (0 if a.script_url_denied else 2),
                'The top script report includes the active URL count')
          script_row = row
          break
        if script_row:
         break
       check(script_row is not None and float(script_row['Score']) > 0,
             'The active script is reported under its object ID with a positive execution score')
       process.stdin.write('OK\n')
       process.stdin.flush()
      if line.startswith('Script HTTP denied '):
       check(a.script_url_denied and line.strip() == 'Script HTTP denied '
             'Could not resolve ExternalHostNameForLSL, HTTP listener for LSL disabled|0',
             'An unusable advertised address denies URL requests and reports zero free URLs')
       process.stdin.write('HTTP complete\n')
       process.stdin.flush()
      if match := re.fullmatch(
          rf'Script HTTP URLs (http://{re.escape(script_host)}:\d+/lslhttp/[0-9a-f-]{{36}}/) '
          rf'(https?://{re.escape(script_host)}:\d+/lslhttps?/[0-9a-f-]{{36}}/)(?: ([^\r\n]*))?\s*', line):
       if a.script_url_quota:
        check(match[3] == 'Too many URLs already open|0',
              'The configured URL quota denies the third request and reports zero free slots')
        reset_url = next((entry.split()[-1] for entry in lines if entry.startswith('Script reset URL ')), None)
        check(reset_url is not None and post(reset_url, b'after reset', 'text/plain')[0] == 404,
              'Default state entry self-reset revokes the URL but preserves the queued grant event')
       script_object = next(entry.split()[-1] for entry in lines if entry.startswith('Primary viewer prim '))
       request = urllib.request.Request(
           match[1] + 'fixture?q=123', b'payload', {'X-Test': 'header', 'Content-Type': 'text/plain'})
       with urllib.request.urlopen(request, timeout=8) as response:
        check(response.status == 201 and response.read() == b'payload|header|/fixture|q=123',
              'Script HTTP URL accepts POST and exposes the original headers and body')
        check(response.headers.get_content_type() == 'application/json' and
              response.headers['X-SecondLife-Object-Key'] == script_object and
              response.headers['X-SecondLife-Owner-Key'] == session['agent_id'] and
              response.headers['X-SecondLife-Region'].startswith('Connected Native (') and
              response.headers['X-SecondLife-Shard'] == 'Native Fixture' and
              response.headers['User-Agent'] == 'Native Fixture Agent' and
              response.headers['Access-Control-Allow-Origin'] == '*',
              'Script HTTP replies include the configured MIME type and simulator metadata')
       for user_agent, content_type in [('External Browser', 'text/plain'), ('SecondLife', 'text/html')]:
        html_request = urllib.request.Request(match[1], b'html', {'User-Agent': user_agent})
        with urllib.request.urlopen(html_request, timeout=8) as response:
         check(response.headers.get_content_type() == content_type,
               'HTML requires the embedded viewer browser and the owner connection address')
       legacy_body = '<>&"\'\u00a0\u00e9\u0100\u4e2d\U0001f600'
       legacy_request = urllib.request.Request(
           match[1], legacy_body.encode(), {'User-Agent': 'Mozilla (mSiE 8.0)', 'X-Test': 'legacy'})
       with urllib.request.urlopen(legacy_request, timeout=8) as response:
        expected = '<html>&lt;&gt;&amp;&quot;&#39;&#160;&#233;\u0100\u4e2d&#128512;|legacy|/|</html>'
        check(response.headers.get_content_type() == 'text/plain' and response.read() == expected.encode(),
              'Legacy browser text responses use the original HTML wrapper and entity encoding')
       json_request = urllib.request.Request(match[1], b'<tag>', {'User-Agent': 'MSIE'})
       with urllib.request.urlopen(json_request, timeout=8) as response:
        check(response.headers.get_content_type() == 'application/json' and response.read() == b'<tag>||/|',
              'Legacy browser wrapping applies only to plain-text replies')
       raw_query = 'first=1&flag&FIRST=2&=empty-key&escaped=x%26y%3Dz%2B+&empty=&legacy=%u00E9&'
       query_request = urllib.request.Request(match[1] + 'fields?' + raw_query, b'query')
       with urllib.request.urlopen(query_request, timeout=8) as response:
        expected = 'query||/fields|first=1,2&flag,&empty-key&escaped=x&y=z+ &empty=&legacy=%u00E9'
        check(response.read() == expected.encode(),
              'Script HTTP query headers group decoded keys separately from the path')
       if a.script_url_quota:
        target = urllib.parse.urlsplit(match[1])
        with contextlib.closing(http.client.HTTPConnection(target.hostname, target.port, timeout=8)) as conn:
         conn.putrequest('POST', target.path)
         for name, value in [
             ('Content-Length', '7'), ('Connection', 'close'), ('Accept', 'text/plain'),
             ('X-Requested-With', 'XMLHttpRequest'), ('Http_X_Requested_With', 'XMLHttpRequest'),
             ('Remote_Addr', '203.0.113.1'), ('Forwarded', 'for=203.0.113.2'),
             ('X-Forwarded-For', 'short'), ('x-forwarded-for', '203.0.113.3'),
             ('X-Forwarded-For', 'short'), ('X-Forwarded-For', '203.0.113.4'),
             ('X-Test', 'first'), ('x-test', 'second'),
         ]:
          conn.putheader(name, value)
         conn.endheaders(b'headers')
         with conn.getresponse() as response:
          check(response.status == 200 and response.read() == b'||||||||203.0.113.3,203.0.113.4|first,second',
                'Script HTTP hides parser-owned headers and preserves accepted duplicate values')
        for suffix, expected in [
            ('?q=123', b'query|||q=123'),
            ('/escaped%20part/%7Evalue?q=123', b'query||/escaped%20part/~value|q=123'),
        ]:
         request = urllib.request.Request(match[1].removesuffix('/') + suffix, b'query')
         with urllib.request.urlopen(request, timeout=8) as response:
          check(response.status == 201 and response.read() == expected,
                'Script HTTP exposes the canonical escaped path and accepts a query without path info')
        with contextlib.closing(http.client.HTTPConnection(target.hostname, target.port, timeout=8)) as conn:
         conn.request('POST', '/prefix/..' + target.path + 'fixture?q=123', b'query')
         with conn.getresponse() as response:
          check(response.status == 201 and response.read() == b'query||/fixture|q=123',
                'Script HTTP selects the route after URI dot-segment normalization')
        for method, body, expected in [
            ('pRoPfInD', b'property', b'PROPFIND|property'),
            ('PURGE', None, b'PURGE|'),
            ('TRACE', None, b'TRACE|'),
            ('CONNECT', b'ordinary request', b'CONNECT|ordinary request'),
            ('PRI', None, b'PRI|'),
        ]:
         extension = urllib.request.Request(match[1], body, method=method)
         with urllib.request.urlopen(extension, timeout=8) as response:
          check(response.status == 202 and response.read() == expected,
                f'Script URL delivers {method} and normalizes the method name')
        with contextlib.closing(http.client.HTTPConnection(target.hostname, target.port, timeout=8)) as conn:
         conn.request('PROPFIND', '/prefix/..' + target.path, b'property')
         with conn.getresponse() as response:
          check(response.status == 202 and response.read() == b'PROPFIND|property',
                'Extension methods resolve dot segments before selecting the script URL')
        with contextlib.closing(http.client.HTTPConnection(target.hostname, target.port, timeout=8)) as conn:
         conn.request('PROPFIND', target.path.removesuffix('/') + '%2Ffixture')
         with conn.getresponse() as response:
          check(response.status == 400,
                'An escaped slash does not delimit a script URL for extension methods')
        for method in ('PROPFIND', 'CONNECT', 'PRI'):
         with contextlib.closing(http.client.HTTPConnection(target.hostname, target.port, timeout=8)) as conn:
          conn.request(method, '/health')
          with conn.getresponse() as response:
           check(response.status == 400, f'{method} remains unavailable outside the script URL paths')
        for encoding in ('utf-8-sig', 'utf-16', 'utf-32'):
         encoded = urllib.request.Request(match[1], 'café'.encode(encoding), {'Content-Type': 'text/plain'})
         with urllib.request.urlopen(encoded, timeout=8) as response:
          check(response.read() == 'café||/|'.encode(),
                f'Inbound script HTTP detects the {encoding} body BOM')

       tls_context = None
       secure_endpoint = match[2]
       if a.script_https:
        check(match[2].startswith('https://') and '/lslhttps/' in match[2],
              'A single script receives a distinct HTTPS URL alongside its plain URL')
        plain_origin = urllib.parse.urlsplit(match[1])
        secure_url = urllib.parse.urlsplit(match[2])
        if a.script_https_external:
         secure_endpoint = urllib.parse.urlunsplit(secure_url._replace(scheme='http'))
         connection = http.client.HTTPConnection(secure_url.hostname, secure_url.port, timeout=8)
        else:
         tls_context = ssl._create_unverified_context()
         connection = http.client.HTTPSConnection(secure_url.hostname, secure_url.port, timeout=8, context=tls_context)
        with contextlib.closing(connection) as secure_connection:
         secure_connection.connect()
         if not a.script_https_external:
          fixture_der = ssl.PEM_cert_to_DER_cert((tls_fixture / 'localhost-cert.pem').read_text(encoding='ascii'))
          check(secure_connection.sock.getpeercert(binary_form=True) == fixture_der,
                'The script HTTPS listener presents the configured certificate')
         secure_connection.request('POST', secure_url.path, b'secure', {'Content-Type': 'text/plain'})
         with secure_connection.getresponse() as response:
          check(response.status == 201 and response.read() == b'secure||/|' and
                response.headers['Accept-CH'] == 'UA' and response.headers['Access-Control-Allow-Origin'] == '*',
                'The HTTPS route serves the same script and carries the secure response headers')
        check(plain_origin.port != secure_url.port, 'The advertised URLs use separate bound ports')
        if a.primary_https:
         check(urllib.parse.urlsplit(url).port not in (plain_origin.port, secure_url.port),
               'Primary HTTPS, plain script HTTP and secondary script HTTPS use distinct ports')
        wrong_listener = urllib.parse.urlunsplit(plain_origin._replace(path=secure_url.path))
        check(post(wrong_listener, b'wrong listener', 'text/plain')[0] == 404,
              'A secure URL path is unavailable on the plain listener')

       def wait_for_script_reply(endpoint, action):
        start = time.monotonic()
        request = urllib.request.Request(endpoint, action, {'Content-Type': 'text/plain'})
        try:
         response = urllib.request.urlopen(request, timeout=35, context=tls_context)
        except urllib.error.HTTPError as error:
         response = error
        with response:
         return response.status, response.headers.get_content_type(), response.read(), time.monotonic() - start

       with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        release = executor.submit(wait_for_script_reply, match[1], b'release')
        timeout = executor.submit(wait_for_script_reply, secure_endpoint, b'timeout')
        released = release.result()
        expired = timeout.result()
       check(released[:3] == (200, 'text/html', b'') and released[3] >= 24,
             'An accepted request to a released URL follows the original empty poll response')
       check(expired[:3] == (500, 'text/plain', b'Script timeout') and expired[3] >= 24,
             'An unanswered request returns the original timeout response after the deadline')
       status, _ = post(match[1], b'after release', 'text/plain')
       check(status == 404, 'Releasing a script URL removes its route')
       if a.script_url_quota:
        check(post(match[2], b'free', 'text/plain') == (200, b'1'),
              'Releasing a URL restores one slot while the other URL remains live')
       process.stdin.write('HTTP complete\n' if a.primary_https or a.script_url_hostname or a.script_url_quota else 'OK\n')
       process.stdin.flush()
      if match:=re.fullmatch(r'Script running query ([0-9a-f-]{36}) ([0-9a-f-]{36}) ([01])\s*',line):
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       try:
        for _ in range(4):
         with urllib.request.urlopen(event_request,timeout=10) as response:event_body=response.read()
         if b'ScriptRunningReply' in event_body:break
       except Exception as error:raise AssertionError('GetScriptRunning reply failed for '+match[2]+'; probe tail: '+''.join(lines[-6:])) from error
       check(response.status==200 and b'ScriptRunningReply' in event_body and match[1].encode() in event_body and match[2].encode() in event_body and ('<key>Running</key><boolean>'+match[3]+'</boolean>').encode() in event_body and b'<key>Mono</key><boolean>1</boolean>' in event_body,'GetScriptRunning produces actual EventQueue script state')
       if a.disable_scripts:
        events=decode(ET.fromstring(event_body)[0])['events']
        replies=[entry for event in events if event['message']=='ScriptRunningReply'
                 for entry in event['body']['Script'] if entry['ItemID']==match[2]]
        check(len(replies)==1,'Only the enabled script-status request receives a reply')
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
       event_body=b""
       for _ in range(4):
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body+=response.read()
        if b"ScriptRunningReply" in event_body:break
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
       event_body=b""
       for _ in range(4):
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body+=response.read()
        if b"ScriptRunningReply" in event_body:break
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
       event_body=b""
       for _ in range(4):
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body+=response.read()
        if b"ScriptRunningReply" in event_body:break
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
       removed_task_items.update((match[2],match[3]))
       event_request=urllib.request.Request(session_caps['EventQueueGet'],b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',{'Content-Type':'application/llsd+xml'})
       event_body=b""
       for _ in range(4):
        with urllib.request.urlopen(event_request,timeout=10) as response:event_body+=response.read()
        if b"ScriptRunningReply" in event_body:break
       check(response.status==200 and b'ScriptRunningReply' in event_body and match[3].encode() in event_body and match[2].encode() not in event_body,'Ordinary LSL inventory removal terminates its engine; retyped removed script remains queryable')
       process.stdin.write('OK\n');process.stdin.flush()
      if edit_phase and line.startswith('Verified task Contents serial '):check(phase_payload_seen>=1,'Updated Contents points to exact newly stored script bytes')

     process.wait(timeout=5);check(process.returncode==0,'Connected viewer probe: '+''.join(lines)+''.join(errors))
     if a.primary_https or a.script_url_denied or a.script_url_hostname or a.script_url_quota:
      return
     if rotating_prim and not a.disable_scripts:
      check(foreign_key_calls == [{'first': 'Foreign', 'last': 'Avatar'}],
            'Hypergrid user-key requests reuse the successful remote result')
      purchase=next(line.split() for line in lines if line.startswith('Purchased object asset '))
      asset_id,item_id=purchase[3:]
      with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:
       item=db.execute('SELECT assetID,assetType,invType,avatarID FROM inventoryitems WHERE inventoryID=?',(item_id,)).fetchone()
       check(db.execute('SELECT 1 FROM inventoryitems WHERE inventoryID=?',(coalesced_item,)).fetchone() is None,
             'Coalesced rez consumes its single no-copy inventory item')
      check(item is None,'Rezzing the no-copy purchase consumes its inventory item')
      with contextlib.closing(sqlite3.connect(root/'assets.db')) as db:
       stored=db.execute('SELECT Type,Data FROM assets WHERE UUID=?',(asset_id,)).fetchone()
      check(stored is not None and stored[0]==6,'Purchased object asset persisted as type Object')
      group=ET.fromstring(stored[1]);part=group.find('./RootPart/SceneObjectPart')
      check(group.tag=='SceneObjectGroup' and part is not None and group.find('OtherParts') is not None,'Purchased object uses original linkset XML')
      check(part.findtext('Name')=='Scripted Prim' and part.findtext('ObjectSaleType')=='0' and part.findtext('SalePrice')=='10' and part.findtext('ClickAction')=='0','Stored copy preserves the prim name and resets its sale settings')
      check(part.findtext('PassTouches') == 'true', 'Stored copy preserves the scripted pass-touches setting')
      check(part.findtext('AllowedDrop') == 'true', 'Stored copy preserves script-enabled inventory drops')
      check(float(part.findtext('SitActRange')) == 128, 'Stored copy preserves the capped sit range')
      check([float(part.findtext(f'StandTarget/{axis}')) for axis in 'XYZ'] == [0,0,1.75],
            'Stored copy preserves the stand target')
      check([float(part.findtext(f'CameraEyeOffset/{axis}')) for axis in 'XYZ'] == [2,3,4]
            and [float(part.findtext(f'CameraAtOffset/{axis}')) for axis in 'XYZ'] == [5,6,7],
            'Stored copy preserves both seat camera offsets')
      for suffix in ('', 'LL'):
       check([float(part.findtext(f'SitTargetPosition{suffix}/{axis}')) for axis in 'XYZ'] == [.5,1.25,2]
             and [float(part.findtext(f'SitTargetOrientation{suffix}/{axis}')) for axis in 'XYZW'] == [0,0,0,1],
             'Stored copy preserves sit targets and normalizes the zero rotation')
      check(part.findtext('./Shape/TextureEntry') and part.find('./TaskInventory/TaskInventoryItem/AssetID/UUID') is not None,'Stored copy preserves texture data and task asset references')
      check(part.findtext('Text')=='Script label' and
            [part.findtext(f'Color/{channel}') for channel in 'RGBA']==['255','127','0','127'],
            'Stored copy preserves floating text and ARGB channels')
      check([part.findtext(f'PayPrice{i}') for i in range(1,3)]==['5','10'] and
            part.findtext('SoundRadius')=='31.5' and part.findtext('SoundQueueing')=='true',
            'Stored copy preserves payment buttons and sound settings')
     primary_viewer_prim=next((line.split()[-1] for line in lines if line.startswith('Primary viewer prim ')),None)
     if not a.advanced_profiles:check(any('PASS: default user info reply before root' in line for line in lines),'Default user preferences arrive over UDP before root')
     if a.profiles_disabled:check(any('PASS: configured UserProfiles disables basic replies' in line for line in lines),'Configured UserProfiles section suppresses basic avatar replies')
     elif a.advanced_profiles:
      check(any('PASS: advanced profile picks before root' in line for line in lines),'Configured advanced profile picks arrive over UDP before root')
      check(any('PASS: advanced profile properties from worker' in line for line in lines),'Background worker returns SQLite avatar properties')
      check(any('PASS: advanced profile note round trip' in line for line in lines),'Viewer note update and read round-trip over UDP')
      with contextlib.closing(sqlite3.connect(root/'profiles.db')) as db:
       row=db.execute('SELECT notes FROM usernotes WHERE useruuid=? AND targetuuid=?',
                      (session['agent_id'],session['agent_id'])).fetchone()
       preferences=db.execute('SELECT imviaemail,visible FROM usersettings WHERE useruuid=?',
                              (session['agent_id'],)).fetchone()
      check(row==('Note\né',),'Viewer note preserves Unicode and newlines in SQLite')
      check(tuple(str(value).lower() for value in preferences) in (('true','false'),('1','0')),
            'Viewer forwarding and hidden-directory preferences persist in SQLite')
     else:
      check(any('PASS: unknown avatar properties/interests before root' in line for line in lines),'Unknown avatar properties and interests arrive before root')
      check(any('PASS: account avatar properties/interests' in line for line in lines),'Local account properties and interests arrive over authenticated UDP')
     if a.disable_scripts:return
     if a.assets:check(len(set(script_ids))>=1,'Actual task script asset IDs returned through Contents/Xfer')
     if a.legacy:
      with contextlib.closing(sqlite3.connect(root/'inventory.db')) as db:row=db.execute('SELECT inventoryCurrentPermissions,creatorID FROM inventoryitems WHERE inventoryID=?',(script_source_id,)).fetchone()
      check(row is not None and row[0]==0x7fff7fff and row[1].startswith(session['agent_id']+';'),'Existing no-copy user item remains stored exactly as source rez path')
    finally:
     if process.poll() is None:process.kill();process.wait(timeout=5)
     reader.join(timeout=5);process.stdout.close();process.stdin.close()

   if a.touch:
    with contextlib.closing(sqlite3.connect(root/'assets.db')) as database, database:
     landmark=b'Landmark version 2\nregion_id 11111111-1111-4111-8111-111111111111\nlocal_pos 125 126 30\nregion_handle 1099511628032000\n'
     database.execute('INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) VALUES(?,?,?,?,?,?,?,?,?)',
                      ('84848484-8484-4484-8484-848484848484','Teleport fixture','',3,0,0,0,login['agent_id'],landmark))
   if a.physics_library:
    observer=proxy.login_to_simulator(dict(credentials,first='Physics',last='Observer',
                                         start='uri:Connected Native&190&190&25'))
    check(observer.get('login')=='true','Physics region accepts a second avatar')
    observer_agent=uuid.UUID(observer['agent_id']).bytes
    observer_session=uuid.UUID(observer['session_id']).bytes
    observer_code=struct.pack('<I',int(observer['circuit_code']) & 0xffffffff)
    endpoint=(observer['sim_ip'],observer['sim_port'])
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
     listener.settimeout(5)
     listener.sendto(struct.pack('>BIB',0x40,1,0)+bytes.fromhex('ffff0003')+
                     observer_code+observer_session+observer_agent,endpoint)
     listener.sendto(struct.pack('>BIB',0x40,2,0)+bytes.fromhex('ffff00f9')+
                     observer_agent+observer_session+observer_code,endpoint)
     for _ in range(64):
      if listener.recv(4096)[6:10]==bytes.fromhex('ffff00fa'):break
     else:raise AssertionError('Observer failed to enter the physics region')
     listener.sendto(struct.pack('>BIBHH',0x40,3,0,0xffff,84)+observer_agent+observer_session+
                     struct.pack('<IfffBHB',1,0.45,0.6,1.95,0,0,3)+bytes((17,103,239)),endpoint)
     hover_seed=urllib.request.Request(observer['seed_capability'],b'<llsd><array><string>AgentPreferences</string></array></llsd>',{'Content-Type':'application/llsd+xml'})
     with urllib.request.urlopen(hover_seed,timeout=5) as response:
      hover_caps=list(ET.fromstring(response.read()).find('map'))
     hover_url={hover_caps[i].text:hover_caps[i+1].text for i in range(0,len(hover_caps),2)}['AgentPreferences']
     with urllib.request.urlopen(urllib.request.Request(hover_url,preference_body,{'Content-Type':'application/llsd+xml'}),timeout=5) as response:
      check(response.status==200,'Connected avatar accepts hover preference update')
     with contextlib.closing(sqlite3.connect(root/'remote-preferences.db')) as db:
      saved_prefs=db.execute('SELECT HoverHeight,Language,LanguageIsPublic FROM AgentPrefs WHERE PrincipalID=?',
                             (observer['agent_id'],)).fetchone()
     check(saved_prefs==(0.125,'fr-ca',0),'Native grid service stores region hover and language privacy fields')
     probe(login,observer['agent_id'],verify_hover=True)
     if a.bakes:
      with urllib.request.urlopen(url.rstrip('/')+'/bakes/'+login['agent_id'],timeout=5) as stored:
       baked=ET.fromstring(stored.read())
      check(baked.tag=='BakedAppearance' and [(entry.tag,entry.get('TextureIndex')) for entry in baked]==
            [('BakedTexture','8'),('BakedTexture','9'),('BESetA','40')] and
            all(entry.get('CacheId')=='890172ac-bab4-4b53-b553-7c0775014fee' and
                entry.findtext('AssetBase/FullID/Guid')==baked_ids[-1] and
                base64.b64decode(entry.findtext('AssetBase/Data'))==b'Actual native bake\x00\xff' for entry in baked),
            'Region publishes ordinary and extended baked assets in XBakes XML')
     with contextlib.closing(sqlite3.connect(root/'avatars.db')) as db:
      deadline=time.monotonic()+7
      while True:
       saved=db.execute('SELECT Value FROM Avatars WHERE PrincipalID=? AND Name=?',
                        (observer['agent_id'],'VisualParams')).fetchone()
       if saved==('17,103,239',) or time.monotonic()>=deadline:break
       time.sleep(.1)
      check(saved==('17,103,239',),'Viewer appearance update persists in the avatar store')
     sender=uuid.UUID(login['agent_id']).bytes
     for _ in range(512):
      packet=listener.recv(4096)
      if packet[6]==20 and packet[7:23]==sender:break
     else:raise AssertionError('Physics observer did not receive the other avatar animation')
     listener.sendto(struct.pack('>BIBHH',0x40,4,0,0xffff,252)+observer_agent+observer_session,endpoint)
   else:
    if a.touch and not a.disable_scripts:
     with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as observer_socket, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as mover_socket:
      def connect_privacy_viewer(sock,last,x,session=None,expected_arrival=None):
       verify_estate=session is None
       if session is None:
        session=proxy.login_to_simulator(dict(credentials,first='Privacy',last=last,
                                             start=f'uri:Connected Native&{x}&128&25'))
       check(session.get('login')=='true',f'Parcel privacy {last} can log in')
       endpoint=(session['sim_ip'],session['sim_port'])
       agent=uuid.UUID(session['agent_id']).bytes
       session_id=uuid.UUID(session['session_id']).bytes
       code=struct.pack('<I',int(session['circuit_code']) & 0xffffffff)
       sequence=0
       def send(payload,reliable=True):
        nonlocal sequence
        sequence+=1
        sock.sendto(struct.pack('>BIB',0x40 if reliable else 0,sequence,0)+payload,endpoint)
       def receive(predicate,timeout=5):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
         sock.settimeout(max(.01,deadline-time.monotonic()))
         packet=sock.recv(8192)
         if packet[0]&0x40:
          send(bytes.fromhex('fffffffb01')+struct.pack('<I',struct.unpack_from('>I',packet,1)[0]),False)
         payload=packet[6+packet[5]:]
         if packet[0]&0x80:
          decoded=bytearray()
          index=0
          while index<len(payload):
           value=payload[index]
           index+=1
           if value:decoded.append(value)
           elif index<len(payload):
            decoded.extend(bytes(payload[index]))
            index+=1
          payload=decoded
         result=predicate(payload)
         if result is not None:return result
        raise AssertionError(f'Parcel privacy {last} packet missing')
       send(bytes.fromhex('ffff0003')+code+session_id+agent)
       def estate_handshake(payload):
        if payload[:4]==bytes.fromhex('ffff0094'):
         offset=10+payload[9]
         flags=struct.unpack_from('<I',payload,4)[0]
         return payload[offset:offset+16],payload[offset+16],struct.unpack_from('<f',payload,offset+21)[0],flags
       estate=receive(estate_handshake)
       profile_restrictions = 0x40800000
       expected_restrictions = profile_restrictions if a.estate_profile_access else 0
       check(estate[3] & profile_restrictions == expected_restrictions,
             'Handshake advertises only enforced age and identity restrictions')
       if verify_estate:
        check(estate[:3]==(uuid.UUID(owner).bytes,int(last=='Mover'),.75),
              f'Region handshake sends stored estate details for {last}')
        check(estate[3]&0x501082b0==0x00108290,'Handshake sends stored flags including fixed sun')
        send(bytes.fromhex('ffff008d')+agent+session_id)
        def region_menu(payload):
         if payload[:4]==bytes.fromhex('ffff008e'):
          offset=37+payload[36]
          return (payload[4:36],struct.unpack_from('<II',payload,offset),
                  struct.unpack_from('<f',payload,offset+14)[0],struct.unpack_from('<iii',payload,offset+34),
                  struct.unpack_from('<I',payload,offset+8)[0])
        menu=receive(region_menu)
        check(menu[:4]==(agent+session_id,(100,1),.75,(9,1024,2048)),
              f'Estate menu returns stored region details for {last}')
        check(menu[4]&0x501082b0==0x280,'Estate menu sends its own flag set without handshake-only bits')
        send(bytes.fromhex('ffff00cb')+agent+session_id)
        def covenant_reply(payload):
         if payload[:4]==bytes.fromhex('ffff00cc'):
          name_end=25+payload[24]
          return (payload[4:20],struct.unpack_from('<I',payload,20)[0],
                  payload[25:name_end].rstrip(b'\0'),payload[name_end:name_end+16])
        check(receive(covenant_reply)==(uuid.UUID('99887766-5544-4332-8110-010203040506').bytes,
                                       1700000123,b'Fixture Estate',uuid.UUID(owner).bytes),
              f'Viewer receives the stored estate covenant for {last}')
       def negotiate_terrain_pbr(capability):
        seed = urllib.request.Request(
            session['seed_capability'], f'<llsd><array><string>{capability}</string></array></llsd>'.encode(),
            {'Content-Type': 'application/llsd+xml'})
        with urllib.request.urlopen(seed, timeout=5) as response:
         response.read()
       if a.pbr_terrain and not a.late_pbr_terrain and last == 'Mover':
        negotiate_terrain_pbr('VETPBR' if a.extended_pbr_terrain else 'VTPBR')
       send(bytes.fromhex('ffff00f9')+agent+session_id+code)
       arrival = receive(lambda p: struct.unpack_from('<6f', p, 36)
                         if p[:4] == bytes.fromhex('ffff00fa') else None)
       if a.late_pbr_terrain and last == 'Mover':
        negotiate_terrain_pbr('VTPBR')
       if a.extended_pbr_terrain and last == 'Observer':
        negotiate_terrain_pbr('VTPBR')
       if a.varregion_relocation and last == 'Observer':
        check(arrival[:3] == (300.5, 128.5, 25.0),
              f'Viewer enters a nonrectangular parcel outside the legacy region footprint: {arrival}')
       elif a.parcel_relocation and last == 'Observer':
        check(arrival[:3] == (132.5, 128.5, 25.0),
              f'Viewer enters the nearest allowed parcel half a meter inside the border: {arrival}')
       elif a.telehub_parcel_access and not a.script_parcel_reset and last == 'Observer':
        expected_x = 128.0 if a.estate_profile_access else 136.0
        check(arrival[:2] == (expected_x, 128.0), f'Viewer arrives at the permitted spawn: {arrival}')
       if a.sl_landing:
        check(arrival == (132.5, 128.25, 25.0, 0.0, 1.0, 0.0),
              'SL landing mode sends the saved parcel position and look direction')
       if expected_arrival is not None:
        check(arrival == expected_arrival, f'Default landing position and look for {last}: {arrival}')
       return agent,session_id,send,receive,session['seed_capability']
      if a.telehub_parcel_access:
       start = 'uri:Connected Native&128&128&25'
       if a.script_parcel_reset:
        deadline = time.monotonic() + 8
        while True:
         admitted = proxy.login_to_simulator(dict(credentials, first='Privacy', last='BanTarget', start=start))
         if admitted.get('login') == 'true':
          break
         check(time.monotonic() < deadline, 'Scripted reset lifts the live ban')
         time.sleep(.1)
        agent, session, send, receive, _ = connect_privacy_viewer(
            observer_socket, 'BanTarget', 128, admitted)
        send(bytes.fromhex('ffff00d7') + agent + session + struct.pack('<iIi', 0, 3, 1))

        def empty_list_flag(payload):
         if payload[:4] != bytes.fromhex('ffff00d8'):
          return None
         flag, parcel_id = struct.unpack_from('<Ii', payload, 24)
         check(parcel_id == 1 and payload[32] == 1 and payload[33:49] == bytes(16),
               'Script reset empties the live parcel list')
         return flag

        check({receive(empty_list_flag), receive(empty_list_flag)} == {1, 2},
              'Both live lists are cleared')
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         saved = set(database.execute(
             'SELECT AccessUUID, Flags FROM landaccesslist WHERE LandUUID LIKE ?', ('a0000000%',)))
         check(saved == {(str(home_target), 1), (str(ban_target), 2), (parcel_observer, 2)},
               'Script resets leave SQLite entries until another update saves the parcel')
         flags = int(database.execute('SELECT LandFlags FROM land WHERE LocalLandID=1').fetchone()[0])
         check(flags & 1024 != 0, 'Script reset leaves the saved ban flag intact')
        print('PASS: scripted parcel reset changes live access without saving the parcel')
        return
       if a.script_parcel_access:
        deadline = time.monotonic() + 8
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         while not database.execute('SELECT 1 FROM landaccesslist WHERE AccessUUID=? AND Flags=1',
                                    (str(home_target),)).fetchone():
          check(time.monotonic() < deadline, 'Restored script finishes its parcel edits')
          time.sleep(.05)
       denied = proxy.login_to_simulator(dict(credentials, first='Privacy', last='BanTarget', start=start))
       check(denied.get('login') == 'false', 'Login rejects an agent denied at every telehub spawn')
       if a.estate_profile_access:
        unidentified = proxy.login_to_simulator(dict(credentials, first='Privacy', last='HomeTarget', start=start))
        check(unidentified.get('login') == 'false', 'Estate rejects age-verified accounts missing identification')
       observer_login = proxy.login_to_simulator(
           dict(credentials, first='Privacy', last='Observer', start=start))
       agent, session, send, receive, _ = connect_privacy_viewer(
           observer_socket, 'Observer', 128, observer_login)
       if a.parcel_profile_access:
        print('PASS: identity and age flags control estate admission and parcel routing')
        return
       if a.script_parcel_denied:
        root_id = uuid.UUID(stored_root).bytes

        def telehub_object(payload):
         if len(payload) >= 33 and payload[0] == 12 and payload[17:33] == root_id:
          return struct.unpack_from('<I', payload, 12)[0]

        object_id = receive(telehub_object)
        send(bytes.fromhex('ffff0075') + agent + session + struct.pack('<I3fB', object_id, 0, 0, 0, 0))
        receive(lambda payload: True if b'Unauthorized parcel edit script finished' in payload else None)
       for local_id, expected in (
           (1, {1: {home_target.bytes if a.script_parcel_access or a.script_group_parcel_access else
                    timeout_target.bytes if a.script_parcel_denied else bytes(16)},
                2: {cancel_target.bytes} if a.script_group_parcel_ban else {agent, ban_target.bytes}}),
           (2, {1: {agent}, 2: {bytes(16)}}),
       ):
        send(bytes.fromhex('ffff00d7') + agent + session + struct.pack('<iIi', 23, 1, local_id))

        def access_reply(payload):
         if payload[:4] != bytes.fromhex('ffff00d8'):
          return None
         sequence, flag, parcel_id = struct.unpack_from('<iIi', payload, 20)
         if parcel_id != local_id:
          return None
         check(payload[4:20] == agent and sequence == 0, 'Parcel access reply identifies the viewer')
         entries = set()
         for index in range(payload[32]):
          offset = 33 + index * 24
          entry_id, expires, entry_flag = struct.unpack_from('<16siI', payload, offset)
          check(expires == 0 and entry_flag == flag, 'Saved access entries are permanent and typed')
          entries.add(entry_id)
         return flag, entries

        replies = dict(receive(access_reply) for _ in range(2))
        check(replies == expected, 'Viewer receives both saved lists and the empty-list placeholder')
       if a.varregion_relocation:
        selected_parcel = -1 if a.public_terrain else 2
        strength = 250 if a.public_terrain else 2
        brush = struct.pack('<BBffBi4fBf', 1, 1, strength, 0, 1, selected_parcel, 128, 128, 304, 129, 1, 2)
        if a.group_terrain:
         send(bytes.fromhex('ffff007c') + agent + session + brush)
         send(bytes.fromhex('ffff00d7') + agent + session + struct.pack('<iIi', 0, 1, 2))
         receive(access_reply)
         with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
          check(database.execute('SELECT Revision FROM terrain').fetchone() == (22,),
                'Unrelated group power does not permit terraforming')
         parcel_member_powers |= 1 << 23
        else:
         forbidden_brush = struct.pack('<BBffBi4fBf', 1, 1, 2, 0, 1, 1, 128, 128, 130, 129, 1, 2)
         send(bytes.fromhex('ffff007c') + agent + session + forbidden_brush)
        time.sleep(.05)
        send(bytes.fromhex('ffff007c') + agent + session + brush)
        deadline = time.monotonic() + 12
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         while True:
          revision, terrain_data = database.execute('SELECT Revision,Heightfield FROM terrain').fetchone()
          if revision == 23:
           raw = gzip.decompress(terrain_data)
           width, height = struct.unpack_from('<II', raw)
           check((width, height) == (512, 512), 'Saved terrain retains the configured region dimensions')

           def height_at(x, y):
            return struct.unpack_from('<f', raw, 8 + 4 * (y * width + x))[0]

           if height_at(300, 128) == (96.0 if a.public_terrain else 23.0):
            check(height_at(128, 128) == 21.0 and height_at(299, 128) == 21.0,
                  'Parcel terrain brush skips the neighboring parcel and the gap')
            break
          check(time.monotonic() < deadline, 'Viewer terrain brush updates the selected varregion parcel')
          time.sleep(.05)
        if a.public_terrain:
         owner_id, owner_session, owner_send, owner_receive, _ = connect_privacy_viewer(
             mover_socket, 'Owner', 128, login)
         admin_brush = struct.pack('<BBffBi4fBf', 1, 1, 250, 0, 1, 2, 300, 128, 304, 129, 1, 2)
         owner_send(bytes.fromhex('ffff007c') + owner_id + owner_session + admin_brush)
         deadline = time.monotonic() + 12
         with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
          while True:
           terrain_data = database.execute('SELECT Heightfield FROM terrain WHERE Revision=23').fetchone()[0]
           raw = gzip.decompress(terrain_data)
           if height_at(300, 128) == 346.0:
            break
           check(time.monotonic() < deadline, 'Administrator terrain edit exceeds the estate raise limit')
           time.sleep(.05)
         def terrain_command(viewer_id, session_id, *parameters):
          encoded = b''.join(bytes([len(parameter) + 1]) + parameter + b'\0'
                             for parameter in parameters)
          return (bytes.fromhex('ffff0104') + viewer_id + session_id + bytes(16)
                  + b'\x08terrain\0' + uuid.uuid4().bytes + bytes([len(parameters)]) + encoded)

         def baked_height():
          with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
           blob = database.execute('SELECT Heightfield FROM bakedterrain').fetchone()[0]
          return struct.unpack_from('<f', gzip.decompress(blob), 8 + 4 * (128 * 512 + 300))[0]

         def denied_bake(viewer_id, session_id, viewer_send, viewer_receive):
          viewer_send(terrain_command(viewer_id, session_id, b'bake'))
          viewer_send(bytes.fromhex('ffff00d7') + viewer_id + session_id + struct.pack('<iIi', 0, 1, 2))
          viewer_receive(lambda payload: True if payload[:4] == bytes.fromhex('ffff00d8') else None)
          check(baked_height() == 21.0, 'Viewer lacks permission to bake the edited terrain')

         denied_bake(agent, session, send, receive)
         with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as manager_socket:
          manager_login = proxy.login_to_simulator(dict(credentials, first='Privacy', last='Mover', start=start))
          manager_viewer = connect_privacy_viewer(manager_socket, 'Mover', 128, manager_login)
          denied_bake(*manager_viewer[:4])
          manager_id, manager_session, manager_send, manager_receive, _ = manager_viewer
          manager_send(terrain_command(manager_id, manager_session, b'upload filename', b'terrain.r32'))
          transfer_id = manager_receive(lambda payload: struct.unpack_from('<Q', payload, 4)[0]
                                       if payload[:4] == bytes.fromhex('ffff009c') else None)
          def send_partial_chunk(index, data):
           manager_send(b'\x12' + struct.pack('<QIH', transfer_id, index, len(data)) + data)
           manager_receive(lambda payload: True if len(payload) >= 13 and payload[0] == 19 and
                           struct.unpack_from('<QI', payload, 1) == (transfer_id, index) else None)

          first_chunk = struct.pack('<I', 64) + bytes(16)
          send_partial_chunk(0, first_chunk)
          send_partial_chunk(0, first_chunk)
          abort = bytes.fromhex('ffff009d') + struct.pack('<Qi', transfer_id, 0)
          owner_send(abort)
          owner_send(terrain_command(owner_id, owner_session, b'upload filename', b'other.r32'))
          owner_receive(lambda payload: True if b'Another Terrain Upload is in progress' in payload else None)
          manager_send(bytes.fromhex('ffff009d') + struct.pack('<Qi', transfer_id + 1, 0))
          send_partial_chunk(1, bytes(16))
          manager_send(abort)
          manager_receive(lambda payload: True if b'Terrain Upload aborted by the client' in payload else None)
          check(baked_height() == 21.0, 'Aborting a partial upload preserves the baked terrain')
          manager_send(terrain_command(manager_id, manager_session, b'upload filename', b'interrupted.r32'))
          transfer_id = manager_receive(lambda payload: struct.unpack_from('<Q', payload, 4)[0]
                                       if payload[:4] == bytes.fromhex('ffff009c') else None)
          send_partial_chunk(0, first_chunk)
          manager_send(bytes.fromhex('ffff00fc') + manager_id + manager_session)
          manager_receive(lambda payload: True if payload[:4] == bytes.fromhex('ffff00fd') else None)

          def replacement_transfer(payload):
           if payload[:4] == bytes.fromhex('ffff009c'):
            return struct.unpack_from('<Q', payload, 4)[0]
           if b'Another Terrain Upload is in progress' in payload:
            return False
           return None

          deadline = time.monotonic() + 5
          while True:
           owner_send(terrain_command(owner_id, owner_session, b'upload filename', b'replacement.r32'))
           replacement = owner_receive(replacement_transfer)
           if replacement is not False:
            break
           check(time.monotonic() < deadline, 'Uploader logout releases the terrain upload slot')
           time.sleep(.05)
          check(replacement != transfer_id and baked_height() == 21.0,
                'A new upload starts after logout without baking the interrupted transfer')
          owner_send(bytes.fromhex('ffff009d') + struct.pack('<Qi', replacement, 0))
          owner_receive(lambda payload: True if b'Terrain Upload aborted by the client' in payload else None)
         with contextlib.closing(sqlite3.connect(root/'users.db')) as database, database:
          database.execute('UPDATE UserAccounts SET UserLevel=200 WHERE PrincipalID=?', (str(home_target),))
         with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as administrator_socket:
          administrator_login = proxy.login_to_simulator(
              dict(credentials, first='Privacy', last='HomeTarget', start=start))
          admin_id, admin_session, admin_send, admin_receive, _ = connect_privacy_viewer(
              administrator_socket, 'HomeTarget', 128, administrator_login)
          admin_send(terrain_command(admin_id, admin_session, b'bake'))
          admin_send(bytes.fromhex('ffff00d7') + admin_id + admin_session + struct.pack('<iIi', 0, 1, 2))
          admin_receive(lambda payload: True if payload[:4] == bytes.fromhex('ffff00d8') else None)
          check(baked_height() == 346.0, 'A grid administrator can bake without owning the estate')

          filename = b'varregion.raw'
          admin_send(terrain_command(admin_id, admin_session, b'download filename', filename))
          download = admin_receive(lambda payload: payload if payload[:4] == bytes.fromhex('ffff0193') else None)
          name_end = 21 + download[20]
          sim_name = download[21:name_end]
          check(download[4:20] == admin_id and download[name_end + 1:].rstrip(b'\0') == filename,
                'Terrain download identifies the viewer and requested filename')
          rates = struct.pack('<7f', 50000, 80000, 10000, 10000, 4000000, 100000, 80000)
          code = int(administrator_login['circuit_code']) & 0xffffffff
          admin_send(bytes.fromhex('ffff0051') + admin_id + admin_session +
                     struct.pack('<IIB', code, 0, len(rates)) + rates)
          transfer_id = 987654321
          admin_send(bytes.fromhex('ffff009c') + struct.pack('<QB', transfer_id, len(sim_name)) +
                     sim_name + bytes(21))

          def download_chunk(payload):
           if len(payload) < 15 or payload[0] != 18:
            return None
           identifier, packet, length = struct.unpack_from('<QIH', payload, 1)
           if identifier != transfer_id:
            return None
           return packet, payload[15:15 + length]

          expected_size = 512 * 512 * 13
          raw_download = bytearray()
          deadline = time.monotonic() + 35
          next_chunk = 0
          while True:
           packet, data = admin_receive(download_chunk)
           index = packet & 0x7fffffff
           if index > next_chunk:
            continue
           admin_send(b'\x13' + struct.pack('<QI', transfer_id, packet))
           if index < next_chunk:
            continue
           if index == 0:
            check(struct.unpack_from('<I', data)[0] == expected_size,
                  'Terrain transfer advertises the complete 512-meter map')
            data = data[4:]
           raw_download.extend(data)
           next_chunk += 1
           if packet & 0x80000000:
            break
           if time.monotonic() >= deadline:
            raise AssertionError('Timed out downloading the full variable-region terrain')
          check(len(raw_download) == expected_size, 'Terrain download includes every LLRAW sample')
          edited_sample = 13 * ((511 - 128) * 512 + 300)
          red, green = raw_download[edited_sample:edited_sample + 2]
          check(345.0 <= red * green / 128 <= 346.0,
                'Terrain download contains the edited height outside the original footprint')
          raw_download[edited_sample:edited_sample + 2] = bytes((64, 128))
          raw_download[edited_sample + 11:edited_sample + 13] = bytes((64, 128))
          admin_send(terrain_command(admin_id, admin_session, b'upload filename', filename))
          upload_id = admin_receive(lambda payload: struct.unpack_from('<Q', payload, 4)[0]
                                   if payload[:4] == bytes.fromhex('ffff009c') else None)
          upload_alerts = []

          def upload_alert(payload):
           if payload[:4] != bytes.fromhex('ffff0086'):
            return None
           message = bytes(payload[5:5 + payload[4]]).rstrip(b'\0')
           if message.startswith((b'Your terrain was loaded', b'There was an IO Exception',
                                  b'There was a general error')):
            return message
           return None

          def upload_confirmation(payload, expected_packet):
           if message := upload_alert(payload):
            upload_alerts.append(message)
           if len(payload) >= 13 and payload[0] == 19:
            return True if struct.unpack_from('<QI', payload, 1) == (upload_id, expected_packet) else None
           return None

          deadline = time.monotonic() + 35
          for offset in range(0, expected_size, 1024):
           number = offset // 1024
           if offset + 1024 >= expected_size:
            number |= 0x80000000
           chunk = raw_download[offset:offset + 1024]
           if offset == 0:
            chunk = struct.pack('<I', expected_size) + chunk
           admin_send(b'\x12' + struct.pack('<QIH', upload_id, number, len(chunk)) + chunk)
           admin_receive(lambda payload: upload_confirmation(payload, number))
           if time.monotonic() >= deadline:
            raise AssertionError('Timed out uploading the full variable-region terrain')
          upload_result = upload_alerts[0] if upload_alerts else admin_receive(upload_alert, timeout=12)
          check(upload_result.startswith(b'Your terrain was loaded as a .raw file'),
                f'Large LLRAW upload result: {upload_result!r}')
          check(baked_height() == 64.0, 'Large LLRAW upload replaces the saved baked sample')
          deadline = time.monotonic() + 5
          with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
           while True:
            terrain_data = database.execute('SELECT Heightfield FROM terrain WHERE Revision=23').fetchone()[0]
            raw = gzip.decompress(terrain_data)
            if height_at(300, 128) == 64.0:
             break
            check(time.monotonic() < deadline, 'Large LLRAW upload persists the active sample')
            time.sleep(.05)
       if a.script_parcel_access:
        print('PASS: restored LSL script edits both parcel access lists')
        return
       if a.script_parcel_denied:
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         saved = set(database.execute(
             "SELECT AccessUUID, Flags FROM landaccesslist WHERE LandUUID LIKE 'a0000000%'"))
         expected_pass = home_target if a.script_group_parcel_access else timeout_target
         expected_bans = {(str(cancel_target), 2)} if a.script_group_parcel_ban else {
             (str(ban_target), 2), (parcel_observer, 2)}
         check(saved == expected_bans | {(str(expected_pass), 1)},
               'Script changes only the parcel lists allowed by the object owner’s group powers')
        if a.script_group_parcel_access:
         print('PASS: scripted LandManagePasses changes pass entries and preserves bans')
        elif a.script_group_parcel_ban:
         print('PASS: scripted LandManageBanned changes bans and preserves pass entries')
        else:
         print('PASS: unauthorized script executes without changing live or saved parcel lists')
        return
       if not a.parcel_relocation:
        def edit_list(sender, viewer_id, session_id, transaction, entries, flags=2):
         data = struct.pack('<Ii16siiB', flags, 1, transaction.bytes, 1, 1, len(entries))
         rows = b''.join(struct.pack('<16siI', entry, expires, flags) for entry, expires in entries)
         sender(bytes.fromhex('ffff00d9') + viewer_id + session_id + data + rows)

        if a.group_parcel_access:
         local_id = 1
         def change_group_parcel(flags, entry, expected):
          edit_list(send, agent, session, uuid.uuid4(), [(entry, 0)], flags)
          send(bytes.fromhex('ffff00d7') + agent + session + struct.pack('<iIi', 0, flags, 1))
          replies = dict(receive(access_reply) for _ in range(2))
          check(replies == expected, f'Group parcel access after edit with flags {flags}')

         original_bans = {agent, ban_target.bytes}
         change_group_parcel(1, agent, {1: {agent}, 2: original_bans})
         change_group_parcel(2, bytes(16), {1: {agent}, 2: original_bans})
         change_group_parcel(3, bytes(16), {1: {agent}, 2: original_bans})
         parcel_member_powers |= 1 << 30
         change_group_parcel(3, bytes(16), {1: {bytes(16)}, 2: {bytes(16)}})
         with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
          saved_flags = int(database.execute('SELECT LandFlags FROM land WHERE LocalLandID=1').fetchone()[0])
          check(saved_flags & 1536 == 0, 'Authorized combined edit clears both parcel flags in SQLite')
         print('PASS: group-owned parcel edits require the corresponding group powers')
         return

        def saved_bans():
         with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
          return {uuid.UUID(row[0]).bytes for row in database.execute(
              "SELECT AccessUUID FROM landaccesslist WHERE Flags=2")}

        edit_list(send, agent, session, uuid.uuid4(), [(bytes(16), 0)])
        send(bytes.fromhex('ffff00d7') + agent + session + struct.pack('<iIi', 0, 2, 1))
        receive(lambda payload: True if payload[:4] == bytes.fromhex('ffff00d8') else None)
        check(saved_bans() == {agent, ban_target.bytes}, 'A visitor cannot clear a parcel ban list')

        owner_id, owner_session, owner_send, owner_receive, _ = connect_privacy_viewer(
            mover_socket, 'Owner', 128, login)
        transaction = uuid.uuid4()
        expired_agent = uuid.uuid4().bytes
        edit_list(owner_send, owner_id, owner_session, transaction, [(expired_agent, 1)])
        edit_list(owner_send, owner_id, owner_session, transaction, [(ban_target.bytes, 0)])
        owner_send(bytes.fromhex('ffff00d7') + owner_id + owner_session + struct.pack('<iIi', 0, 2, 1))
        def ban_reply(payload):
         if payload[:4] == bytes.fromhex('ffff00d8') and struct.unpack_from('<I', payload, 24)[0] == 2:
          return {bytes(payload[33 + index * 24:49 + index * 24]) for index in range(payload[32])}
        check(owner_receive(ban_reply) == {ban_target.bytes}, 'Viewer replies omit expired live bans')
        check(saved_bans() == {expired_agent, ban_target.bytes}, 'Both parts of a parcel edit persist together')
        edit_list(owner_send, owner_id, owner_session, uuid.uuid4(), [(bytes(16), 0)])
        owner_send(bytes.fromhex('ffff00d7') + owner_id + owner_session + struct.pack('<iIi', 0, 2, 1))
        check(owner_receive(ban_reply) == {bytes(16)} and not saved_bans(),
              'The parcel owner can clear the ban list in memory and SQLite')
        unbanned = proxy.login_to_simulator(dict(credentials, first='Privacy', last='BanTarget', start=start))
        check(unbanned.get('login') == 'true', 'Clearing the list immediately changes admission')
       print('PASS: persisted telehub parcel access on login and viewer arrival')
       return
      if a.sl_landing:
       login = proxy.login_to_simulator(dict(credentials, first='Privacy', last='Mover',
                                             start='uri:Connected Native&128&128&25'))
       connect_privacy_viewer(mover_socket, 'Mover', 128, login)
       print('PASS: configured SL landing position and look direction')
       return
      if a.os_landing:
       start = 'uri:Connected Native&128&128&25'
       observer_login = proxy.login_to_simulator(
           dict(credentials, first='Privacy', last='Observer', start=start))
       observer_id, observer_session, observer_send, observer_receive, _ = connect_privacy_viewer(
           observer_socket, 'Observer', 128, observer_login,
           expected_arrival=(132.5, 128.25, 25.0, 1.0, 0.0, 0.0))
       manager_login = proxy.login_to_simulator(
           dict(credentials, first='Privacy', last='Mover', start=start))
       if a.pbr_terrain:
        def decode_environment(node):
         if node.tag == 'map':
          fields = list(node)
          return {fields[i].text: decode_environment(fields[i + 1]) for i in range(0, len(fields), 2)}
         if node.tag == 'array':
          return [decode_environment(child) for child in node]
         if node.tag == 'integer':
          return int(node.text or 0)
         if node.tag == 'boolean':
          return node.text in ('1', 'true')
         return node.text or ''

        def environment_capability(seed_url, name='ExtEnvironment'):
         seed_request = urllib.request.Request(
             seed_url, f'<llsd><array><string>{name}</string></array></llsd>'.encode(),
             {'Content-Type': 'application/llsd+xml'})
         with urllib.request.urlopen(seed_request, timeout=5) as response:
          return decode_environment(ET.fromstring(response.read())[0])[name]

        environment_url = environment_capability(manager_login['seed_capability'])
        try:
         urllib.request.urlopen(environment_url, timeout=5).close()
         raise AssertionError('Environment read should wait for viewer presence')
        except urllib.error.HTTPError as error:
         check(error.code == 503 and error.headers['Retry-After'] == '5',
               'ExtEnvironment asks the viewer to retry before presence is connected')
       manager_id, manager_session, manager_send, manager_receive, _ = connect_privacy_viewer(
           mover_socket, 'Mover', 128, manager_login,
           expected_arrival=(128.0, 128.0, 25.0, 1.0, 0.0, 0.0))

       def handshake_assets(payload):
        if payload[:4] == bytes.fromhex('ffff0094'):
         return struct.unpack_from('<16s16s16s16s', payload, 10 + payload[9] + 105)
        return None

       if a.pbr_terrain:
        def read_environment(query=''):
         with urllib.request.urlopen(environment_url + query, timeout=5) as response:
          return decode_environment(ET.fromstring(response.read())[0])

        region_environment = read_environment()
        check(region_environment['success'] and region_environment['parcel_id'] == -1 and
              region_environment['environment']['day_length'] == 36000 and
              region_environment['environment']['day_offset'] == 3600,
              'ExtEnvironment returns the saved region timing')
        parcel_environment = read_environment('?parcelid=2')
        check(parcel_environment['success'] and parcel_environment['parcel_id'] == 2 and
              parcel_environment['environment']['day_length'] == 18000 and
              parcel_environment['environment']['day_offset'] == 1200,
              'ExtEnvironment returns the saved parcel override')
        default_environment = read_environment('?parcelid=1')
        check(default_environment['success'] and default_environment['parcel_id'] == 1 and
              default_environment['environment']['is_default'],
              'ExtEnvironment identifies parcels without an override')
        def sun_time_packet(packet):
         if packet[:4] == bytes.fromhex('ffff0096'):
          return struct.unpack_from('<QII3ff3f', packet, 4)
         return None

        def verify_sun_time(fields, day_length, offset, advanced):
         micros, seconds_per_day, seconds_per_year, *values = fields
         fraction = ((micros / 1000000 + offset) % day_length) / day_length
         if advanced:
          phase = 2 * math.pi * fraction
         elif fraction <= .25:
          phase = math.pi * (fraction + 1.5)
         elif fraction > .75:
          phase = math.pi * (fraction + .5)
         elif fraction >= .333333:
          phase = math.pi * (3 * fraction - 1)
         else:
          phase = math.pi * (3 * fraction + 1)
         return (abs(time.time() - micros / 1000000) < 10 and
                 (seconds_per_day, seconds_per_year) == (14400, 158400) and
                 values[:3] == [0., 0., 0.] and values[4:] == [0., 0., 0.] and
                 abs(values[3] - phase) < .0001)

        expected = tuple(uuid.UUID(int=value).bytes for value in (31, 32, 33, 34))
        check(manager_receive(handshake_assets) == expected, 'Negotiated viewer receives saved PBR materials')
        check(verify_sun_time(manager_receive(sun_time_packet), 36000, 3600, True),
              'Advanced viewer receives the periodic region sun phase')
        check(verify_sun_time(observer_receive(sun_time_packet), 18000, 1200, False),
              'Legacy viewer receives the remapped parcel sun phase')
        def change_environment(capability, method, day_length=None):
         payload = None
         if day_length is not None:
          payload = (f'<llsd><map><key>environment</key><map><key>day_length</key><integer>{day_length}</integer>'
                     '</map></map></llsd>').encode()
         request = urllib.request.Request(capability, payload, {'Content-Type': 'application/llsd+xml'},
                                          method=method)
         with urllib.request.urlopen(request, timeout=5) as response:
          return decode_environment(ET.fromstring(response.read())[0])

        check(not change_environment(environment_url + '?trackno=1', 'POST', 1000)['success'],
              'ExtEnvironment rejects track edits')
        check(change_environment(environment_url, 'PUT', 43200)['success'] and
              read_environment()['environment']['day_length'] == 43200 and
              read_environment()['environment']['day_offset'] == 3600,
              'ExtEnvironment updates the region and preserves omitted timing fields')
        def import_environment_asset(asset, name=None):
         name_field = f'<key>day_name</key><string>{name}</string>' if name else ''
         payload = (f'<llsd><map><key>environment</key><map><key>day_asset</key><uuid>{asset}</uuid>'
                    f'{name_field}</map></map></llsd>').encode()
         request = urllib.request.Request(environment_url, payload, {'Content-Type': 'application/llsd+xml'},
                                          method='PUT')
         with urllib.request.urlopen(request, timeout=5) as response:
          check(decode_environment(ET.fromstring(response.read())[0])['success'],
                'ExtEnvironment imports the requested asset')
         return read_environment()['environment']['day_cycle']

        default_cycle = import_environment_asset('5646d39e-d3d7-6aff-ed71-30fc87d64a92')
        check(len(default_cycle['tracks'][1]) > 1, 'The bundled day cycle supplies multiple sky keyframes')
        sky_cycle = import_environment_asset('3ae23978-ac82-bcf3-a9cb-ba6e52dcb9ad', 'Fixture sky')
        check(sky_cycle['name'] == 'Fixture sky' and sky_cycle['frames']['Fixture sky']['type'] == 'sky' and
              [entry['key_name'] for entry in sky_cycle['tracks'][1]] == ['Fixture sky'] and
              sky_cycle['tracks'][2:] == [[], [], []] and sky_cycle['tracks'][0] == default_cycle['tracks'][0],
              'A sky asset replaces all sky tracks and preserves the water track')
        sky_cycle = import_environment_asset('ff98b84a-cacf-459d-bf91-9e4498d68619', 'Sparse sky')
        sky = sky_cycle['frames']['Sparse sky']
        check(float(sky['cloud_scale']) == 0.125 and float(sky['dome_radius']) == 15000 and
              [float(value) for value in sky['sunlight_color']] == [1, 0.5, 0.25, 0] and
              [float(value) for value in sky['legacy_haze']['ambient']] == [0.25, 0.5, 1] and
              float(sky['reflection_probe_ambiance']) == 0.75 and
              [float(entry['width']) for entry in sky['absorption_config']] == [8, 0] and 'extra' not in sky,
              'Sky normalization applies defaults and matches the managed atmosphere reader')
        water_cycle = import_environment_asset('59d1a851-47e7-0e5f-1ed7-6b715154f41a')
        check(water_cycle['name'] == 'Sparse sky' and water_cycle['frames']['Water']['type'] == 'water' and
              [entry['key_name'] for entry in water_cycle['tracks'][0]] == ['Water'] and
              water_cycle['tracks'][1:] == sky_cycle['tracks'][1:] and
              water_cycle['frames']['Sparse sky'] == sky,
              'A water import preserves the normalized sky without interpreting it again')
        water_cycle = import_environment_asset('390d83cf-91d3-4f76-9483-4ae3af6198f2', 'Sparse water')
        water = water_cycle['frames']['Sparse water']
        check(float(water['blur_multiplier']) == 0.125 and float(water['water_fog_density']) == 10 and
              [float(value) for value in water['wave1_direction']] == [3, 4] and
              [float(value) for value in water['normal_scale']] == [0, 0, 0] and
              water['transparent_texture'] == '2bfd3884-7e27-69b9-ba3a-3e673f680004' and
              'name' not in water and 'extra' not in water,
              'Water normalization applies defaults, field types and vector lengths from .NET')
        parcel_url = environment_url + '?parcelid=2'
        observer_event_queue = environment_capability(observer_login['seed_capability'], 'EventQueueGet')
        environment_ack = None
        def wait_environment_event(predicate):
         nonlocal environment_ack
         for _ in range(4):
          ack = '<undef/>' if environment_ack is None else f'<integer>{environment_ack}</integer>'
          event_request = urllib.request.Request(
              observer_event_queue,
              f'<llsd><map><key>ack</key>{ack}<key>done</key><boolean>0</boolean></map></llsd>'.encode(),
              {'Content-Type': 'application/llsd+xml'})
          with urllib.request.urlopen(event_request, timeout=5) as response:
           batch = decode_environment(ET.fromstring(response.read())[0])
          environment_ack = batch['id']
          if any(predicate(event) for event in batch['events']):
           return True
         return False

        def parcel_environment_event(event, version):
         if event.get('message') != 'ParcelProperties':
          return False
         body = event['body']
         parcel_data = body['ParcelData'][0]
         return (parcel_data['LocalID'] == 2 and
                 [float(value) for value in parcel_data['AABBMin']] == [132., 0., 23.] and
                 [float(value) for value in parcel_data['AABBMax']] == [256., 256., 27.] and
                 body['ParcelEnvironmentBlock'][0] ==
                 {'ParcelEnvironmentVersion': version, 'RegionAllowEnvironmentOverride': True})

        parcel_edit = change_environment(parcel_url, 'POST', 21600)
        check(parcel_edit['success'] and
              read_environment('?parcelid=2')['environment']['day_length'] == 21600,
              f'ExtEnvironment lets the parcel owner edit the override: {parcel_edit}')
        parcel_version = read_environment('?parcelid=2')['environment']['env_version']
        check(wait_environment_event(lambda event: parcel_environment_event(event, parcel_version)),
              'Parcel environment edit advertises the saved version to the viewer on the parcel')
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         region_data = database.execute('SELECT llsd_settings FROM regionenvironment').fetchone()[0]
         parcel_data = database.execute('SELECT environment FROM land WHERE LocalLandID=2').fetchone()[0]
         saved_region = decode_environment(ET.fromstring(region_data)[0])
         check(saved_region['day_length'] == 43200 and
               saved_region['day_cycle'] == water_cycle and
               decode_environment(ET.fromstring(parcel_data)[0])['day_length'] == 21600,
               'ExtEnvironment saves both edits to SQLite')
        proc.stdin.write('terrain elevate 1\n')
        proc.stdin.flush()
        deadline = time.monotonic() + 8
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         while True:
          revision, terrain_data = database.execute('SELECT Revision,Heightfield FROM terrain').fetchone()
          if revision == 23 and struct.unpack_from('<f', gzip.decompress(terrain_data), 8 + 4 * 132)[0] == 24.:
           break
          check(time.monotonic() < deadline, 'Terrain edit persists before the parcel update')
          time.sleep(.05)
        check(change_environment(parcel_url, 'DELETE')['success'] and
              read_environment('?parcelid=2')['environment']['is_default'],
              'ExtEnvironment resets the parcel override')
        check(wait_environment_event(lambda event: parcel_environment_event(event, -1)),
              'Parcel environment reset advertises no override and retains the original parcel bounds')
        proc.stdin.write('terrain revert\n')
        proc.stdin.flush()
        legacy = b'''<llsd><array><map/><array>
          <array><real>0.75</real><string>Evening</string></array>
          <array><real>0.25</real><string>Evening</string></array>
          </array><map><key>Evening</key><map>
          <key>ambient</key><array><real>0.5</real><real>0.25</real><real>1</real><real>1</real></array>
          <key>cloud_scroll_rate</key><array><real>11</real><real>12</real></array>
          <key>enable_cloud_scroll</key><array><boolean>true</boolean><boolean>false</boolean></array>
          <key>star_brightness</key><real>0.25</real>
          </map></map><map>
          <key>waterFogDensity</key><real>12.5</real>
          <key>waterFogColor</key><array><real>0.25</real><real>0.5</real><real>1</real><real>1</real></array>
          </map></array></llsd>'''
        legacy_request = urllib.request.Request(environment_url, legacy,
                                                 {'Content-Type': 'application/llsd+xml'}, method='POST')
        with urllib.request.urlopen(legacy_request, timeout=5) as response:
         legacy_result = decode_environment(ET.fromstring(response.read())[0])
        converted = read_environment()['environment']
        converted_cycle = converted['day_cycle']
        legacy_sky = converted_cycle['frames']['Evening']
        legacy_water = converted_cycle['frames']['WLWater']
        check(legacy_result['success'] and converted['day_length'] == 14400 and
              converted_cycle['name'] == 'WLDaycycle' and
              [float(key['key_keyframe']) for key in converted_cycle['tracks'][1]] == [0.25, 0.75] and
              [float(value) for value in legacy_sky['cloud_scroll_rate']] == [1, 0] and
              float(legacy_sky['star_brightness']) == 62.5 and
              [float(value) for value in legacy_water['water_fog_color']] == [0.25, 0.5, 1] and
              float(legacy_water['water_fog_density']) == 12.5,
              'ExtEnvironment converts legacy WindLight tracks and frames to EEP')
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         migrated = database.execute('SELECT llsd_settings FROM regionenvironment').fetchone()[0]
         check(decode_environment(ET.fromstring(migrated)[0])['day_cycle'] == converted_cycle,
               'Converted WindLight settings are persisted as an EEP day cycle')
        legacy_url = environment_capability(manager_login['seed_capability'], 'EnvironmentSettings')
        def legacy_upload(capability):
         request = urllib.request.Request(capability, legacy, {'Content-Type': 'application/llsd+xml'},
                                          method='POST')
         with urllib.request.urlopen(request, timeout=5) as response:
          return decode_environment(ET.fromstring(response.read())[0])

        check(legacy_upload(legacy_url)['success'], 'EnvironmentSettings accepts an estate WindLight upload')
        with urllib.request.urlopen(legacy_url, timeout=5) as response:
         legacy_readback = decode_environment(ET.fromstring(response.read())[0])
        check(legacy_readback[0]['regionID'] == '11111111-1111-4111-8111-111111111111' and
              [float(key[0]) for key in legacy_readback[1]] == [0.25, 0.75] and
              [float(value) for value in legacy_readback[2]['Evening']['cloud_scroll_rate']] == [11, 10] and
              float(legacy_readback[2]['Evening']['star_brightness']) == 0.25 and
              float(legacy_readback[3]['waterFogDensity']) == 12.5,
              'EnvironmentSettings exports the four-part WindLight response')
        def lightshare_parameters(packet, method):
         if packet[:4] != bytes.fromhex('ffff0105'):
          return None
         method_end = 53 + packet[52]
         if packet[53:method_end].rstrip(b'\0') != method:
          return None
         check(packet[4:20] == observer_id and packet[20:36] == observer_session and
               packet[36:52] == bytes(16), 'LightShare uses the connected viewer circuit')
         return packet[method_end + 16:]

        def windlight_payload(packet):
         params = lightshare_parameters(packet, b'Windlight')
         if params and len(params) >= 14 and struct.unpack_from('<3f', params, 2) == (64., 128., 256.):
          return params
         return None

        params = observer_receive(windlight_payload)
        check(params[:2] == bytes((1, 249)) and len(params) == 251 and
              abs(struct.unpack_from('<f', params, 14)[0] - 12.5**0.5) < 0.00001 and
              struct.unpack_from('<f', params, 186)[0] == .25 and
              struct.unpack_from('<2f', params, 238) == (1., 0.) and params[-3:] == bytes((0, 1, 1)),
              'LightShare delivers the 249-byte sky and water block to a UDP-only viewer')
        check(change_environment(environment_url, 'DELETE')['success'] and
              read_environment()['environment']['day_length'] == 14400,
              'ExtEnvironment resets the region to the default cycle')
        check(observer_receive(lambda packet: lightshare_parameters(packet, b'WindlightReset')) == b'\0',
              'LightShare sends a reset with zero parameters after region settings are cleared')
        for x in (128., 132.5):
         observer_send(bytes.fromhex('ffff003f') + observer_id + observer_session +
                       struct.pack('<Qffffff', 1099511628032000, x, 128., 25., 1., 0., 0.))
         arrival = observer_receive(lambda packet: struct.unpack_from('<3f', packet, 24)
                                    if packet[:4] == bytes.fromhex('ffff0040') else None)
         check(arrival[0] == x, f'Parcel crossing reaches the requested coordinate: {arrival}')
         check(wait_environment_event(lambda event: event.get('message') == 'WindLightRefresh' and
                                      event.get('body', {}).get('Interpolate') == 1),
               'Crossing a parcel boundary queues an interpolated WindLight refresh')
        observer_environment = environment_capability(observer_login['seed_capability'])
        check(not change_environment(observer_environment, 'POST', 1000)['success'],
              'ExtEnvironment denies region edits by ordinary viewers')
        observer_legacy_url = environment_capability(observer_login['seed_capability'], 'EnvironmentSettings')
        denied = legacy_upload(observer_legacy_url)
        check(not denied['success'] and 'estate permissions' in denied['fail_reason'],
              'EnvironmentSettings denies a non-estate WindLight upload')
        with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
         check(database.execute('SELECT COUNT(*) FROM regionenvironment').fetchone() == (0,) and
               database.execute('SELECT environment FROM land WHERE LocalLandID=2').fetchone() == (None,),
               'ExtEnvironment clears both persisted overrides')

       def texture_edit(agent_id, session_id, method, *values):
        encoded = b''.join(bytes([len(value) + 1]) + value + b'\0' for value in values)
        return (bytes.fromhex('ffff0104') + agent_id + session_id + bytes(16) +
                bytes([len(method) + 1]) + method + b'\0' + uuid.uuid4().bytes +
                bytes([len(values)]) + encoded)

       observer_send(texture_edit(observer_id, observer_session, b'textureheights', b'0 99 100'))
       texture = uuid.UUID(int=42)
       observer_send(texture_edit(observer_id, observer_session, b'texturedetail', f'1 {texture}'.encode()))
       observer_send(bytes.fromhex('ffff00d7') + observer_id + observer_session + struct.pack('<iIi', 0, 1, 1))
       observer_receive(lambda payload: True if payload[:4] == bytes.fromhex('ffff00d8') else None)
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute('SELECT elevation_1_sw,elevation_2_sw FROM regionsettings').fetchone() == (11, 61),
              'A visitor cannot edit saved texture heights')
        check(database.execute('SELECT terrain_texture_2 FROM regionsettings').fetchone() == (str(uuid.UUID(int=22)),),
              'A visitor cannot edit saved terrain texture assets')

       manager_send(texture_edit(manager_id, manager_session, b'textureheights', b'4 99 100', b'2 -12.25 84.5'))

       def updated_texture_heights(payload):
        if payload[:4] != bytes.fromhex('ffff0094'):
         return None
        heights = struct.unpack_from('<8f', payload, 10 + payload[9] + 169)
        return heights if heights[2] == -12.25 else None

       expected_heights = (11, 12, -12.25, 14, 61, 62, 84.5, 64)
       check(manager_receive(updated_texture_heights) == expected_heights and
             observer_receive(updated_texture_heights) == expected_heights,
             'Texture height updates broadcast all four saved corners to viewers')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute('SELECT elevation_1_se,elevation_2_se FROM regionsettings').fetchone()
              == (-12.25, 84.5), 'Estate texture height edit persists in SQLite')
       manager_send(texture_edit(manager_id, manager_session, b'texturedetail',
                                 f'0 {uuid.UUID(int=0)}'.encode(), f'1 {texture}'.encode()))

       def updated_texture_assets(payload):
        assets = handshake_assets(payload)
        return assets if assets and assets[1] == texture.bytes else None

       if a.pbr_terrain:
        expected_pbr = tuple(uuid.UUID(int=value).bytes for value in (31, 42, 33, 34))
        expected_legacy = tuple(uuid.UUID(int=value).bytes for value in (21, 22, 23, 24))
        check(manager_receive(updated_texture_assets) == expected_pbr and
              observer_receive(handshake_assets) == expected_legacy,
              'Mixed viewers receive PBR and legacy terrain assets independently')
       else:
        expected_assets = tuple(uuid.UUID(int=value).bytes for value in (21, 42, 23, 24))
        check(manager_receive(updated_texture_assets) == expected_assets and
              observer_receive(updated_texture_assets) == expected_assets,
              'Texture asset selection broadcasts to viewers and ignores the zero UUID')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        expected_saved = (uuid.UUID(int=22), texture) if a.pbr_terrain else (texture, uuid.UUID(int=32))
        check(database.execute('SELECT terrain_texture_2,TerrainPBR2 FROM regionsettings').fetchone() ==
              tuple(map(str, expected_saved)), 'Terrain edit persists in the correct SQLite column')
        if a.extended_pbr_terrain:
         terrain = database.execute('SELECT Heightfield FROM terrain WHERE Revision=22').fetchone()[0]
         check(struct.unpack_from('<II', terrain) == (512, 512), 'Extended PBR test uses a 512-meter region')
       parameters = (b'33.25', b'250', b'-225', b'Y', b'N', b'6', b'N', b'N', b'0')
       manager_send(texture_edit(manager_id, manager_session, b'setregionterrain', *parameters))

       def updated_region_terrain(payload):
        if payload[:4] != bytes.fromhex('ffff008e'):
         return None
        values = struct.unpack_from('<3f', payload, 37 + payload[36] + 22)
        return values if values[0] == 33.25 else None

       expected_settings = (33.25, 200.0, -200.0)
       check(manager_receive(updated_region_terrain) == expected_settings and
             observer_receive(updated_region_terrain) == expected_settings,
             'Terrain settings broadcast the updated water and clamped terraform limits')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute(
            'SELECT water_height,terrain_raise_limit,terrain_lower_limit FROM regionsettings'
        ).fetchone() == expected_settings, 'Terrain settings persist in SQLite')
        if a.pbr_terrain:
         check(database.execute(
             'SELECT use_estate_sun,fixed_sun,sun_position FROM regionsettings'
         ).fetchone() == (0, 0, 0.0), 'OSSL saves the estate-sun selection and legacy property defaults')
       parameters = (b'N', b'yes', b'N', b'true', b'17.5', b'2.25', b'42', b't', b'false')

       def estate_region_info(payload):
        if payload[:4] != bytes.fromhex('ffff008e'):
         return None
        offset = 37 + payload[36] + 8
        flags, access, limit, _, bonus = struct.unpack_from('<IBBff', payload, offset)
        return access, limit, bonus, flags & 0x244800c1

       observer_send(texture_edit(observer_id, observer_session, b'setregioninfo', *parameters))
       observer_send(bytes.fromhex('ffff008d') + observer_id + observer_session)
       check(observer_receive(estate_region_info)[0] == 21, 'Visitor cannot change region maturity')
       manager_send(texture_edit(manager_id, manager_session, b'setregioninfo', *parameters))

       def updated_estate_region_info(payload):
        info = estate_region_info(payload)
        return info if info and info[0] == 42 else None

       expected_info = (42, 18, 2.25, 0x480000)
       check(manager_receive(updated_estate_region_info) == expected_info and
             observer_receive(updated_estate_region_info) == expected_info,
             'Estate settings update broadcasts access, capacity, bonus and flags')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute(
            'SELECT maturity,agent_limit,object_bonus,block_terraform,block_fly,allow_damage,'
            'allow_land_resell,restrict_pushing,allow_land_join_divide FROM regionsettings'
        ).fetchone() == (2, 18, 2.25, 0, 1, 0, 1, 1, 0), 'Estate settings update persists in SQLite')

       def estate_cap(login):
        seed_request = urllib.request.Request(login['seed_capability'],
            b'<llsd><array><string>DispatchRegionInfo</string></array></llsd>',
            {'Content-Type': 'application/llsd+xml'})
        with urllib.request.urlopen(seed_request, timeout=5) as response:
         return ET.fromstring(response.read()).findtext('./map/string')

       def cap_status(endpoint, data=None):
        request = urllib.request.Request(endpoint, data, {'Content-Type': 'application/llsd+xml'})
        try:
         with urllib.request.urlopen(request, timeout=5) as response:
          return response.status
        except urllib.error.HTTPError as error:
         with error:
          return error.code

       manager_cap, observer_cap = estate_cap(manager_login), estate_cap(observer_login)
       event_seed_request = urllib.request.Request(
           manager_login['seed_capability'],
           b'<llsd><array><string>EventQueueGet</string></array></llsd>',
           {'Content-Type': 'application/llsd+xml'})
       with urllib.request.urlopen(event_seed_request, timeout=5) as response:
        event_cap = ET.fromstring(response.read()).findtext('./map/string')

       def event_fields(node):
        children = list(node)
        return {key.text: value for key, value in zip(children[::2], children[1::2])}

       def event_uint(value):
        if value.tag == 'binary':
         return int.from_bytes(base64.b64decode(value.text), 'big')
        return int(value.text)

       manager_send(texture_edit(manager_id, manager_session, b'colliders'))
       name_filter = b'No object matches this fixture filter\0'
       manager_send(bytes.fromhex('ffff01a5') + manager_id + manager_session +
                    struct.pack('<IIB', 0, 4, len(name_filter)) + name_filter + struct.pack('<i', 0))
       reports = {}
       ack = '<undef/>'
       for _ in range(5):
        poll = urllib.request.Request(
            event_cap, f'<llsd><map><key>ack</key>{ack}<key>done</key><boolean>0</boolean></map></llsd>'.encode(),
            {'Content-Type': 'application/llsd+xml'})
        with urllib.request.urlopen(poll, timeout=8) as response:
         batch = event_fields(ET.fromstring(response.read())[0])
        ack = f'<integer>{int(batch["id"].text)}</integer>'
        for event in batch['events']:
         fields = event_fields(event)
         if fields['message'].text == 'LandStatReply':
          request = event_fields(event_fields(fields['body'])['RequestData'][0])
          reports[event_uint(request['ReportType'])] = (
              event_uint(request['RequestFlags']), event_uint(request['TotalObjectCount']))
        if reports.keys() >= {0, 1}:
         break
       check(reports == {0: (4, 0), 1: (0, 0)},
             'Direct filtered script request and estate collider request return matching LandStatReply metadata')

       edit = (b'<llsd><map><key>sim_access</key><integer>13</integer>'
               b'<key>block_parcel_search</key><boolean>true</boolean>'
               b'<key>agent_limit</key><integer>1</integer></map></llsd>')
       check(cap_status(manager_cap) == 404, 'Estate region capability accepts only POST')
       check(cap_status(manager_cap, b'<llsd><array/></llsd>') == 400, 'Estate capability requires a map')
       check(cap_status(manager_cap, b'<llsd><map><key>sim_access</key><integer>13</integer></map></llsd>') == 400,
             'Estate capability preserves the source single-field rejection')
       check(cap_status(observer_cap, edit) == 401, 'Visitor cannot edit settings through the estate capability')
       check(cap_status(manager_cap, b'<llsd><map/></llsd>') == 200,
             'Estate capability preserves the source empty-map behavior')
       check(cap_status(manager_cap, edit) == 200, 'Estate manager can edit settings through the capability')

       def cap_updated_info(payload):
        info = estate_region_info(payload)
        return info if info and info[0] == 13 else None

       check(manager_receive(cap_updated_info) == (13, 100, 2.25, 0x20480000) and
             observer_receive(cap_updated_info) == (13, 100, 2.25, 0x20480000),
             'Estate capability broadcasts the partial update to both viewers')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute(
            'SELECT maturity,agent_limit,object_bonus,block_show_in_search,restrict_pushing FROM regionsettings'
        ).fetchone() == (0, 100, 2.25, 1, 1), 'Estate capability persists updates and preserves omitted fields')
       notice = {'METHOD': ['update_covenant'], 'TOKEN': ['estate-covenant-fixture'],
                 'EstateID': ['100'], 'CovenantID': ['99887766-5544-4332-8110-010203040506']}
       for _ in range(6):
        check(estate_deliveries.get(timeout=5) == notice,
              'Each accepted estate region edit forwards the current covenant once')
       peer_covenant = 'f7d2c35a-b6bd-4b34-9559-e10f3d24ca6a'
       peer_update = urllib.parse.urlencode(dict(METHOD='update_covenant', TOKEN='estate-covenant-fixture',
                                                EstateID='100', CovenantID=peer_covenant)).encode()
       with urllib.request.urlopen(urllib.request.Request(url.rstrip('/') + '/estate', peer_update),
                                   timeout=5) as response:
        check(ET.fromstring(response.read()).findtext('RESULT') == 'True', 'Peer covenant update is accepted')
       try:
        estate_deliveries.get(timeout=1)
        raise AssertionError('A peer update echoed or a region notification fired early')
       except queue.Empty:
        pass
       check(estate_deliveries.get(timeout=12) == {**notice, 'CovenantID': [peer_covenant]},
             'Coalesced delayed notification forwards the latest covenant')
       check(estate_deliveries.empty(), 'Rapid edits share one delayed estate notification')

       def debug_flags(payload):
        if payload[:4] == bytes.fromhex('ffff008e'):
         return struct.unpack_from('<I', payload, 37 + payload[36] + 8)[0] & 0x7000
        return None

       def edit_debug(agent, session, send, receive, values):
        send(texture_edit(agent, session, b'setregiondebug', *values))
        send(bytes.fromhex('ffff008d') + agent + session)
        return receive(debug_flags)

       check(edit_debug(observer_id, observer_session, observer_send, observer_receive, (b'Y', b'Y', b'Y'))
             == 0x4000, 'Visitor cannot edit region debug flags')
       check(edit_debug(manager_id, manager_session, manager_send, manager_receive, (b'Y', b'Y', b'Y'))
             == 0x7000, 'Estate manager can disable scripts and set the collision debug flag')
       with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
        check(database.execute('SELECT disable_scripts,disable_collisions,disable_physics FROM regionsettings')
              .fetchone() == (1, 1, 1), 'Region debug settings persist together')
       check(edit_debug(manager_id, manager_session, manager_send, manager_receive, (b'N', b'N', b'Y'))
             == 0x4000, 'Estate manager can restore scripts and clear the collision debug flag')

       def region_notice(expected, recipient):
        def match(payload):
         if payload[:4] != bytes.fromhex('ffff0087') or payload[4:20] != recipient:
          return None
         message = bytes(payload[22:22 + payload[21]]).rstrip(b'\0')
         check(message not in (b'/Visitor notice', b'/Wrong estate notice', b'/Invalid token notice'),
               'Unauthorized or wrong-estate announcements do not reach viewers')
         return payload[20] == 0 if message == expected else None
        return match

       observer_send(texture_edit(observer_id, observer_session, b'instantmessage', b'Visitor', b'Visitor notice'))
       manager_send(texture_edit(manager_id, manager_session, b'simulatormessage', b'0', b'0',
                                 str(uuid.UUID(bytes=manager_id)).encode(), b'Mover', b'Region notice'))
       check(manager_receive(region_notice(b'/Region notice', manager_id)) and
             observer_receive(region_notice(b'/Region notice', observer_id)), 'Simulator announcement reaches both roots')
       manager_send(texture_edit(manager_id, manager_session, b'instantmessage', b'Estate Mover', b'Estate + update'))
       check(manager_receive(region_notice(b'/Estate + update', manager_id)) and
             observer_receive(region_notice(b'/Estate + update', observer_id)), 'Estate announcement reaches both roots')
       deadline = time.monotonic() + 5
       while True:
        outgoing = estate_deliveries.get(timeout=max(.01, deadline - time.monotonic()))
        if outgoing.get('METHOD') == ['estate_message']:
         break
       check(outgoing == {'METHOD': ['estate_message'], 'TOKEN': ['estate-covenant-fixture'], 'EstateID': ['100'],
                          'FromID': [str(uuid.UUID(bytes=manager_id))], 'FromName': ['Estate Mover'],
                          'Message': ['Estate + update']}, 'Estate announcement forwards sender and message once')

       def peer_notice(estate, token, message):
        body = urllib.parse.urlencode(dict(METHOD='estate_message', TOKEN=token, EstateID=estate,
                                         FromID=str(uuid.UUID(bytes=manager_id)), FromName='Peer', Message=message)).encode()
        with urllib.request.urlopen(urllib.request.Request(url.rstrip('/') + '/estate', body), timeout=5) as response:
         return ET.fromstring(response.read()).findtext('RESULT')

       check(peer_notice(100, 'wrong', 'Invalid token notice') == 'False', 'Estate notice rejects invalid token')
       check(peer_notice(101, 'estate-covenant-fixture', 'Wrong estate notice') == 'True',
             'Estate notice acknowledges another estate')
       check(peer_notice(100, 'estate-covenant-fixture', 'Peer notice') == 'True', 'Estate notice accepts peer message')
       check(manager_receive(region_notice(b'/Peer notice', manager_id)) and
             observer_receive(region_notice(b'/Peer notice', observer_id)), 'Peer announcement reaches both roots')
       while True:
        try:
         outgoing = estate_deliveries.get(timeout=.4)
        except queue.Empty:
         break
        check(outgoing.get('METHOD') != ['estate_message'], 'Peer and simulator announcements are not forwarded')

       def teleport_local(expected_agent):
        def match(payload):
         if payload[:4] == bytes.fromhex('ffff0040') and payload[4:20] == expected_agent:
          return struct.unpack_from('<3f', payload, 24)
         return None
        return match

       observer_uuid = str(uuid.UUID(bytes=observer_id))
       manager_send(texture_edit(manager_id, manager_session, b'teleporthomeuser', b'0', observer_uuid.encode()))
       check(observer_receive(teleport_local(observer_id)) == (140., 144., 25.),
             'Estate manager teleports a viewer to its saved local home')

       def next_estate_home():
        deadline = time.monotonic() + 5
        while True:
         form = estate_deliveries.get(timeout=max(.01, deadline - time.monotonic()))
         if form.get('METHOD', [''])[0].startswith('teleport_home_'):
          return form

       absent = '905d0bdb-c2dd-4ac7-bcb2-8f5337d2d5e8'
       observer_send(texture_edit(observer_id, observer_session, b'teleporthomeuser', b'0', absent.encode()))
       manager_send(texture_edit(manager_id, manager_session, b'teleporthomeuser', b'0', str(home_target).encode()))
       check(next_estate_home() == {'METHOD': ['teleport_home_one_user'], 'TOKEN': ['estate-covenant-fixture'],
                                    'EstateID': ['100'], 'PreyID': [str(home_target)]},
             'Only the manager can forward a home request for an absent viewer')
       peer_home = urllib.parse.urlencode(dict(METHOD='teleport_home_one_user', TOKEN='estate-covenant-fixture',
                                              EstateID='100', PreyID=observer_uuid)).encode()
       with urllib.request.urlopen(urllib.request.Request(url.rstrip('/') + '/estate', peer_home),
                                   timeout=5) as response:
        check(ET.fromstring(response.read()).findtext('RESULT') == 'True', 'Peer home request is accepted')
       check(observer_receive(teleport_local(observer_id)) == (140., 144., 25.), 'Peer can send a viewer home')
       manager_send(texture_edit(manager_id, manager_session, b'teleporthomeallusers'))
       check(manager_receive(teleport_local(manager_id)) == (146., 148., 26.) and
             observer_receive(teleport_local(observer_id)) == (140., 144., 25.),
             'Estate-wide home request sends both root viewers home')
       check(next_estate_home() == {'METHOD': ['teleport_home_all_users'], 'TOKEN': ['estate-covenant-fixture'],
                                    'EstateID': ['100']}, 'Estate-wide home request is forwarded without echo')
       print('PASS: default parcel landing and estate manager exemption')
       return
      observer_id,observer_session,observer_send,observer_receive,_=connect_privacy_viewer(observer_socket,'Observer',128)
      root_id=uuid.UUID(stored_root).bytes
      def stored_scale(p):
       if len(p)>=52 and p[0]==12 and p[17:33]==root_id:
        return struct.unpack_from('<I',p,12)[0],struct.unpack_from('<3f',p,40)
      prim_id,original_scale=observer_receive(stored_scale)
      mover_id,mover_session,mover_send,mover_receive,mover_seed=connect_privacy_viewer(mover_socket,'Mover',120)
      event_seed=urllib.request.Request(mover_seed,b'<llsd><array><string>EventQueueGet</string></array></llsd>',
                                       {'Content-Type':'application/llsd+xml'})
      with urllib.request.urlopen(event_seed,timeout=5) as response:
       event_cap=ET.fromstring(response.read()).find('map/string').text
      def llsd_map(node):
       values=list(node)
       return {values[i].text:values[i+1] for i in range(0,len(values),2)}
      def check_group_update(notices=True,profile=False):
       poll=urllib.request.Request(event_cap,b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>',
                                   {'Content-Type':'application/llsd+xml'})
       with urllib.request.urlopen(poll,timeout=8) as response:
        events=llsd_map(ET.fromstring(response.read())[0])['events']
       membership=next(llsd_map(event)['body'] for event in events
                       if llsd_map(event)['message'].text=='AgentGroupDataUpdate')
       body=llsd_map(membership)
       group=llsd_map(body['GroupData'][0])
       check(llsd_map(body['AgentData'][0])['AgentID'].text==str(uuid.UUID(bytes=mover_id)) and
             group['GroupID'].text=='00000000-0000-4000-8000-000000000003' and
             group['GroupName'].text=='Parcel Group' and group['Contribution'].text=='42' and
             int.from_bytes(base64.b64decode(group['GroupPowers'].text),'big')==274877906944 and
             (group['AcceptNotices'].text in ('1','true'))==notices and
             (llsd_map(body['NewGroupData'][0])['ListInProfile'].text in ('1','true'))==profile,
             'Viewer receives its group membership, role powers, and profile visibility through EventQueueGet')
      check_group_update()
      mover_send(bytes.fromhex('ffff0182')+mover_id+mover_session)
      check_group_update()
      mover_send(bytes.fromhex('ffff0172')+mover_id+mover_session+
                 uuid.UUID('00000000-0000-4000-8000-000000000003').bytes+bytes([0,1]))
      mover_send(bytes.fromhex('ffff0182')+mover_id+mover_session)
      check_group_update(False,True)
      check(group_preferences[manager]==('False','True'),'Remote Groups service receives both membership preferences')
      def active_group_reply(payload):
       if payload[:4]!=bytes.fromhex('ffff0183'):return
       index=20
       names=[]
       for _ in range(3):
        end=index+1+payload[index]
        names.append(bytes(payload[index+1:end]).rstrip(b'\0'))
        index=end
       group=bytes(payload[index:index+16])
       powers=struct.unpack_from('<Q',payload,index+16)[0]
       size=payload[index+24]
       name=bytes(payload[index+25:index+25+size]).rstrip(b'\0')
       return (group,powers,names[2],name)
      active_group=uuid.UUID('00000000-0000-4000-8000-000000000003').bytes
      for expected in ((active_group,274877906944,b'Builder',b'Parcel Group'),(bytes(16),0,b'',b'')):
       mover_send(bytes.fromhex('ffff0170')+mover_id+mover_session+expected[0])
       deadline=time.monotonic()+5
       while True:
        actual=mover_receive(active_group_reply)
        if actual==expected or time.monotonic()>=deadline:break
       check(actual==expected,'Activating or clearing a group sends the authoritative active group and title')
       if expected[0]==active_group:
        def remote_titles():
         request=uuid.uuid4().bytes
         mover_send(bytes.fromhex('ffff0177')+mover_id+mover_session+active_group+request)
         def decode_titles(payload):
          if payload[:52]!=bytes.fromhex('ffff0178')+mover_id+active_group+request:return
          index=53
          titles={}
          for _ in range(payload[52]):
           end=index+1+payload[index]
           titles[str(uuid.UUID(bytes=bytes(payload[end:end+16])))]=(bytes(payload[index+1:end]).rstrip(b'\0'),payload[end+16])
           index=end+17
          return titles
         return mover_receive(decode_titles)
        check(remote_titles()=={remote_builder:(b'Builders',1),remote_host:(b'Hosts',0)},
              'Remote Groups connector lists assigned roles and selected title')
        mover_send(bytes.fromhex('ffff0179')+mover_id+mover_session+active_group+uuid.UUID(remote_host).bytes)
        host_reply=(active_group,42,b'Host',b'Parcel Group')
        check(mover_receive(active_group_reply)==host_reply,'Remote title selection refreshes title and powers')
        mover_send(bytes.fromhex('ffff0179')+mover_id+mover_session+active_group+uuid.uuid4().bytes)
        check(remote_titles()=={remote_builder:(b'Builders',0),remote_host:(b'Hosts',1)} and active_roles[manager]==remote_host,
              'Unassigned roles cannot be selected through the remote connector')
      def mover_object(payload):
       if len(payload)>=33 and payload[0]==12 and payload[17:33]==mover_id:
        return struct.unpack_from('<I',payload,12)[0]
      local_id=observer_receive(mover_object)
      def telehub_command(command, parameter, expected_id, expected_count):
       arguments = (command.encode() + b'\0', str(parameter).encode() + b'\0')
       encoded = b''.join(bytes([len(argument)]) + argument for argument in arguments)
       mover_send(bytes.fromhex('ffff0104') + mover_id + mover_session + bytes(16) +
                  b'\x08telehub\0' + uuid.uuid4().bytes + bytes([len(arguments)]) + encoded)
       def matches(payload):
        if payload[:4] != bytes.fromhex('ffff000a') or len(payload) < 46:
         return None
        count_offset = 21 + payload[20] + 24
        if (count_offset < len(payload) and payload[4:20] == expected_id.bytes and
                payload[count_offset] == expected_count):
         return True
       check(mover_receive(matches), f'Telehub {command} reports its object and spawn points')

      stored_hub = uuid.UUID('eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee')
      telehub_command('info ui', 0, stored_hub, 1)
      telehub_command('connect', prim_id, stored_hub, 0)
      telehub_command('spawnpoint add', prim_id, stored_hub, 1)
      with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
       check(database.execute('SELECT TelehubObject FROM regionsettings').fetchone() == (str(stored_hub),) and
             database.execute('SELECT Distance FROM spawn_points').fetchall() == [(0.0,)],
             'Estate telehub commands save the object and replacement spawn points together')
      telehub_command('delete', 0, uuid.UUID(int=0), 0)
      with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
       check(database.execute('SELECT TelehubObject FROM regionsettings').fetchone() == (str(uuid.UUID(int=0)),) and
             database.execute('SELECT COUNT(*) FROM spawn_points').fetchone() == (0,),
             'Estate telehub deletion clears persisted object and spawn points')
      invoice=uuid.uuid4().bytes
      mover_send(bytes.fromhex('ffff0104')+mover_id+mover_session+bytes(16)+b'\x08getinfo\0'+invoice+b'\0')
      def estate_details(payload):
       if payload[:4]!=bytes.fromhex('ffff0104'):return
       method_end=53+payload[52]
       if payload[method_end:method_end+16]!=invoice:return
       method=bytes(payload[53:method_end]).rstrip(b'\0')
       index=method_end+16
       count=payload[index]
       index+=1
       parameters=[]
       for _ in range(count):
        end=index+1+payload[index]
        parameters.append(bytes(payload[index+1:end]))
        index=end
       return method,parameters
      reply_method,parameters=mover_receive(estate_details)
      details=[value.rstrip(b'\0') for value in parameters]
      check(reply_method==b'estateupdateinfo' and len(details)==10 and
            details[:3]==[b'Fixture Estate',owner.encode(),b'100'] and
            details[6:8]==[b'99887766-5544-4332-8110-010203040506',b'1700000123'],
            'Estate details preserve invoice and stored covenant metadata')
      check(int(details[3]) & 0x40800000 == 0,
            'Estate details omit stored identity and age restrictions when enforcement is ignored')
      access_lists=[]
      for _ in range(5):
       reply_method,parameters=mover_receive(estate_details)
       check(reply_method==b'setaccess','Estate access list uses the source reply method')
       access_lists.append(([int(value.rstrip(b'\0')) for value in parameters[:6]],parameters[6:]))
      check(access_lists==[([100,8,0,0,0,1],[mover_id]),
                           ([100,1,63,0,0,0],[agent.bytes for agent in allowed[:63]]),
                           ([100,1,1,0,0,0],[allowed[-1].bytes]),
                           ([100,2,0,0,0,0],[]),([100,4,0,0,0,0],[])],
            'Estate lists contain binary UUIDs, split at 63 entries, and include empty groups and bans')
      group=uuid.UUID('84e2ad34-3209-4fdd-a39f-ddd6630c1b8a')
      def edit_estate_list(flag,target):
       method=b'estateaccessdelta\0'
       parameters=[b'100\0',str(flag).encode()+b'\0',str(target).encode()+b'\0']
       mover_send(bytes.fromhex('ffff0104')+mover_id+mover_session+bytes(16)+
                  bytes([len(method)])+method+invoice+bytes([len(parameters)])+
                  b''.join(bytes([len(value)])+value for value in parameters))
      for flag,expected in ((16,[group.bytes]),(32,[])):
       edit_estate_list(flag,group)
       method,parameters=mover_receive(estate_details)
       check(method==b'setaccess' and parameters[6:]==expected,'Estate manager can update the allowed-group list')
       with contextlib.closing(sqlite3.connect(root/'estate.db')) as database:
        check(database.execute('SELECT uuid FROM estate_groups WHERE EstateID=100').fetchall()==
              ([(str(group),)] if expected else []),'Estate group membership persists in SQLite')
      extra_user=uuid.UUID('73da8c9a-f610-4d6d-8298-2ad5e2a31ddb')
      for flag,present in ((4,True),(8,False)):
       edit_estate_list(flag,extra_user)
       replies=[mover_receive(estate_details) for _ in range(3)]
       check(all(method==b'setaccess' for method,_ in replies) and
             any(extra_user.bytes in parameters[6:] for _,parameters in replies)==present,
             'Estate manager can add and remove allowed users')
       with contextlib.closing(sqlite3.connect(root/'estate.db')) as database:
        check(bool(database.execute('SELECT 1 FROM estate_users WHERE EstateID=100 AND uuid=?',
                                    (str(extra_user),)).fetchone())==present,
              'Estate user membership persists in SQLite')
      manager_target=uuid.UUID(bytes=observer_id)
      for flag,expected in ((256,[mover_id,observer_id]),(512,[mover_id])):
       edit_estate_list(flag,manager_target)
       method,parameters=mover_receive(estate_details)
       check(method==b'setaccess' and [int(value.rstrip(b'\0')) for value in parameters[:6]]==
             [100,8,0,0,0,len(expected)] and parameters[6:]==expected,
             'Authorized estate administrator can edit the manager list')
       with contextlib.closing(sqlite3.connect(root/'estate.db')) as database:
        check({uuid.UUID(value).bytes for value, in database.execute('SELECT uuid FROM estate_managers WHERE EstateID=100')}==
              set(expected),'Estate manager membership persists in SQLite')
      ban_credentials=dict(credentials,first='Privacy',last='BanTarget')
      ban_socket=cleanup.enter_context(socket.socket(socket.AF_INET,socket.SOCK_DGRAM))
      _,_,_,ban_receive,_=connect_privacy_viewer(ban_socket,'BanTarget',124)
      for flag,present in ((64,True),(128,False)):
       edit_estate_list(flag,ban_target)
       replies=[mover_receive(estate_details) for _ in range(3)]
       bans=[parameters for method,parameters in replies
             if method==b'setaccess' and parameters[1].rstrip(b'\0')==b'4']
       check(len(bans)==1 and (ban_target.bytes in bans[0][6:])==present,
             'Estate ban edits update the viewer ban list')
       with contextlib.closing(sqlite3.connect(root/'estate.db')) as database:
        stored=database.execute('SELECT banningUUID,banTime FROM estateban WHERE EstateID=100 AND bannedUUID=?',
                                (str(ban_target),)).fetchone()
       check((stored is not None)==present and (not present or
             (stored[0]==str(uuid.UUID(bytes=mover_id)) and abs(stored[1]-time.time())<10)),
             'Estate ban edits persist with the issuing manager and timestamp')
       if present:
        ejection_packets=set()
        def ban_ejection(payload):
         if payload[:4]==bytes.fromhex('ffff004a'):ejection_packets.add('teleport failure')
         if payload[:4]==bytes.fromhex('ffff00a3') and b'TP home failed' in payload:ejection_packets.add('kick')
         if len(ejection_packets)==2:return True
        check(ban_receive(ban_ejection),'Connected ban target receives home teleport failure and fallback kick')
        time.sleep(.6)
       ban_login=proxy.login_to_simulator(ban_credentials)
       check(ban_login['login']==('false' if present else 'true'),
             'Estate ban changes take effect in the login path')
      home_socket=cleanup.enter_context(socket.socket(socket.AF_INET,socket.SOCK_DGRAM))
      home_id,home_session,_,home_receive,home_seed=connect_privacy_viewer(home_socket,'HomeTarget',124)
      edit_estate_list(64,home_target)
      for _ in range(3):mover_receive(estate_details)
      def home_finish(payload):
       if payload[:4]==bytes.fromhex('ffff004a'):raise AssertionError(f'Unexpected teleport failure packet {payload!r}')
       if payload[:4]!=bytes.fromhex('ffff0045'):return
       seed_length=struct.unpack_from('<H',payload,38)[0]
       return payload[4:20],struct.unpack_from('<Q',payload,30)[0],struct.unpack_from('<I',payload,41+seed_length)[0]
      agent,handle,flags=home_receive(home_finish)
      check(agent==home_id and handle==(1024<<32)|768 and flags&34==34,
            'Banning a connected viewer with a reachable home issues a ViaHome teleport')
      home_lookup=grid_lookups.get(timeout=5)
      check(home_lookup.get('X')==['1024'] and home_lookup.get('Y')==['768'],
            'Ban-triggered home transfer resolves the home handle on the grid')
      check([transfer_requests.get(timeout=5) for _ in range(3)]==
            [('QUERYACCESS',f'/agent/{home_target}/{destination_id}/'),('POST',f'/agent/{home_target}/'),
             ('PUT',f'/agent/{home_target}/')],'Ban-triggered home transfer negotiates, creates, and updates the remote agent')
      update=transfer_updates.get(timeout=5)
      check(update['agent_uuid']==str(home_target) and update['session_uuid']==str(uuid.UUID(bytes=home_session)) and
            update['position']=='<119, 125, 42>' and update['at_axis']=='<0, 1, 0>',
            'Ban-triggered transfer carries the stored home position and look direction')
      release=urllib.request.Request(update['cb_uri'],method='DELETE')
      with urllib.request.urlopen(release,timeout=5) as response:
       check(response.status==200,'Destination callback confirms the ban-triggered handoff')
      deadline=time.monotonic()+5
      while True:
       try:
        with urllib.request.urlopen(home_seed,timeout=5):pass
       except urllib.error.HTTPError as error:
        if error.code==404:break
        raise
       if time.monotonic()>=deadline:raise AssertionError('Ban-triggered handoff retained the source session')
       time.sleep(.05)
      edit_estate_list(128,home_target)
      for _ in range(3):mover_receive(estate_details)
      cancel_socket=cleanup.enter_context(socket.socket(socket.AF_INET,socket.SOCK_DGRAM))
      cancel_id,cancel_session,cancel_send,cancel_receive,_=connect_privacy_viewer(cancel_socket,'CancelTarget',124)
      edit_estate_list(64,cancel_target)
      for _ in range(3):mover_receive(estate_details)
      cancel_receive(home_finish)
      grid_lookups.get(timeout=5)
      for _ in range(3):transfer_requests.get(timeout=5)
      transfer_updates.get(timeout=5)
      cancel_send(bytes.fromhex('ffff0048')+cancel_id+cancel_session)
      check(transfer_requests.get(timeout=5)==('DELETE',
            f'/agent/{cancel_target}/{destination_id}/?auth={uuid.UUID(bytes=cancel_session)}'),
            'Cancelling a ban-triggered teleport cleans up the destination')
      check(cancel_receive(lambda p: True if p[:4]==bytes.fromhex('ffff00a3') and b'TP home failed' in p else None),
            'Cancelling a ban-triggered teleport sends the fallback kick')
      time.sleep(.6)
      edit_estate_list(128,cancel_target)
      for _ in range(3):mover_receive(estate_details)
      timeout_socket=cleanup.enter_context(socket.socket(socket.AF_INET,socket.SOCK_DGRAM))
      _,timeout_session,_,timeout_receive,timeout_seed=connect_privacy_viewer(timeout_socket,'TimeoutTarget',124)
      edit_estate_list(64,timeout_target)
      for _ in range(3):mover_receive(estate_details)
      timeout_receive(home_finish)
      grid_lookups.get(timeout=5)
      for _ in range(3):transfer_requests.get(timeout=5)
      transfer_updates.get(timeout=5)
      timeout_packets=set()
      def timeout_ejection(payload):
       if payload[:4]==bytes.fromhex('ffff004a') and b'Destination did not confirm teleport arrival' in payload:
        timeout_packets.add('timeout')
       if payload[:4]==bytes.fromhex('ffff00a3') and b'TP home failed' in payload:
        timeout_packets.add('kick')
       if len(timeout_packets)==2:return True
      check(timeout_receive(timeout_ejection,timeout=40),
            'An unconfirmed ban-triggered teleport sends both the timeout and fallback kick')
      check(transfer_requests.get(timeout=5)==('DELETE',
            f'/agent/{timeout_target}/{destination_id}/?auth={uuid.UUID(bytes=timeout_session)}'),
            'An unconfirmed ban-triggered teleport closes the destination circuit')
      deadline=time.monotonic()+5
      while True:
       try:
        with urllib.request.urlopen(timeout_seed,timeout=5):pass
       except urllib.error.HTTPError as error:
        if error.code==404:break
        raise
       if time.monotonic()>=deadline:raise AssertionError('Timed-out estate ejection retained the source session')
       time.sleep(.05)
      check(True,'Timed-out estate ejection removes the source seed')
      edit_estate_list(128,timeout_target)
      for _ in range(3):mover_receive(estate_details)
      changed_covenant=uuid.UUID('11223344-5566-4788-899a-aabbccddeeff')
      method=b'estatechangecovenantid\0'
      parameter=str(changed_covenant).encode()+b'\0'
      mover_send(bytes.fromhex('ffff0104')+mover_id+mover_session+bytes(16)+
                 bytes([len(method)])+method+bytes(16)+bytes([1,len(parameter)])+parameter)
      def edited_covenant(payload):
       if payload[:4]==bytes.fromhex('ffff00cc'):
        return uuid.UUID(bytes=bytes(payload[4:20])),struct.unpack_from('<I',payload,20)[0]
      deadline=time.monotonic()+5
      while True:
       observer_send(bytes.fromhex('ffff00cb')+observer_id+observer_session)
       covenant,timestamp=observer_receive(edited_covenant)
       if covenant==changed_covenant or time.monotonic()>=deadline:break
       time.sleep(.05)
      check(covenant==changed_covenant and abs(timestamp-time.time())<10,
            'Estate manager covenant edit reaches other viewers with a fresh timestamp')
      check(estate_deliveries.get(timeout=5)=={'METHOD':['update_covenant'],'TOKEN':['estate-covenant-fixture'],
            'EstateID':['100'],'CovenantID':[str(changed_covenant)]},'Local edit forwards the covenant to the grid-discovered estate peer')
      with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
       check(database.execute('SELECT covenant,covenant_datetime FROM regionsettings').fetchone()==
             (str(changed_covenant),timestamp),'Estate covenant edit persists with the advertised timestamp')
      remote_covenant=uuid.UUID('88776655-4433-4210-899a-112233445566')
      def estate_update(estate,token):
       body=urllib.parse.urlencode(dict(METHOD='update_covenant',TOKEN=token,EstateID=estate,
                                       CovenantID=str(remote_covenant))).encode()
       with urllib.request.urlopen(urllib.request.Request(url.rstrip('/')+'/estate',body),timeout=5) as response:
        return ET.fromstring(response.read()).findtext('RESULT')
      check(estate_update(100,'wrong')=='False','Estate propagation rejects an invalid token')
      check(estate_update(101,'estate-covenant-fixture')=='True','Valid update for a different estate is acknowledged')
      observer_send(bytes.fromhex('ffff00cb')+observer_id+observer_session)
      check(observer_receive(edited_covenant)==(changed_covenant,timestamp),'Other estate update leaves the local covenant intact')
      check(estate_update(100,'estate-covenant-fixture')=='True','Estate propagation accepts an authenticated update')
      observer_send(bytes.fromhex('ffff00cb')+observer_id+observer_session)
      check(observer_receive(edited_covenant)==(remote_covenant,timestamp),'Remote update changes the covenant ID and preserves the local timestamp')
      check(estate_deliveries.empty(),'Shared estate endpoint receives one request and incoming updates do not echo')
      def request_god(agent,session,send,receive,enable):
       send(bytes.fromhex('ffff0101')+agent+session+bytes([enable])+bytes(16))
       return receive(lambda p: p[36] if len(p)>=53 and p[:4]==bytes.fromhex('ffff0102') else None)
      check(request_god(mover_id,mover_session,mover_send,mover_receive,True)==210,
            'SQLite estate manager can activate local god mode')
      check(request_god(mover_id,mover_session,mover_send,mover_receive,False)==0,
            'Estate manager can leave local god mode')
      def edit_scale(scale):
       mover_send(bytes.fromhex('ffff005c')+mover_id+mover_session+
                  struct.pack('<BI3f',1,prim_id,*scale))
       def match(p):
        result=stored_scale(p)
        if result and result[1]==scale:return True
       observer_receive(match)
      edit_scale((1.,1.,1.))
      edit_scale(original_scale)
      check(True,'Implicit estate god can edit another owner’s prim with viewer god mode off')
      def teleport(agent,session,send,receive,x):
       send(bytes.fromhex('ffff003f')+agent+session+
            struct.pack('<Qffffff',1099511628032000,x,128.,25.,1.,0.,0.))
       receive(lambda p: True if p[:4]==bytes.fromhex('ffff0040') else None)
      teleport(mover_id,mover_session,mover_send,mover_receive,160.)
      def mover_removed(p):
       if len(p)>=2 and p[0]==16 and any(local_id==value[0] for value in struct.iter_unpack('<I',p[2:2+4*p[1]])):
        return True
      observer_receive(mover_removed)
      check(request_god(observer_id,observer_session,observer_send,observer_receive,True)==200,
            'Authorized viewer can activate grid god mode')
      check(observer_receive(mover_object)==local_id,'God mode reveals the hidden avatar across parcels')
      check(request_god(observer_id,observer_session,observer_send,observer_receive,False)==0,'Viewer can leave god mode')
      observer_receive(mover_removed)
      teleport(observer_id,observer_session,observer_send,observer_receive,164.)
      check(observer_receive(mover_object)==local_id,'Entering the private parcel republishes the stationary avatar with the same local ID')
      group_sound=uuid.uuid4().bytes
      mover_send(bytes([29])+group_sound+bytes(48)+struct.pack('<Q4f',1099511628032000,160.,128.,25.,1.))
      check(observer_receive(lambda p: True if p[:17]==bytes([29])+group_sound else None),
            'Remote group membership allows avatar sound on a group-only parcel')
      def god_action(send,god,session,target,mode,reason):
       send(bytes.fromhex('ffff00a5')+god+session+target+struct.pack('<IH',mode,len(reason)+1)+reason+b'\0')
      god_action(observer_send,observer_id,observer_session,mover_id,0,b'lower level')
      mover_receive(lambda p: True if b'ignored, kick reason: lower level' in p else None)
      grid_freeze=dict(from_agent_id=str(uuid.UUID(bytes=mover_id)),to_agent_id=str(uuid.UUID(bytes=observer_id)),
                       dialog='+g==',message='Freeze fixture',message_key='wrong',
                       im_session_id=str(uuid.UUID(int=0)),timestamp='0',from_agent_name='God',from_group='FALSE',
                       offline='AQ==',parent_estate_id='0',position_x='0',position_y='0',position_z='0',
                       region_id=str(uuid.UUID(int=0)),binary_bucket='AQ==')
      check(proxy.grid_instant_message(grid_freeze)['success']=='FALSE','Grid moderation rejects an invalid message key')
      grid_freeze['message_key']='parcel-moderation-fixture'
      check(proxy.grid_instant_message(dict(grid_freeze,from_agent_id=str(uuid.uuid4())))['success']=='FALSE',
            'Grid moderation rejects a sender without destination rights')
      check(proxy.grid_instant_message(grid_freeze)['success']=='TRUE','Grid moderation accepts an authorized sender')
      observer_receive(lambda p: True if b'Freeze fixture' in p else None)
      seed_request=urllib.request.Request(mover_seed,b'<llsd><array><string>UntrustedSimulatorMessage</string></array></llsd>',
                                          {'Content-Type':'application/llsd+xml'})
      with urllib.request.urlopen(seed_request,timeout=5) as response:
       cap_fields=list(ET.fromstring(response.read()).find('map'))
      cap_url={cap_fields[i].text:cap_fields[i+1].text for i in range(0,len(cap_fields),2)}['UntrustedSimulatorMessage']
      def cap_moderation(session):
       body=f'''<llsd><map><key>message</key><string>GodKickUser</string>
        <key>body</key><map><key>UserInfo</key><array><map>
        <key>GodID</key><uuid>{uuid.UUID(bytes=mover_id)}</uuid>
        <key>GodSessionID</key><uuid>{session}</uuid>
        <key>AgentID</key><uuid>{uuid.UUID(bytes=observer_id)}</uuid>
        <key>KickFlags</key><integer>2</integer><key>Reason</key><string>Unfreeze fixture</string>
        </map></array></map></map></llsd>'''.encode()
       try:
        with urllib.request.urlopen(urllib.request.Request(cap_url,body,{'Content-Type':'application/llsd+xml'}),timeout=5) as response:
         return response.status
       except urllib.error.HTTPError as error:
        return error.code
      check(cap_moderation(uuid.uuid4())==401,'Moderation capability rejects a mismatched session')
      check(cap_moderation(uuid.UUID(bytes=mover_session))==200,'Moderation capability accepts the active sender session')
      observer_receive(lambda p: True if b'Unfreeze fixture' in p else None)
      god_action(mover_send,mover_id,mover_session,observer_id,0,b'Kick fixture')
      observer_receive(lambda p: True if p[:4]==bytes.fromhex('ffff00a3') and b'Kick fixture' in p else None)
      check(True,'God moderation honors rank and delivers freeze, unfreeze, and kick packets')
      mover_send(bytes.fromhex('ffff00fc')+mover_id+mover_session)
      # Allow the simulator's delayed kick cleanup before the main probe joins.
      time.sleep(.6)
    probe(login)
    if a.script_url_denied or a.script_url_hostname or a.script_url_quota:
     print(f'PASS: {len(checks)} script URL hostname network checks')
     return
    if a.primary_https:
     print(f'PASS: {len(checks)} primary HTTPS region network checks')
     return
    if a.remote_mutes:
     check([fields['METHOD'] for fields in remote_calls] == ['update','get','get','delete','get'] and
           int(remote_calls[2]['mutecrc']) != 0 and not remote_entries,
           'Authenticated remote mute-list service received update, CRC lookup, and deletion')
    elif a.raw_fps:
     with contextlib.closing(sqlite3.connect(root/'mutes.db')) as database:
      check(database.execute("SELECT version FROM migrations WHERE name='MuteListStore'").fetchall() == [(1,)],
            'SQLite mute lists record the original .NET migration version')
      check(database.execute("SELECT COUNT(*) FROM MuteList").fetchone() == (0,),
            'Viewer removal deletes the stored mute row')
   # Scene logout disposes seed routes and removes session state; same account can log in again.
   deadline=time.monotonic()+5
   while True:
    try:
     with urllib.request.urlopen(request,timeout=5) as response:response.read()
    except urllib.error.HTTPError as e:
     if e.code==404:break
     raise
    if time.monotonic()>deadline:raise AssertionError('Logout retained seed route')
   if a.presence_provider:
    check(presence_request(METHOD='getagent', SessionID=login['session_id']).findtext('result') == 'null',
          f'{a.presence_provider} presence removes the viewer session on logout')
   if a.grid_user_provider:
    deadline = time.monotonic() + 5
    while True:
     grid_user = grid_user_record()
     if grid_user.findtext('Online') == 'False':
      break
     if time.monotonic() >= deadline:
      raise AssertionError(f'{a.grid_user_provider} grid-user store retained online viewer after logout')
     time.sleep(.025)
    check(grid_user.findtext('LastRegionID') != str(uuid.UUID(int=0)),
          f'{a.grid_user_provider} grid-user store records viewer logout location')
   if a.disable_scripts:
    again=proxy.login_to_simulator(credentials)
    check(again.get('login')=='true' and again['session_id']!=login['session_id'],'Fresh session after enabling scripts')
    result=subprocess.run([str(Path(a.viewer).resolve()),again['agent_id'],again['session_id'],
                           str(again['circuit_code']),again['sim_ip'],str(again['sim_port']),
                           '--check-scripting-enabled'],capture_output=True,text=True,timeout=15)
    check(result.returncode==0,'Reconnect handshake reflects live scripting state: '+result.stderr)
    print("PASS: persisted SkipScripts suppresses events until the console enables scripting")
    return
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
   if a.physics_library:
    observer_credentials=dict(credentials,first='Physics',last='Observer',start='uri:Connected Native&190&190&25')
    deadline=time.monotonic()+5
    while True:
     restored=proxy.login_to_simulator(observer_credentials)
     if restored.get('login')=='true':break
     check(time.monotonic()<deadline,'Appearance owner can log in again after logout')
     time.sleep(.05)
    check(restored['session_id']!=observer['session_id'],'Appearance owner reconnects with a fresh session')
    agent=uuid.UUID(restored['agent_id']).bytes
    session_id=uuid.UUID(restored['session_id']).bytes
    code=struct.pack('<I',int(restored['circuit_code']) & 0xffffffff)
    endpoint=(restored['sim_ip'],restored['sim_port'])
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
     listener.settimeout(5)
     listener.sendto(struct.pack('>BIBHH',0x40,1,0,0xffff,3)+code+session_id+agent,endpoint)
     listener.sendto(struct.pack('>BIBHH',0x40,2,0,0xffff,249)+agent+session_id+code,endpoint)
     for _ in range(64):
      if listener.recv(4096)[6:10]==bytes.fromhex('ffff00fa'):break
     else:raise AssertionError('Appearance owner failed to reenter the physics region')
     probe(again,restored['agent_id'],verify_hover=True,warm_notecard=True)
     listener.sendto(struct.pack('>BIBHH',0x40,3,0,0xffff,252)+agent+session_id,endpoint)
   if a.touch and not a.physics_library and not a.disable_scripts:
    deadline=time.monotonic()+65
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
     while database.execute('SELECT 1 FROM prims WHERE UUID=?',(temporary_prim,)).fetchone():
      check(time.monotonic()<deadline,'Temporary prim is removed from region storage before the deadline')
      time.sleep(.25)
     check(database.execute('SELECT 1 FROM primshapes WHERE UUID=?',(temporary_prim,)).fetchone() is None and
           database.execute('SELECT 1 FROM primitems WHERE primID=?',(temporary_prim,)).fetchone() is None,
           'Temporary prim, shape and task inventory are deleted from backup together')
   if a.advanced_profiles:
    check(offline_form(**message).findtext('RESULT')=='True','Native offline endpoint queues a message before shutdown')
  finally:
   if proc.poll() is None:
    proc.stdin.write('"quit"\n');proc.stdin.flush()
    try:proc.wait(timeout=15)
    except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
   check(proc.returncode==0,'Clean region/socket/capability shutdown: '+''.join(errors))
  if a.touch:
   if not a.physics_library:
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
     stored_name=database.execute('SELECT Name FROM prims WHERE UUID=?',(stored_child,)).fetchone()[0]
     check(stored_name==('Stored part' if a.disable_scripts else 'Restored script executed'),
           'Native metadata write-back preserves the restored script rename')
     sound,volume=database.execute('SELECT CollisionSound,CollisionSoundVolume FROM prims WHERE UUID=?',
                                   (stored_child,)).fetchone()
     check(sound=='ffffffff-ffff-ffff-ffff-ffffffffffff' and float(volume)==0.,
           'Empty collision sound disables playback and clamps negative volume')
     stored_position=database.execute('SELECT GroupPositionX FROM prims WHERE UUID=?',(stored_root,)).fetchone()[0]
     check(float(stored_position)==-12,'Native transform write-back saves the moved linkset root')
     group,x,y,z=database.execute('SELECT SceneGroupID,GroupPositionX,GroupPositionY,GroupPositionZ '
                                  'FROM prims WHERE UUID=?',(stored_child,)).fetchone()
     check(group==stored_child and tuple(map(float,(x,y,z)))==(-11.,130.,28.),
           'Native linkset write-back saves a detached child as a root at its world position')
     material,action=database.execute('SELECT Material,ClickAction FROM prims WHERE UUID=?',(stored_child,)).fetchone()
     check((int(material),int(action))==(2,2),'Native metadata write-back saves viewer material and click action edits')
     geometry=database.execute('SELECT PathTwist,ProfileHollow FROM primshapes WHERE UUID=?',(stored_child,)).fetchone()
     check(tuple(map(int,geometry))==(-8,1200),'Native shape write-back saves viewer geometry edits')
     root_shape=database.execute('SELECT PCode,PathTwist,ProfileHollow FROM primshapes WHERE UUID=?',
                                (stored_root,)).fetchone()
     check(root_shape is not None and tuple(map(int,root_shape))==(9,-8,1200),
           'Editing a legacy prim without a shape row creates its complete shape')
     flags=database.execute('SELECT ObjectFlags FROM prims WHERE UUID=?',(stored_child,)).fetchone()[0]
     check(int(flags)&0x400,'Native prim write-back saves the viewer phantom flag')
     expected_light=bytes([1,32,0,16,0,0,0,0,255,255,255])+struct.pack('<fff',8.,.5,1.)
     check(database.execute('SELECT ExtraParams FROM primshapes WHERE UUID=?',(stored_child,)).fetchone()==(expected_light,),
           'Viewer replacement of the light parameter persists in SQLite')
     rezzed=database.execute("SELECT p.UUID,p.SceneGroupID,p.OwnerID,s.PCode,s.PathTwist "
                            "FROM prims p JOIN primshapes s ON s.UUID=p.UUID WHERE p.UUID=?",
                            (primary_viewer_prim,)).fetchone()
     check(rezzed is not None and rezzed[0]==rezzed[1] and rezzed[2]==principal and
           tuple(map(int,rezzed[3:]))==(9,-12 if a.disable_scripts else -15),
           f'Viewer-rezzed prim and shape are persisted together: {rezzed!r}')
     texture=database.execute('SELECT Texture FROM primshapes WHERE UUID=?',(rezzed[0],)).fetchone()[0]
     label=database.execute('SELECT Text,ColorA,ColorR,ColorG,ColorB FROM prims WHERE UUID=?',(rezzed[0],)).fetchone()
     check(label[0]=='Script label' and tuple(map(int,label[1:]))==(127,255,127,0),
           'Script floating text and clamped color persist in SQLite')
     check(database.execute('SELECT TouchName,SitName FROM prims WHERE UUID=?',(rezzed[0],)).fetchone()
           ==('Activate ','Take seat'),'Script interaction labels persist with the nine-unit limit')
     sound,gain=database.execute('SELECT LoopedSound,LoopedSoundGain FROM prims WHERE UUID=?',(rezzed[0],)).fetchone()
     check(uuid.UUID(sound).int==0 and float(gain)==0,
           'Script sound stop clears the saved sound and gain')
     sound,volume=database.execute('SELECT CollisionSound,CollisionSoundVolume FROM prims WHERE UUID=?',
                                   (rezzed[0],)).fetchone()
     check(sound=='12345678-1234-4234-8234-123456789abc' and float(volume)==.625,
           'Script collision sound asset and volume persist in SQLite')
     prices=database.execute('SELECT PayPrice,PayButton1,PayButton2,PayButton3,PayButton4 FROM prims WHERE UUID=?',
                             (rezzed[0],)).fetchone()
     check(tuple(map(int,prices))==(-2,5,10,-1,-1),'Script pay prices persist and omitted buttons are hidden')
     check(isinstance(texture,bytes) and b'\0' in texture,
           'Viewer texture entry persists as binary SQLite data including embedded zero bytes')
     scripts=database.execute('SELECT itemID,assetID,ownerID FROM primitems WHERE primID=? AND invType=10 '
                              "AND typeof(assetType)='integer' AND typeof(creationDate)='integer' AND typeof(flags)='integer'",
                              (rezzed[0],)).fetchall()
     check((not scripts if removed_task_items else len(scripts)>=2)
           and all(uuid.UUID(item).int and uuid.UUID(asset).int and owner==principal
                                  for item,asset,owner in scripts),
           f'Viewer-rezzed prim retains its remaining task scripts in SQLite: {scripts!r}')
     for item in removed_task_items:
      check(database.execute('SELECT 1 FROM primitems WHERE itemID=?',(item,)).fetchone() is None,
            'Removed task inventory item stays deleted in SQLite')
     friction,restitution=database.execute('SELECT Friction,Restitution FROM prims WHERE UUID=?',(stored_child,)).fetchone()
     check(abs(float(friction)-.2)<1e-6 and abs(float(restitution)-.7)<1e-6,
           'Native write-back saves physics coefficients updated by material edits')
     for table,height in (('terrain',24.),('bakedterrain',21.)):
      revision,blob=database.execute(f'SELECT Revision,Heightfield FROM {table}').fetchone()
      payload=gzip.decompress(blob)
      check(revision==23 and struct.unpack_from('<II',payload)==(256,256)
            and struct.unpack_from('<f',payload,8+4*(106*256+106))[0]==height,
            f'{table} stores the expected revision 23 height')
      check(struct.unpack_from('<f',payload,8+4*(107*256+107))[0]==(27. if table=='terrain' else 24.),
            f'{table} retains the height loaded from the older database format')
      if table=='terrain':
       saved_terrain=root/'expected-terrain.gz'
       saved_terrain.write_bytes(blob)
      else:
       original_baseline=blob
   registration=grid_lifecycle.get_nowait()
   deregistration=grid_lifecycle.get_nowait()
   check(registration['METHOD']==['register'] and registration['VERSIONMIN']==['1']
         and registration['VERSIONMAX']==['1'] and int(registration['serverPort'][0])>0
         and int(registration['serverHttpPort'][0])>0,
         'Native region registers its bound endpoints and protocol version with the remote grid')
   check(deregistration=={'METHOD':['deregister'],'REGIONID':registration['uuid']},
         'Native region deregisters the same UUID on shutdown')
   if not a.physics_library:
    check(grid_lookups.get_nowait()=={'METHOD':['get_region_by_position'],'X':['512'],'Y':['768'],
          'SCOPEID':registration['SCOPEID']},'Remote viewer teleport looks up the requested grid coordinates')
    check(grid_lookups.get_nowait()['X']==['1024'],'Second viewer teleport resolves the remote region')
    check(grid_lookups.empty(),'Duplicate teleport is rejected without another remote lookup')
    check(transfer_requests.get_nowait()==('QUERYACCESS',f"/agent/{login['agent_id']}/{destination_id}/"),
          'Remote lookup result supplies the destination URL and UUID for access negotiation')
    check(transfer_requests.get_nowait()==('POST',f"/agent/{login['agent_id']}/"),
          'Accepted access leads to a remote agent creation request')
    check(transfer_requests.get_nowait()==('PUT',f"/agent/{login['agent_id']}/"),
          'The source sends the avatar state after remote agent creation')
    check(transfer_requests.get_nowait()==('DELETE',
          f"/agent/{login['agent_id']}/{destination_id}/?auth={login['session_id']}"),
          'Viewer cancellation closes the destination circuit with the current session ID')
  if a.touch and not a.physics_library:
   restart_linkset=[]
   if rotating_prim and not a.disable_scripts:
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
     prim,asset_id=database.execute(
         'SELECT p.UUID,i.assetID FROM prims p JOIN primitems i ON i.primID=p.UUID '
         'WHERE p.lnkstBinData=? AND i.invType=10',
         (bytes([1,0x80,0x20,3])+b'\x07persist\x05value\x00\x05extra\x03yes\x00\x01z\x01x\x00',)).fetchone()
     database.execute('UPDATE prims SET BaseMask=BaseMask|16384,OwnerMask=OwnerMask|16384 WHERE SceneGroupID=?',(prim,))
     database.execute('UPDATE prims SET StartStr=? WHERE UUID=?',('stored-start',prim))
     target=database.execute('SELECT UUID FROM prims WHERE CAST(GroupPositionX AS REAL)=130 '
       'AND CAST(GroupPositionY AS REAL)=132 AND CAST(GroupPositionZ AS REAL)=30.25').fetchone()[0]
     database.execute('UPDATE prims SET lnkstBinData=? WHERE UUID=?',
       (bytes([1,22,1])+b'\x07persist\x06target\x00',target))
    with contextlib.closing(sqlite3.connect(root/'assets.db')) as database, database:
     database.execute('UPDATE assets SET Data=? WHERE UUID=?',
       (b'default { touch_start(integer n) { if (llGetLinkNumber() == 1 && llDetectedLinkNumber(0) == 2) { key child = llGetLinkKey(2); if (child && llGetObjectLinkKey(child, 1) == llGetKey() && llGetObjectLinkKey(child, LINK_THIS) == child && llGetObjectPermMask(99) == -1) llSay(0, "Touched link 2"); return; } llSay(0, "Restart linkset " + llGetStartString() + ":" + llLinksetDataRead("persist") + "/" + llLinksetDataRead("extra") + "/" + llLinksetDataRead("z")); } }',asset_id))
    restart_linkset=['--linkset-target',target,'--linkset-data',prim]
    with contextlib.closing(sqlite3.connect(root/'estate.db')) as database, database:
     database.execute('ALTER TABLE estate_settings ADD COLUMN PublicAccess INTEGER NOT NULL DEFAULT 0')
     database.execute('INSERT INTO estate_users VALUES (100,?)',(str(home_target),))
     database.execute('INSERT INTO estate_groups VALUES (100,?)',(estate_access_group,))
   restart=subprocess.Popen(cmd+['--console'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,text=True,encoding='utf-8')
   output=queue.Queue()
   def read_terrain_restart():
    for line in restart.stdout:output.put(line)
    output.put(None)
   threading.Thread(target=read_terrain_restart,daemon=True).start()
   try:
    deadline=time.monotonic()+20
    while True:
     line=output.get(timeout=max(.01,deadline-time.monotonic()))
     if line is None:raise AssertionError('Terrain restart failed before HTTP startup')
     if line.startswith('LISTENING '):break
    restart_proxy=xmlrpc.client.ServerProxy(line.split(' ',1)[1].strip())
    if rotating_prim and not a.disable_scripts:
     private_start='uri:Connected Native&124&128&25'
     check(restart_proxy.login_to_simulator(dict(credentials,first='Privacy',last='BanTarget',
                                                start=private_start))['login']=='false',
           'Private estate rejects a user outside its access list')
     check(restart_proxy.login_to_simulator(dict(credentials,first='Privacy',last='HomeTarget',
                                                start=private_start))['login']=='true',
           'Private estate admits a user on the persisted access list')
     check(restart_proxy.login_to_simulator(dict(credentials,first='Privacy',last='CancelTarget',
                                                start=private_start))['login']=='true',
           'Private estate admits a member of an allowed group')
     requests=[]
     while not group_requests.empty():requests.append(group_requests.get_nowait())
     check(any(fields=={'METHOD':['GETMEMBERSHIP'],'ALL':['true'],'AgentID':[str(cancel_target)],
                       'RequestingAgentID':[str(uuid.UUID(int=0))]} for fields in requests),
           'Private estate queries the authenticated Groups service for memberships')
    session=restart_proxy.login_to_simulator(credentials)
    check(session.get('login')=='true','Viewer logs in after terrain restart')
    result=subprocess.run([str(Path(a.viewer).resolve()),session['agent_id'],session['session_id'],
                           str(session['circuit_code']),session['sim_ip'],str(session['sim_port']),
                           '--terrain-only',str(saved_terrain),'--bake',rezzed[0],stored_child,texture.hex(),
                           *(value for item,asset,_ in scripts for value in (item,asset)),*restart_linkset],
                          capture_output=True,text=True,timeout=45)
    check(result.returncode==0,'Restart sends the saved terrain to a fresh viewer: '+result.stderr)
    uploaded=bytearray(gzip.decompress(saved_terrain.read_bytes()))
    struct.pack_into('<fffff',uploaded,8,0.,0.,0.,42.,4095.)
    deadline=time.monotonic()+10
    with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
     while True:
      revision,blob=database.execute('SELECT Revision,Heightfield FROM bakedterrain').fetchone()
      if revision==23 and gzip.decompress(blob)==uploaded:break
      check(time.monotonic()<deadline,'Viewer terrain upload persists the new baked baseline')
      time.sleep(.05)
   finally:
    if restart.poll() is None:
     restart.stdin.write('"quit"\n');restart.stdin.flush()
     try:restart.wait(timeout=15)
     except subprocess.TimeoutExpired:restart.kill();restart.wait();raise
   check(restart.returncode==0,'Terrain restart shuts down cleanly')
   fresh_config=root/'fresh-region.ini'
   fresh_settings=config.read_text(encoding='utf-8').replace(
       'TerrainDatabase = terrain.db\nPhysics = ubODE\nPhysicsLibrary = deliberately-missing-ubode',
       'TerrainDatabase = fresh-region.db')
   check_local_groups=rotating_prim and not a.disable_scripts
   if check_local_groups:
    while not group_requests.empty():group_requests.get_nowait()
    orphan_group='fc64b140-cc4a-42d0-a44b-0431772c3c15'
    with contextlib.closing(sqlite3.connect(root/'groups.db')) as database, database:
     database.execute('CREATE TABLE os_groups_groups (GroupID CHAR(36) PRIMARY KEY, Name TEXT)')
     database.execute('CREATE TABLE os_groups_membership (GroupID CHAR(36), PrincipalID VARCHAR(255), SelectedRoleID CHAR(36), AcceptNotices INT DEFAULT 1, ListInProfile INT DEFAULT 0, PRIMARY KEY(GroupID,PrincipalID))')
     database.execute('CREATE TABLE os_groups_roles (GroupID CHAR(36), RoleID CHAR(36), Powers TEXT, Name TEXT, Title TEXT, PRIMARY KEY(GroupID,RoleID))')
     database.execute('CREATE TABLE os_groups_rolemembership (GroupID CHAR(36), RoleID CHAR(36), PrincipalID VARCHAR(255), PRIMARY KEY(GroupID,RoleID,PrincipalID))')
     database.execute('CREATE TABLE os_groups_principals (PrincipalID VARCHAR(255) PRIMARY KEY, ActiveGroupID CHAR(36))')
     role=str(uuid.uuid4())
     alternate_role=str(uuid.uuid4())
     database.execute('INSERT INTO os_groups_groups VALUES (?,?)',(estate_access_group,'Local Parcel Group'))
     database.executemany('INSERT INTO os_groups_roles VALUES (?,?,?,?,?)',
                          ((estate_access_group,role,str((1<<64)-1),'Builders','Local Builder'),
                           (estate_access_group,alternate_role,'42','Hosts','Local Host')))
     database.executemany('INSERT INTO os_groups_rolemembership VALUES (?,?,?)',
                          ((estate_access_group,value,str(timeout_target)) for value in (role,alternate_role)))
     database.executemany('INSERT INTO os_groups_membership (GroupID,PrincipalID,SelectedRoleID) VALUES (?,?,?)',
                          ((estate_access_group,str(timeout_target),role),(orphan_group,str(ban_target),role)))
    with contextlib.closing(sqlite3.connect(root/'estate.db')) as database, database:
     database.execute('INSERT INTO estate_groups VALUES (100,?)',(orphan_group,))
    fresh_settings=fresh_settings.replace('ServicesConnectorModule=Groups Remote Service Connector',
                                          'ServicesConnectorModule=Groups Local Service Connector\nSQLiteMembershipDatabase=groups.db')
   fresh_config.write_text(fresh_settings,encoding='utf-8')
   fresh=subprocess.run([cmd[0],'--config',str(fresh_config),'--console'],input='"quit"\n',
                        capture_output=True,text=True,timeout=20)
   check(fresh.returncode==0,'Fresh SQLite region starts and shuts down: '+fresh.stderr)
   with contextlib.closing(sqlite3.connect(root/'fresh-region.db')) as database:
    check(database.execute("SELECT version FROM migrations WHERE name='RegionStore'").fetchone()==(43,),
          'Fresh region uses the current OpenSimulator SQLite migration version')
    for table in ('prims','primshapes','primitems'):
     check(database.execute(f'SELECT count(*) FROM {table}').fetchone()==(0,),
           f'Fresh region initializes the empty {table} table')
   def fresh_viewer(mode,*args):
    nonlocal check_local_groups
    server=subprocess.Popen([cmd[0],'--config',str(fresh_config),'--console'],
                            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
     lines=queue.Queue()
     def read_fresh():
      for text in server.stdout:lines.put(text)
      lines.put(None)
     threading.Thread(target=read_fresh,daemon=True).start()
     deadline=time.monotonic()+20
     while True:
      line=lines.get(timeout=max(.01,deadline-time.monotonic()))
      check(line is not None,'Fresh region reaches HTTP startup')
      if line.startswith('LISTENING '):break
     fresh_proxy=xmlrpc.client.ServerProxy(line.split(' ',1)[1].strip())
     if rotating_prim and not a.disable_scripts:
      private_start='uri:Connected Native&124&128&25'
      local_login=fresh_proxy.login_to_simulator(dict(credentials,first='Privacy',last='TimeoutTarget',start=private_start))
      check(local_login['login']=='true',
            'Private estate admits a member from the local Groups database')
      group_id=uuid.UUID(estate_access_group)
      cleared=(bytes(16),0,b'',b'')
      selected=(group_id.bytes,(1<<64)-1,b'Local Builder',b'Local Parcel Group')
      alternate=(group_id.bytes,42,b'Local Host',b'Local Parcel Group')
      with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as local_socket:
       agent,session_id,send,receive,_=connect_privacy_viewer(local_socket,'TimeoutTarget',124,local_login)
       check(receive(active_group_reply)==(cleared if check_local_groups else alternate),
             'Region entry restores the active group and title from SQLite after restart')
       desired=selected if check_local_groups else cleared
       send(bytes.fromhex('ffff0170')+agent+session_id+desired[0])
       check(receive(active_group_reply)==desired,'Local active group change reaches the viewer')
       if check_local_groups:
        def read_titles():
         request=uuid.uuid4().bytes
         send(bytes.fromhex('ffff0177')+agent+session_id+group_id.bytes+request)
         def titles_reply(payload):
          if payload[:52]!=bytes.fromhex('ffff0178')+agent+group_id.bytes+request:return
          index=53
          rows={}
          for _ in range(payload[52]):
           end=index+1+payload[index]
           title=bytes(payload[index+1:end]).rstrip(b'\0')
           rows[str(uuid.UUID(bytes=bytes(payload[end:end+16])))]=(title,payload[end+16])
           index=end+17
          return rows
         return receive(titles_reply)
        check(read_titles()=={role:(b'Builders',1),alternate_role:(b'Hosts',0)},
              'Group title reply lists assigned roles and the selected role')
        send(bytes.fromhex('ffff0179')+agent+session_id+group_id.bytes+uuid.UUID(alternate_role).bytes)
        check(receive(active_group_reply)==alternate,'Selecting a group title updates the active title and powers')
        check(read_titles()=={role:(b'Builders',0),alternate_role:(b'Hosts',1)},
              'Group title reply reflects the changed selection')
        send(bytes.fromhex('ffff0172')+agent+session_id+group_id.bytes+bytes([0,1]))
        send(bytes.fromhex('ffff0182')+agent+session_id)
        check(receive(active_group_reply)==alternate,'Membership preference edit preserves the active group')
       with contextlib.closing(sqlite3.connect(root/'groups.db')) as database:
        check(database.execute('SELECT ActiveGroupID FROM os_groups_principals WHERE PrincipalID=?',
                               (str(timeout_target),)).fetchone()==(str(uuid.UUID(bytes=desired[0])),),
              'Local active group change persists in SQLite')
        check(database.execute('SELECT SelectedRoleID FROM os_groups_membership WHERE PrincipalID=?',
                               (str(timeout_target),)).fetchone()==(alternate_role,),
              'Selected group title persists independently of the active group')
        check(database.execute('SELECT AcceptNotices,ListInProfile FROM os_groups_membership WHERE PrincipalID=?',
                               (str(timeout_target),)).fetchone()==(0,1),
              'Local membership preferences persist across region restart')
       send(bytes.fromhex('ffff00fc')+agent+session_id)
       receive(lambda p: True if p[:4]==bytes.fromhex('ffff00fd') else None)
     if check_local_groups:
      check(fresh_proxy.login_to_simulator(dict(credentials,first='Privacy',last='BanTarget',
                                               start=private_start))['login']=='false',
            'An orphaned local membership does not grant private-estate access')
      check(group_requests.empty(),'Local Groups admission does not call the remote connector')
      check_local_groups=False
     session=fresh_proxy.login_to_simulator(credentials)
     check(session.get('login')=='true','Viewer logs into the fresh region')
     result=subprocess.run([str(Path(a.viewer).resolve()),session['agent_id'],session['session_id'],
                            str(session['circuit_code']),session['sim_ip'],str(session['sim_port']),mode,*args],
                           capture_output=True,text=True,timeout=20)
     check(result.returncode==0,'Fresh region viewer '+mode+' succeeds: '+result.stderr)
     return result.stdout.strip().splitlines()[-1]
    finally:
     if server.poll() is None:
      server.stdin.write('"quit"\n');server.stdin.flush()
      try:server.wait(timeout=15)
      except subprocess.TimeoutExpired:server.kill();server.wait();raise
     check(server.returncode==0,'Fresh region viewer session shuts down cleanly: '+server.stderr.read())
   rezzed_id=fresh_viewer('--rez-persistent')
   with contextlib.closing(sqlite3.connect(root/'fresh-region.db')) as database, database:
    children=database.execute('SELECT p.UUID,s.ScaleX,s.ScaleY,s.ScaleZ FROM prims p '
                              'JOIN primshapes s ON s.UUID=p.UUID WHERE p.SceneGroupID=? AND p.UUID<>?',
                              (rezzed_id,rezzed_id)).fetchall()
    check(len(children)==1 and children[0][1:]==(.5,.75,1.25),
          'Fresh region saves a new linked child and its shape with the root')
    child_id=children[0][0]
    motion=database.execute('SELECT VelocityX,VelocityY,VelocityZ,AngularVelocityX,AngularVelocityY,AngularVelocityZ,'
                            'AccelerationX,AccelerationY,AccelerationZ,SitName,TouchName FROM prims WHERE UUID=?',
                            (rezzed_id,)).fetchone()
    check(motion==(0.,)*9+('',''),'New prim stores zero motion vectors and empty sit and touch labels')
    defaults=database.execute('SELECT p.Text,s.Shape,s.ExtraParams FROM prims p JOIN primshapes s ON s.UUID=p.UUID '
                              'WHERE p.UUID=?',(rezzed_id,)).fetchone()
    check(defaults==('',0,b'\0'),'New prim supplies the original SQLite reader’s required text and shape defaults')
    prices=database.execute('SELECT PayPrice,PayButton1,PayButton2,PayButton3,PayButton4 FROM prims WHERE UUID=?',
                            (rezzed_id,)).fetchone()
    check(prices==(-2,)*5,'New prim stores the original default payment buttons')
    database.execute('ALTER TABLE prims DROP COLUMN StartStr')
    database.execute("UPDATE migrations SET version=42 WHERE name='RegionStore'")
   check(fresh_viewer('--restore-persistent',rezzed_id,child_id)==rezzed_id,
         'Fresh region restores the viewer-rezzed linkset UUIDs, scales and parent relationship after restart')
   with contextlib.closing(sqlite3.connect(root/'fresh-region.db')) as database:
    check(database.execute("SELECT version FROM migrations WHERE name='RegionStore'").fetchone()==(43,)
          and database.execute('SELECT StartStr FROM prims WHERE UUID=?',(rezzed_id,)).fetchone()==(None,),
          'Restart upgrades the existing versioned region database while preserving its prim')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('UPDATE terrain SET Heightfield=?',(original_baseline,))
   reverted=subprocess.run(cmd+['--console'],input='terrain revert\n"quit"\n',
                           capture_output=True,text=True,timeout=20)
   check(reverted.returncode==0,'Terrain revert server shuts down cleanly: '+reverted.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    revision,blob=database.execute('SELECT Revision,Heightfield FROM terrain').fetchone()
    check(revision==23 and gzip.decompress(blob)==uploaded,
          'Console revert restores and saves the baseline uploaded to the previous server')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('DELETE FROM terrain')
   recovered=subprocess.run(cmd+['--console'],input='"quit"\n',
                             capture_output=True,text=True,timeout=20)
   check(recovered.returncode==0,'Baked-only terrain server shuts down cleanly: '+recovered.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    revision,blob=database.execute('SELECT Revision,Heightfield FROM terrain').fetchone()
    check(revision==23 and gzip.decompress(blob)==uploaded,
          'Startup rebuilds missing active terrain from the baked baseline')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('UPDATE terrain SET Heightfield=?',(b'corrupt gzip',))
   repaired=subprocess.run(cmd+['--console'],input='"quit"\n',
                           capture_output=True,text=True,timeout=20)
   check(repaired.returncode==0,'Corrupt active terrain server shuts down cleanly: '+repaired.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    revision,blob=database.execute('SELECT Revision,Heightfield FROM terrain').fetchone()
    check(revision==23 and gzip.decompress(blob)==uploaded,
          'Startup repairs corrupt active terrain from the baked baseline')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('UPDATE bakedterrain SET Heightfield=?',(b'corrupt gzip',))
   rebaked=subprocess.run(cmd+['--console'],input='terrain revert\n"quit"\n',
                          capture_output=True,text=True,timeout=20)
   check(rebaked.returncode==0,'Corrupt baked terrain server shuts down cleanly: '+rebaked.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    for table in ('terrain','bakedterrain'):
     revision,blob=database.execute(f'SELECT Revision,Heightfield FROM {table}').fetchone()
     check(revision==23 and gzip.decompress(blob)==uploaded,
           f'Startup and revert preserve valid heights in {table} after repairing baked terrain')
   flipped=subprocess.run(cmd+['--console'],input='terrain flip X\nterrain flip y\n"quit"\n',
                          capture_output=True,text=True,timeout=20)
   check(flipped.returncode==0,'Terrain flip server shuts down cleanly: '+flipped.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    active=database.execute('SELECT Heightfield FROM terrain').fetchone()[0]
    baked=database.execute('SELECT Heightfield FROM bakedterrain').fetchone()[0]
    expected=uploaded[:8]+b''.join(uploaded[i:i+4] for i in range(len(uploaded)-4,7,-4))
    check(gzip.decompress(active)==expected and gzip.decompress(baked)==uploaded,
          'Console flips mirror both axes while preserving the baked baseline')
   adjusted=subprocess.run(cmd+['--console'],
     input='terrain fill " 2e1 "\nterrain elevate +5\nterrain lower 2\nterrain multiply 2\nterrain min 45\nterrain max 47\n"quit"\n',
     capture_output=True,text=True,timeout=20)
   check(adjusted.returncode==0,'Terrain adjustment server shuts down cleanly: '+adjusted.stderr)
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    active=database.execute('SELECT Heightfield FROM terrain').fetchone()[0]
    check(gzip.decompress(active)==uploaded[:8]+struct.pack('<f',46.)*(256*256),
          'Console height adjustments persist the expected heightmap')
   clamped=subprocess.run(cmd+['--console'],
     input='terrain min 48\nterrain bake\nterrain max 47\nterrain show 12.5,13\nterrain show 256,0\nterrain stats\n"quit"\n',
     capture_output=True,text=True,timeout=20)
   check(clamped.returncode==0,'Terrain clamp server shuts down cleanly: '+clamped.stderr)
   check('Terrain height at <12.5, 13> is 47' in clamped.stdout and
         'Terrain height at <256, 0> is 0' in clamped.stdout,
         'Terrain show reports an in-range sample and zero beyond the region')
   check('Channel 256x256' in clamped.stdout and 'max/min/avg/sum: 47/47/47/3080192' in clamped.stdout,
         'Terrain stats reports channel size and height statistics')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    for table,height in (('terrain',47.),('bakedterrain',48.)):
     blob=database.execute(f'SELECT Heightfield FROM {table}').fetchone()[0]
     check(gzip.decompress(blob)==uploaded[:8]+struct.pack('<f',height)*(256*256),
           f'Console clamp persists the expected height in {table}')
   flat_config=config.read_text(encoding='utf-8')
   config.write_text(flat_config.replace('InitialTerrain = flat\n',''),encoding='utf-8')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('DELETE FROM terrain')
    database.execute('DELETE FROM bakedterrain')
   raw_export=root/'terrain export.raw'
   float_export=root/'terrain export.r32'
   terragen_export=root/'terrain export.ter'
   bitmap_export=root/'terrain export.bmp'
   png_export=root/'terrain export.png'
   failed_export=root/'missing directory'/'terrain.r32'
   over_limit=root/'height limit.bmp'
   island=subprocess.run(cmd+['--console'],
     input=f'terrain rescale 10 60\nterrain show 128,128\nterrain save "{failed_export.as_posix()}"\n'
           f'terrain save "{raw_export.as_posix()}"\n'
           f'terrain save "{float_export.as_posix()}"\nterrain save "{terragen_export.as_posix()}"\n'
           f'terrain save "{bitmap_export.as_posix()}"\nterrain save "{png_export.as_posix()}"\n'
           f'terrain fill 255\nterrain save "{over_limit.as_posix()}"\n'
           'terrain rescale 33 33\nterrain rescale 10 60\n"quit"\n',
     capture_output=True,text=True,timeout=20)
   check(island.returncode==0,'Default island server shuts down cleanly: '+island.stderr)
   check('Unable to save terrain to ' in island.stdout and not failed_export.exists(),
         'Failed terrain export reports the error and leaves the console running')
   check('Terrain exceeds the export format' in island.stdout and not over_limit.exists(),
         'BMP export rejects a peak of 255 under the source MaxHeight convention')
   check('Terrain height at <128, 128> is 60' in island.stdout,'Rescale maps the island peak to the maximum')
   raw=raw_export.read_bytes()
   floats=float_export.read_bytes()
   terragen=terragen_export.read_bytes()
   bitmap=bitmap_export.read_bytes()
   png=png_export.read_bytes()
   check(len(raw)==256*256*13 and raw[0]*raw[1]==1280,'Console exports LLRAW channels')
   check(len(floats)==256*256*4 and struct.unpack_from('<f',floats,4*(128*256+128))[0]==60.,
         'Console exports unrounded little-endian RAW32 heights')
   check(len(terragen)==68+256*256*2 and terragen[56:60]==b'ALTW' and
         struct.unpack_from('<hhh',terragen,60)==(50,35,-32768),
         'Console exports Terragen scale, base, and signed sample')
   check(len(bitmap)==54+256*256*4 and bitmap[:2]==b'BM' and
         bitmap[54:58]==bytes((20,20,20,255)) and
         bitmap[54+4*(128*256+128):58+4*(128*256+128)]==bytes((120,120,120,255)),
         'Console exports bottom-up grayscale BMP terrain')
   check(png[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>IIBB',png,16)==(256,256,8,6),
         'Console exports the same RGBA PNG layout as System.Drawing')
   chunks={}
   offset=8
   while offset<len(png):
    size,tag=struct.unpack_from('>I4s',png,offset)
    chunks[tag]=png[offset+8:offset+8+size]
    offset+=12+size
   check(chunks[b'sRGB']==b'\0' and chunks[b'gAMA']==struct.pack('>I',45455) and
         chunks[b'pHYs']==struct.pack('>IIB',3779,3779,1),'PNG retains System.Drawing metadata')
   pixels=zlib.decompress(chunks[b'IDAT'])
   center=127*1025+513
   check(len(pixels)==256*1025 and pixels[1:5]==bytes((20,20,20,255)) and
         pixels[center:center+4]==bytes((120,120,120,255)),
         'PNG scanlines preserve terrain orientation and opaque grayscale values')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    for table in ('terrain','bakedterrain'):
     blob=gzip.decompress(database.execute(f'SELECT Heightfield FROM {table}').fetchone()[0])
     heights=tuple(struct.unpack_from('<f',blob,8+4*(128*256+x))[0] for x in (0,128,178,228))
     expected=(33.,)*4 if table=='terrain' else (0.,25.,7.5,0.)
     check(heights==expected,f'Default island and console rescale produce the expected heights in {table}')
   oversized=root/'oversized.r32'
   oversized.write_bytes(bytes(512*512*4))
   missing=root/'missing.r32'
   uppercase=root/'uppercase.R32'
   uppercase.write_bytes(bytes(256*256*4))
   imported=subprocess.run(cmd+['--console'],
                            input=f'terrain load "{float_export.as_posix()}"\nterrain load "{oversized.as_posix()}"\n'
                                  f'terrain load "{missing.as_posix()}"\nterrain load "{uppercase.as_posix()}"\n'
                                  'terrain show 128,128\n"quit"\n',
                            capture_output=True,text=True,timeout=20)
   check(imported.returncode==0 and 'Loaded terrain from ' in imported.stdout,
         'Console loads the exported RAW32 heightmap: '+imported.stderr)
   check('Terrain file dimensions do not match the region' in imported.stdout and
         'Cannot open terrain file' in imported.stdout and
         'Unsupported terrain file format' in imported.stdout and
         'Terrain height at <128, 128> is 60' in imported.stdout,
         'Rejected terrain files report errors and leave the console and active map intact')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database:
    active=gzip.decompress(database.execute('SELECT Heightfield FROM terrain').fetchone()[0])
    baked=gzip.decompress(database.execute('SELECT Heightfield FROM bakedterrain').fetchone()[0])
    heights=tuple(struct.unpack_from('<f',active,8+4*(128*256+x))[0] for x in (0,128,178,228))
    check(active==baked and heights==(10.,60.,25.,10.),'Console import replaces and bakes the terrain')
   blocked_import=root/'blocked.r32'
   blocked_import.write_bytes(bytes(256*256*4))
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute("CREATE TRIGGER reject_bake BEFORE INSERT ON bakedterrain BEGIN SELECT RAISE(ABORT,'fixture rejects bake'); END")
   blocked=subprocess.run(cmd+['--console'],
     input=f'terrain load "{blocked_import.as_posix()}"\nterrain show 128,128\n"quit"\n',
     capture_output=True,text=True,timeout=20)
   check(blocked.returncode==0 and 'fixture rejects bake' in blocked.stdout and
         'Terrain height at <128, 128> is 60' in blocked.stdout,
         'Failed baked write leaves the active terrain unchanged')
   with contextlib.closing(sqlite3.connect(root/'terrain.db')) as database, database:
    database.execute('DROP TRIGGER reject_bake')
    for table in ('terrain','bakedterrain'):
     blob=database.execute(f'SELECT Heightfield FROM {table}').fetchone()[0]
     check(gzip.decompress(blob)==active,f'Failed import preserves the stored {table} heightmap')
   config.write_text(flat_config,encoding='utf-8')
  if a.advanced_profiles:
   with contextlib.closing(sqlite3.connect(root/'offline.db')) as database, database:
    database.execute("INSERT INTO fixture_offline (PrincipalID, Message, StoredAt) SELECT PrincipalID, Message, StoredAt - 15 * 86400 FROM fixture_offline WHERE PrincipalID = ?", (recipient,))
   settings=config.read_text(encoding='utf-8').splitlines(keepends=True)
   settings=[line for line in settings if not line.startswith(('OfflineMessageURL=','ConnectionString=Data Source=offline.db'))]
   settings=[line for line in settings if not line.startswith('PrivateMailbox=')]
   settings=''.join(settings)+'\n[Startup]\nServiceConnectors=Ignored.dll:Other, MailboxAuth@0/OpenSim.Addons.OfflineIM.dll:OfflineIMServiceRobustConnector\n'
   config.write_text(settings+'\n[DatabaseService]\nConnectionString=Data Source=offline.db\n',encoding='utf-8')
   restart=subprocess.Popen(cmd+['--console'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8')
   startup=queue.Queue()
   def read_restart():
    for line in restart.stdout:startup.put(line)
    startup.put(None)
   threading.Thread(target=read_restart,daemon=True).start()
   try:
    deadline=time.monotonic()+20
    while True:
     line=startup.get(timeout=max(.01,deadline-time.monotonic()))
     if line is None:raise RuntimeError('Restart failed before HTTP startup')
     if line.startswith('LISTENING '):
      url=line.split(' ',1)[1].strip()
      offline_url=url.rstrip('/')+'/offlineim'
      break
    persisted=offline_form(METHOD='GET',PrincipalID=recipient)
    check(persisted.findtext('RESULT/im-0/Message')==message['Message'],'Offline message survives server restart')
    check(len(persisted.find('RESULT'))==1,'Offline startup cleanup removes the expired message')
    restarted_login=xmlrpc.client.ServerProxy(url, context=primary_context).login_to_simulator(credentials)
    check(restarted_login.get('login')=='true','Original viewer can log in after restart: '+str(restarted_login.get('message','')))
    session=xmlrpc.client.ServerProxy(url, context=primary_context).login_to_simulator({**credentials,'first':'Offline'})
    check(session.get('login')=='true','Viewer logs into restarted region')
    stored=offline_form(**{**message,'ToAgentID':session['agent_id'],'FromAgentID':'93939393-9393-4393-8393-939393939393','BinaryBucket':'00 01 FF','Dialog':'4','Offline':'0'})
    check(stored.findtext('RESULT')=='True','Mailbox queues a message for the connected viewer')
    result=subprocess.run([str(Path(a.viewer).resolve()),session['agent_id'],session['session_id'],
                           str(session['circuit_code']),session['sim_ip'],str(session['sim_port']),
                           '','','','','','--local-offline'],capture_output=True,text=True,timeout=20)
    check(result.returncode==0,'Viewer receives local offline message over UDP: '+result.stderr)
    check(len(offline_form(METHOD='GET',PrincipalID=session['agent_id']).find('RESULT'))==0,
          'Local viewer retrieval drains the durable mailbox')
   finally:
    if restart.poll() is None:
     restart.stdin.write('"quit"\n');restart.stdin.flush()
     try:restart.wait(timeout=15)
     except subprocess.TimeoutExpired:restart.kill();restart.wait();raise
    check(restart.returncode==0,'Restarted region shuts down cleanly')
 report=dict(passed=True,basicProfileDisabled=a.profiles_disabled,checks=checks,scope='Native executable HTTP login/seed plus UDP handshake/root/terrain/avatar/wearables, primitive create/edit, actual default LSL state_entry and optional touch_start, optional real ubODE walk/turn/run/jump/fly/hover and animation UUIDs, optional shipped SQLite assets/capabilities/ranges/concurrent requests, actual region-loaded default XML cache retrieval after deleting its SQL copy before the first HTTP request, actual SQLite FetchInventory2 item retrieval/source serialization/statuses, before-root legacy item/folder/library fetches, real task Contents/Xfer cache and mutation, script assets, GetScriptRunning EventQueue and two-stage script save/compile/error/stopped/recovery plus viewer start/stop/reset and stopped-reset continuation with exact stored invalid-UTF8/trailing-NUL bytes and real ObjectProperties serials, logout/relogin. Actual viewer rendering, complete physics/collision engines, complete objects/permissions, full YEngine language/runtime, asset/inventory transfer and macOS execution remain pending.')
 if a.advanced_profiles:
  report['advancedProfiles']=True
  report['scope']+=' OfflineIM covers authenticated remote GET and STORE, local SQLite mailbox limits and rollback on corrupt rows, restart persistence and expiration, and viewer UDP delivery of stored inventory offers. The same account can reconnect after clean shutdown with a pending circuit.'
  report['scope']+=' Configured advanced profiles cover console queue counts, system avatar properties, session validation, malformed generic requests, SQLite picks and notes, preference updates and the original SQLite preference read failure, and background avatar property replies.'
 (a.work/'connected-region-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(f'PASS: {len(checks)} connected native region network checks')
if __name__=='__main__':main()
