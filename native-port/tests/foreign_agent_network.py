"""Exercise Gatekeeper admission through the first viewer handshake."""
import base64
import gzip
import http.client
import json
import os
import queue
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client
from contextlib import closing
from pathlib import Path
from socketserver import ThreadingMixIn
from urllib.parse import urlsplit
from xmlrpc.server import SimpleXMLRPCRequestHandler, SimpleXMLRPCServer

from sql_offline_im_network import running_server

calls = queue.Queue()
kicks = queue.Queue()
home_logouts = queue.Queue()
access_queries = queue.Queue()
hypergrid_queries = queue.Queue()
hypergrid_replies = queue.Queue()
resolve_hypergrid = threading.Event()
allow_hypergrid_access = threading.Event()
home_launches = queue.Queue()
accept_home_launch = threading.Event()
outbound_updates = queue.Queue()
remote_texture_id = uuid.UUID("809a479a-0ad8-4701-86df-469033210d4f")
remote_texture = b"Hypergrid UDP texture fixture"
discard_texture_id = uuid.UUID("0ce112f1-f249-431c-aa53-06a076593fa9")
discard_texture = bytes(range(1, 251)) * 12
layered_texture_id = uuid.UUID("1dee5fb4-39a3-485e-b93e-fcb6950574c1")
layered_texture = (Path(__file__).resolve().parents[2] /
                   "bin/assets/TexturesAssetSet/default_avatar.jp2").read_bytes()
slow_texture_id = uuid.UUID("0a3765d1-054e-4abe-b570-16196ca7bdc4")
ready_texture_id = uuid.UUID("2b86c410-5981-4fa4-b42c-bb155f5bac69")
slow_texture_started = threading.Event()
release_slow_texture = threading.Event()
ready_texture_served = threading.Event()

class HomeRequestHandler(SimpleXMLRPCRequestHandler):
 protocol_version = "HTTP/1.1"

 def do_GET(self):
  textures = {f"/assets/{remote_texture_id}": (remote_texture_id, remote_texture),
              f"/assets/{discard_texture_id}": (discard_texture_id, discard_texture),
              f"/assets/{layered_texture_id}": (layered_texture_id, layered_texture),
              f"/assets/{slow_texture_id}": (slow_texture_id, remote_texture),
              f"/assets/{ready_texture_id}": (ready_texture_id, remote_texture)}
  if self.path not in textures:
   self.send_error(404)
   return
  texture_id, data = textures[self.path]
  if texture_id == slow_texture_id:
   slow_texture_started.set()
   release_slow_texture.wait(8)
  asset = (f"<AssetBase><FullID><Guid>{texture_id}</Guid></FullID>"
           f"<Type>0</Type><Data>{base64.b64encode(data).decode()}</Data></AssetBase>").encode()
  self.send_response(200)
  self.send_header("Content-Type", "text/xml")
  self.send_header("Content-Length", str(len(asset)))
  self.end_headers()
  self.wfile.write(asset)
  if texture_id == ready_texture_id:
   ready_texture_served.set()

 def read_body(self):
  if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
   body = bytearray()
   while size := int(self.rfile.readline().split(b";", 1)[0], 16):
    body.extend(self.rfile.read(size))
    self.rfile.read(2)
   self.rfile.readline()
  else:
   body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
  return body

 def send_json(self, reply):
  self.send_response(200)
  self.send_header("Content-Type", "application/json")
  self.send_header("Content-Length", str(len(reply)))
  self.end_headers()
  self.wfile.write(reply)

 def do_QUERYACCESS(self):
  access_queries.put((self.path, self.read_body()))
  allowed = allow_hypergrid_access.is_set() and f"/{remote_id}/" in self.path
  self.send_json(b'{"success":true,"negotiated_inbound_version":0.8,"negotiated_outbound_version":0.8}'
                 if allowed else b'{"success":false,"reason":"Fixture denied teleport"}')

 def do_POST(self):
  if self.path == "/hgfriends":
   body = self.read_body()
   fields = urllib.parse.parse_qs(body.decode())
   managed = getattr(self.server, "managed_friends", None)
   if managed and fields.get("PrincipalID") == [managed[0]]:
    endpoint = urlsplit(managed[1])
    with closing(http.client.HTTPConnection(endpoint.hostname, endpoint.port, timeout=5)) as connection:
     connection.request("POST", endpoint.path.rstrip("/") + "/hgfriends", body,
                        {"Content-Type": "application/x-www-form-urlencoded"})
     response = connection.getresponse()
     reply = response.read()
     managed[2].put((fields, response.status, reply))
     self.send_response(response.status)
     self.send_header("Content-Type", response.getheader("Content-Type", "text/xml"))
     self.send_header("Content-Length", str(len(reply)))
     self.end_headers()
     self.wfile.write(reply)
    return
   if fields.get("METHOD") == ["statusnotification"]:
    friend = fields["friend_0"][0].split(";")[0]
    self.send_json((f"<ServerResponse><friend_0>{unrelated_online_friend}</friend_0>"
                    f"<friend_1>{friend}</friend_1></ServerResponse>").encode())
   else:
    getattr(self.server, "friend_requests", home_friend_requests).put(fields)
    self.send_json(b"<ServerResponse><Result>Success</Result></ServerResponse>")
   return
  if not self.path.startswith(("/homeagent/", "/foreignagent/")):
   return super().do_POST()
  body = self.read_body()
  if body.startswith(b"\x1f\x8b"):
   body = gzip.decompress(body)
  home_launches.put((self.path, json.loads(body)))
  self.send_json(b'{"success":true}' if accept_home_launch.is_set()
                 else b'{"success":false,"reason":"Fixture denied home-agent launch"}')

 def do_PUT(self):
  body = self.read_body()
  if body.startswith(b"\x1f\x8b"):
   body = gzip.decompress(body)
  outbound_updates.put((self.path, json.loads(body)))
  self.send_json(b"true")

class HomeServer(ThreadingMixIn, SimpleXMLRPCServer):
 daemon_threads = True

home_friend_requests = queue.Queue()
unrelated_online_friend = uuid.uuid4()

deny_client = "--deny-client" in sys.argv[2:]
telehub_denial = "--telehub-denial" in sys.argv[2:]


def viewer_map_url(connection, seed_path):
    def llsd_map(node):
        children = list(node)
        return dict(zip((key.text for key in children[::2]), children[1::2]))

    connection.request(
        "POST", seed_path, "<llsd><array><string>SimulatorFeatures</string></array></llsd>",
        {"Content-Type": "application/llsd+xml"},
    )
    response = connection.getresponse()
    assert response.status == 200, response.status
    caps = llsd_map(ET.fromstring(response.read()).find("map"))
    connection.request("GET", urlsplit(caps["SimulatorFeatures"].text).path)
    response = connection.getresponse()
    assert response.status == 200, response.status
    features = llsd_map(ET.fromstring(response.read()).find("map"))
    return llsd_map(features["OpenSimExtras"])["map-server-url"].text


def seed_restricted_telehub(root, owner, blocked):
 region = "11111111-1111-4111-8111-111111111111"
 hub = str(uuid.uuid4())
 with closing(sqlite3.connect(root / "terrain.db")) as database, database:
  database.execute(
      "INSERT INTO prims (UUID, RegionUUID, SceneGroupID, Name, Description, OwnerID, "
      "GroupPositionX, GroupPositionY, GroupPositionZ, RotationX, RotationY, RotationZ, RotationW, "
      "CreationDate, ObjectFlags, BaseMask, OwnerMask, GroupMask, EveryoneMask, NextOwnerMask, ClickAction) "
      "VALUES (?, ?, ?, 'Telehub', '', ?, 128, 128, 25, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0)",
      (hub, region, hub, str(owner)))
  settings = {
      name: 0 for _, name, _, required, default, _ in database.execute("PRAGMA table_info(regionsettings)")
      if required and default is None
  }
  settings.update(regionUUID=region, TelehubObject=hub, agent_limit=40, object_bonus=1,
                  water_height=20, disable_physics=1)
  columns = ",".join(settings)
  placeholders = ",".join("?" for _ in settings)
  database.execute(f"INSERT INTO regionsettings ({columns}) VALUES ({placeholders})", tuple(settings.values()))
  database.execute("INSERT INTO spawn_points (RegionID,Yaw,Pitch,Distance) VALUES (?,0,0,0)", (region,))
  database.execute(
      "INSERT INTO land (UUID,RegionUUID,LocalLandID,OwnerUUID,Bitmap,LandFlags,Name,Desc,GroupUUID,"
      "ClaimDate,LandingType,UserLocationX,UserLocationY,UserLocationZ,UserLookAtX,UserLookAtY,UserLookAtZ) "
      "VALUES (?,?,1,?,?,1024,'Telehub parcel','',?,0,2,0,0,0,0,0,0)",
      (region, region, str(owner), bytes([255]) * 512, str(uuid.UUID(int=0))))
  database.execute("INSERT INTO landaccesslist (LandUUID,AccessUUID,Flags) VALUES (?,?,2)",
                   (region, str(blocked)))
 with closing(sqlite3.connect(root / "estate.db")) as database, database:
  database.execute(
      "CREATE TABLE estate_settings (EstateID INTEGER PRIMARY KEY, EstateOwner TEXT, "
      "BillableFactor REAL, ParentEstateID INT, PricePerMeter INT, RedirectGridX INT, RedirectGridY INT, "
      "AllowDirectTeleport INT, PublicAccess INT, TaxFree INT)")
  database.execute("INSERT INTO estate_settings VALUES (100,?,1,1,0,0,0,0,1,0)", (str(owner),))
  database.execute("CREATE TABLE estate_map (RegionID TEXT, EstateID INT)")
  database.execute("INSERT INTO estate_map VALUES (?,100)", (region,))


with HomeServer(("127.0.0.1",0),requestHandler=HomeRequestHandler,logRequests=False) as home:
 home.register_function(lambda fields:{"result":"True"},"verify_agent")
 def verify(fields):
  calls.put(fields)
  return {"result":"False" if deny_client else "True"}
 home.register_function(verify,"verify_client")
 def kick(fields):
  kicks.put(fields)
  return {"success":"TRUE"}
 home.register_function(kick,"grid_instant_message")
 def logout(fields):
  home_logouts.put(fields)
  return {"result":"true"}
 home.register_function(logout, "logout_agent")
 remote_id = str(uuid.uuid4())
 malformed_link_id = str(uuid.uuid4())
 remote_calls = []
 def link_region(fields):
  remote_calls.append(("link", fields))
  if fields["region_name"] == "Malformed Link":
   return {"result": "True", "uuid": malformed_link_id, "handle": "10995116282880", "region_image": 1}
  return {"result":" True ", "uuid":remote_id, "handle":" +10995116282880 ", "size_x":"256", "size_y":" +512 ",
          "external_name":f"http://127.0.0.1:{home.server_address[1]}/ Remote Region"}
 home.register_function(link_region, "link_region")
 def get_region(fields):
  hypergrid_queries.put(fields)
  try:
   return hypergrid_replies.get_nowait()
  except queue.Empty:
   pass
  if resolve_hypergrid.is_set():
   return {"result":" True ", "uuid":remote_id, "x":"76800", "y":"76800",
           "size_x":"256", "size_y":"512", "region_name":"Resolved Remote",
           "hostname":"127.0.0.1", "internal_port":"9000",
           "http_port":str(home.server_address[1]), "server_uri":home_uri}
  return {"result":"false", "message":"Fixture denied Hypergrid destination"}
 home.register_function(get_region, "get_region")
 threading.Thread(target=home.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory() as d, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as viewer:
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
   probe.bind(("127.0.0.1",0))
   udp=probe.getsockname()[1]
  home_uri=f"http://127.0.0.1:{home.server_address[1]}/"
  config=Path(d)/"foreign.ini"
  walk_crossing = "--walk-crossing" in sys.argv[2:]
  fly_crossing = walk_crossing and "--fly" in sys.argv[2:]
  blocked_flight_destination = fly_crossing and "--no-flight-destination" in sys.argv[2:]
  fall_crossing = walk_crossing and "--fall" in sys.argv[2:]
  config.write_text(f"[NativeServer]\nEnableLocalRegion=true\nEnablePresenceEndpoint=true\nEnableGridUserEndpoint=true\nEnableGridEndpoint=true\nGridRegionProvider=SQLite\nGridRegionDatabase=regions.db\nEnableGatekeeperDiscovery=true\nEnableForeignAgentAdmission=true\nUserAccountDatabase=users.db\nAuthenticationDatabase=users.db\nInventoryDatabase=inventory.db\nGridUserDatabase=grid.db\n[Startup]\nGatekeeperURI=http://127.0.0.1:8002/\n[Network]\nhttp_listener_port=0\n[NativeRegion]\nUDPPort={udp}\nBindAddress=127.0.0.1\nExternalHost=127.0.0.1\nName=Foreign Test\n[GatekeeperService]\nForeignAgentsAllowed=false\nDisallowExcept={home_uri}\nDeniedMacs=abc123\nDeniedID0s=def456\nAllowedClients=^Fixture\nDeniedClients=Blocked$\n[Messaging]\nMessageKey=fixture-grid-key\n[GridService]\nAllowHypergridMapSearch=true\n")
  settings = config.read_text()
  check_map_features = not sys.argv[2:]
  if check_map_features:
   settings += (
       "\n[Map]\nWorldMapModule=WorldMap\n"
       "[HGWorldMap]\nMapTileURL=https://hypergrid.example/map\n"
       "[SimulatorFeatures]\nMapTileURL=https://visitors.example/map\n"
   )
  config.write_text(settings.replace("[NativeServer]\n",
      "[NativeServer]\nEnableSimulationEndpoint=true\nEnableFriendsEndpoint=true\nFriendsDatabase=friends.db\n"))
  if check_map_features:
   config.write_text(config.read_text().replace("[Startup]\n", "[Startup]\nWorldMapModule=HGWorldMap\n", 1))
  if walk_crossing:
   library = sys.argv[sys.argv.index("--walk-crossing") + 1]
   config.write_text(config.read_text().replace("[NativeRegion]\n",
       f"[NativeRegion]\nPhysics=ubODE\nPhysicsLibrary={library}\n", 1))
  if not deny_client:
   config.write_text(config.read_text() + "\n[PresenceService]\nConnectionString=presence.db\n")
  command=[str(Path(sys.argv[1]).resolve()),"--config",str(config)]
  subprocess.run(command+["--init-db"],check=True)
  created = subprocess.run(command+["--create-user","Access","Fixture"], input="fixture-password\n",
                           capture_output=True, text=True, check=True)
  local_agent = uuid.UUID(created.stdout.rsplit("Created user ", 1)[1].strip())
  created = subprocess.run(command+["--create-user","Offer","Recipient"], input="fixture-password\n",
                           capture_output=True, text=True, check=True)
  offer_recipient = uuid.UUID(created.stdout.rsplit("Created user ", 1)[1].strip())
  with closing(sqlite3.connect(Path(d) / "users.db")) as database:
   database.execute("UPDATE UserAccounts SET ServiceURLs = ?, UserFlags = 4, UserTitle = 'Fixture Title' WHERE PrincipalID = ?",
                    ("InventoryServerURI=http%3A%2F%2Finventory.example%2F", str(local_agent)))
   database.commit()
  if deny_client:
   settings = config.read_text()
   config.write_text(settings.replace("[GatekeeperService]\n", "[GatekeeperService]\nBansService=Fixture.Bans.dll:BansService\n"))
   startup = subprocess.run(command, capture_output=True, text=True, timeout=10)
   assert startup.returncode != 0 and "cannot load the configured BansService" in startup.stderr
   config.write_text(settings)
  managed_offer_lookup = "--managed-offer-lookup" in sys.argv[2:]
  travel_settings = "[NativeServer]\nTravelDatabase=travel.db\n"
  if not managed_offer_lookup:
   travel_settings += "EnableHypergridUuidOfferLookup=true\n"
  config.write_text(config.read_text().replace("[NativeServer]\n", travel_settings, 1))
  with config.open("a") as settings_file:
   settings_file.write("\n[LoginService]\nSRV_FriendsServerURI=http://127.0.0.1:8002/\n")
   if check_map_features:
    settings_file.write("MapTileURL=https://local.example/map\n")
  if deny_client:
   with config.open("a") as settings_file:
    settings_file.write("\n[UserAgentService]\nLevelOutsideContacts=1\nVerifyCallers=true\nAuthorizedCallers=192.0.2.1\nShowUserDetailsInHGProfile=false\n")
  live_hypergrid = "--live-hypergrid" in sys.argv[2:]
  if not live_hypergrid and "--native-home-launch" not in sys.argv[2:]:
   config.write_text(config.read_text().replace("[Startup]\n", "[Startup]\nHomeURI=http://127.0.0.1:8002/\n", 1))
  if live_hypergrid:
   with closing(socket.socket()) as listener_probe:
    listener_probe.bind(("127.0.0.1", 0))
    source_port = listener_probe.getsockname()[1]
   source_home = f"http://127.0.0.1:{source_port}/"
   config.write_text(config.read_text().replace("http_listener_port=0", f"http_listener_port={source_port}")
                     .replace("GatekeeperURI=http://127.0.0.1:8002/", f"GatekeeperURI={source_home}\nHomeURI={source_home}"))
   if "--return-hypergrid" in sys.argv[2:]:
    config.write_text(config.read_text().replace("[NativeServer]\n", "[NativeServer]\nHttpWorkers=1\n", 1))
  if "--native-home-launch" in sys.argv[2:]:
   config.write_text(config.read_text().replace("[Startup]\n", "[Startup]\nHomeURI=http://127.0.0.1:8002/\n", 1))
   with config.open("a") as policy:
    policy.write("\n[UserAgentService]\nForeignTripsAllowed_Level_+00=false\n")
    if "--trusted-home-launch" not in sys.argv[2:]:
     policy.write("LoginServerIP=192.0.2.1\n")
    exception = home_uri.upper() if "--deny-native-trip" in sys.argv[2:] else home_uri
    policy.write(f"DisallowExcept_Level_00={exception}\n")
  if not sys.argv[2:]:
   settings = config.read_text().replace("[Startup]\n",
       "[Startup]\nGatekeeperURIAlias=http://127.0.0.1:18003/\n", 1)
   settings = settings.replace("DeniedMacs=abc123", "DeniedMacs=abc123,CAF\u00c9")
   settings = settings.replace("[NativeServer]\n", "[NativeServer]\nEnableRegionAssets=true\n", 1)
   config.write_text(settings.replace("[GatekeeperService]\n",
       "[GatekeeperService]\nGatekeeperURI=http://127.0.0.1:18001\n"
       "GatekeeperURIAlias= , http://127.0.0.1:18002/\n", 1), encoding="utf-8")
   with config.open("a") as settings_file:
    settings_file.write("\n[UserAgentService]\nGatekeeperURI=HTTP://127.0.0.1:8002/Fixture\n")
    settings_file.write("\n[Modules]\nAssetServices=RegionAssetConnector\n"
                        "\n[AssetService]\nLocalGridAssetService=OpenSim.Services.AssetService.dll:AssetService\n"
                        "HypergridAssetService=OpenSim.Services.Connectors.dll:HGAssetServiceConnector\n"
                        "StorageProvider=OpenSim.Data.SQLite.dll\n"
                        f"ConnectionString=URI=file:{Path(d) / 'assets.db'};version=3;Pooling=false\n")
  if telehub_denial:
   blocked_visitor = uuid.uuid4()
   config.write_text(config.read_text().replace(
       "[NativeRegion]\n", "[NativeRegion]\nTerrainDatabase=terrain.db\n"))
   with running_server(command):
    pass
   config.write_text(config.read_text().replace(
       "[NativeRegion]\n", "[NativeRegion]\nEstateDatabase=estate.db\n"))
   seed_restricted_telehub(Path(d), local_agent, blocked_visitor)
  with running_server(command) as post:
   port=urlsplit(next(cell.cell_contents for cell in post.__closure__ if isinstance(cell.cell_contents,str))).port
   travel_session = str(uuid.uuid4())
   travel_user = str(uuid.uuid4())
   foreign_friend = str(uuid.uuid4())
   foreign_uui = f"{foreign_friend};http://friend.example/;Foreign Friend"
   repeated_secret_friend = str(uuid.uuid4())
   malformed_friend = str(uuid.uuid4())
   missing_secret_friend = str(uuid.uuid4())
   with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
    database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                     (travel_user, foreign_uui + ";fixture-secret", 1))
    database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                     (foreign_uui + ";fixture-secret", travel_user, 2))
    database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)", (
        (travel_user, f"{repeated_secret_friend};http://friend.example/;fixture-secret Friend;fixture-secret", 1),
        (travel_user, f"{malformed_friend};http://friend.example/;   ;fixture-secret", 1),
        (travel_user, f"{missing_secret_friend};http://friend.example/;Foreign Friend", 1),
    ))
    database.commit()
   with closing(sqlite3.connect(Path(d) / "travel.db")) as database:
    database.execute(
        "INSERT INTO hg_traveling_data (SessionID, UserID, ServiceToken, ClientIPAddress, GridExternalName) VALUES (?, ?, ?, ?, ?)",
        (travel_session, travel_user, "fixture-travel-token", "192.0.2.17", "http://home.example/"))
    database.commit()
   with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as hg:
    pending = f"{uuid.uuid4()};http://pending.example/;Pending Friend;pending-secret"
    creation = {"METHOD":"newfriendship", "PrincipalID":travel_user, "Friend":pending,
                "SESSIONID":travel_session, "KEY":"fixture-travel-token"}
    for arguments, expected in (({**creation, "Friend":pending.split(";")[0] + ";;"}, "Failure"),
                                (creation, "Success"), (creation, "Failure")):
     form = urllib.parse.urlencode(arguments)
     request_body = ("  " + form + "  ").encode("utf-16") if expected == "Success" else form
     hg.request("POST", "/hgfriends", request_body,
                {"Content-Type":"application/x-www-form-urlencoded"})
     reply = hg.getresponse()
     body = ET.fromstring(reply.read())
     assert reply.status == 200 and (body.findtext("Result") or body.findtext("RESULT")) == expected
    with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
     assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                             (travel_user, pending)).fetchone() == ("0",)
    for offerer, expected in ((pending.split(";")[0], "False" if managed_offer_lookup else "True"), (foreign_friend, "False"),
                              ("invalid-uuid", "False")):
     validation = {"METHOD":"validate_friendship_offered", "PrincipalID":offerer, "Friend":travel_user}
     hg.request("POST", "/hgfriends", urllib.parse.urlencode(validation),
                {"Content-Type":"application/x-www-form-urlencoded"})
     reply = hg.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == expected
    if managed_offer_lookup:
     sys.exit(0)
    approval = {"METHOD":"newfriendship", "PrincipalID":pending.split(";")[0], "Friend":travel_user}
    for expected in ("Success", "Failure"):
     hg.request("POST", "/hgfriends", urllib.parse.urlencode(approval),
                {"Content-Type":"application/x-www-form-urlencoded"})
     reply = hg.getresponse()
     body = ET.fromstring(reply.read())
     assert reply.status == 200 and (body.findtext("Result") or body.findtext("RESULT")) == expected
    with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
     pending_id = approval["PrincipalID"]
     rows = database.execute("SELECT PrincipalID, Friend, Flags FROM Friends "
                             "WHERE PrincipalID = ? OR Friend IN (?, ?)",
                             (pending_id, pending_id, pending)).fetchall()
     assert set(rows) == {(pending_id, travel_user, "1"), (travel_user, pending_id, "1")}
    fields = {"METHOD":"getfriendperms", "PRINCIPALID":travel_user, "FRIENDID":foreign_friend,
              "SESSIONID":travel_session}
    deletion = {**fields, "METHOD":"deletefriendship", "PrincipalID":travel_user, "Friend":foreign_uui}
    for arguments in (deletion, {**deletion, "SECRET":"fixture-secret"}):
     hg.request("POST", "/hgfriends", urllib.parse.urlencode(arguments),
                {"Content-Type":"application/x-www-form-urlencoded"})
     reply = hg.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "False"
    for key, expected in (("wrong-token", "Failure"), ("fixture-travel-token", "Success")):
     hg.request("POST", "/hgfriends", urllib.parse.urlencode({**fields, "KEY":key}),
                {"Content-Type":"application/x-www-form-urlencoded"})
     reply = hg.getresponse()
     body = ET.fromstring(reply.read())
     assert reply.status == 200 and body.findtext("RESULT") == expected
     if expected == "Success":
      assert body.findtext("Value") == "2"
   with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as home_agent:
    assert home_agent.get_online_friends({"userID":travel_user, "friend_0":foreign_uui}) == {}
    visible_id = uuid.UUID(int=0) if deny_client else local_agent
    assert home_agent.get_uuid({"first":"Access", "last":"Fixture"}) == {"UUID":str(visible_id)}
    assert home_agent.get_uuid({"first":"Missing", "last":"Fixture"}) == {"UUID":str(uuid.UUID(int=0))}
    assert home_agent.get_uuid({"first":"Access"}) == {}
    assert home_agent.get_server_urls({"userID":str(local_agent)}) == {"SRV_InventoryServerURI":"http://inventory.example/"}
    assert home_agent.get_server_urls({"userID":str(uuid.uuid4())}) == {"result":"No Service URLs"}
    profile = home_agent.get_user_info({"userID":str(local_agent)})
    assert profile["result"] == "success" and profile["user_firstname"] == "Access" and profile["user_lastname"] == "Fixture", profile
    assert profile["user_flags"] == (0 if deny_client else 4), profile
    assert profile["user_title"] == ("" if deny_client else "Fixture Title"), profile
    assert (profile["user_created"] == 0) == deny_client, profile
    assert home_agent.get_user_info({"userID":str(uuid.uuid4())}) == {"result":"failure"}
    uui_grid = source_home if live_hypergrid else "http://127.0.0.1:8002/"
    home_agent_grid = "http://127.0.0.1:8002/fixture/" if not sys.argv[2:] else uui_grid
    assert home_agent.get_uui({"userID":travel_user, "targetUserID":str(local_agent)}) == {
        "UUI":f"{local_agent};{home_agent_grid};Access Fixture"}
    assert home_agent.get_uui({"userID":travel_user, "targetUserID":str(uuid.uuid4())}) == {"result":"User unknown"}
    assert home_agent.get_uui({"userID":travel_user, "targetUserID":foreign_friend}) == {"UUI":foreign_uui + ";0"}
    assert home_agent.get_uui({"userID": travel_user, "targetUserID": repeated_secret_friend}) == {
        "UUI": f"{repeated_secret_friend};http://friend.example/;0 Friend;0",
    }
    assert home_agent.get_uui({"userID": travel_user, "targetUserID": malformed_friend}) == {
        "result": "User unknown",
    }
    try:
     home_agent.get_uui({"userID": travel_user, "targetUserID": missing_secret_friend})
    except xmlrpc.client.Fault as error:
     assert error.faultCode == -32603, error
    else:
     raise AssertionError("A friendship without a secret should fault during identity lookup")
    expected_location = {} if deny_client else {"URL":"http://home.example/"}
    assert home_agent.locate_user({"userID":travel_user}) == expected_location
    if not deny_client:
     assert home_agent.locate_user({"userID":str(uuid.uuid4())}) == {"result":"Unable to locate user"}
     for destination, expected in ((home_agent_grid, {"result":"Unable to locate user"}),
                                   (home_agent_grid.upper(), {"URL":home_agent_grid.upper()})):
      with closing(sqlite3.connect(Path(d) / "travel.db")) as database:
       database.execute("UPDATE hg_traveling_data SET GridExternalName = ? WHERE SessionID = ?",
                        (destination, travel_session))
       database.commit()
      assert home_agent.locate_user({"userID":travel_user}) == expected
     with closing(sqlite3.connect(Path(d) / "travel.db")) as database:
      database.execute("UPDATE hg_traveling_data SET GridExternalName = ? WHERE SessionID = ?",
                       ("http://home.example/", travel_session))
      database.commit()
    for method, token, expected in (
        ("verify_agent", "fixture-travel-token", "True"),
        ("verify_agent", "wrong-token", "False"),
        ("verify_client", "192.0.2.17", "True"),
        ("verify_client", "127.0.0.1", "True"),
        ("verify_client", "192.0.2.18", "False")):
     verify = getattr(home_agent, method)
     assert verify({"sessionID":travel_session, "token":token}) == {"result":expected}
     assert verify({"sessionID":str(uuid.uuid4()), "token":token}) == {"result":"False"}
    for grid, expected in (("HTTP://HOME.EXAMPLE/", "True"), ("http://other.example/", "False")):
     assert home_agent.agent_is_coming_home({"sessionID":travel_session, "externalName":grid}) == {"result":expected}
    credentials = {"sessionID":travel_session, "token":"fixture-travel-token"}
    for method, fields in (
        ("verify_agent", []),
        ("verify_agent", {**credentials, "sessionID": 17}),
        ("verify_agent", {**credentials, "token": True}),
        ("verify_client", {**credentials, "token": ["127.0.0.1"]}),
        ("agent_is_coming_home", {"sessionID": travel_session, "externalName": 1}),
        ("logout_agent", {"sessionID": travel_session, "userID": 1}),
    ):
     try:
      getattr(home_agent, method)(fields)
     except xmlrpc.client.Fault as error:
      assert error.faultCode == -32603, error
     else:
      raise AssertionError(f"{method} accepted a value with an invalid XML-RPC type")
    assert home_agent.verify_agent({"sessionID": travel_session}) == {"result": "False"}
    assert home_agent.logout_agent({"sessionID":travel_session, "userID":str(uuid.uuid4())}) == {"result":"False"}
    assert home_agent.verify_agent(credentials) == {"result":"True"}
    assert home_agent.logout_agent({"sessionID":travel_session, "userID":travel_user}) == {"result":"true"}
    assert home_agent.verify_agent(credentials) == {"result":"False"}
   with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as c:
    query = {"position":"<120,121,45>", "viaTeleport":True,
             "simulation_service_supported_min":0.3, "simulation_service_supported_max":0.8,
             "simulation_service_accepted_min":0.3, "simulation_service_accepted_max":0.8}
    foreign_agent = uuid.uuid4()
    c.request("QUERYACCESS", f"/agent/{foreign_agent}/11111111-1111-4111-8111-111111111111/",
              json.dumps({**query, "agent_home_uri": home_uri}), {"Content-Type":"application/json"})
    response = c.getresponse()
    body = response.read()
    assert response.status == 200 and body.startswith(b"{"), (response.status, body)
    result = json.loads(body)
    assert result["success"] is True and result["reason"] == "", result
    c.request("QUERYACCESS", f"/agent/{local_agent}/11111111-1111-4111-8111-111111111111/",
              json.dumps(query), {"Content-Type":"application/json"})
    response = c.getresponse()
    result = json.loads(response.read())
    assert response.status == 200 and result["success"] is True and result["reason"] == "", result
    unrecognized = {"agent_id":str(local_agent), "session_id":str(uuid.uuid4()),
                    "secure_session_id":str(uuid.uuid4()), "circuit_code":"9471",
                    "destination_uuid":"11111111-1111-4111-8111-111111111111"}
    c.request("POST", f"/agent/{local_agent}/", json.dumps(unrecognized),
              {"Content-Type":"application/json"})
    response = c.getresponse()
    result = json.loads(response.read())
    assert response.status == 200 and result.get("success", False) is False and result.get("reason") == "Invalid agent session", result
    c.request("POST", f"/agent/{foreign_agent}/",
              json.dumps({**unrecognized, "agent_id":str(foreign_agent), "serviceurls":{"HomeURI":home_uri}}),
              {"Content-Type":"application/json"})
    response = c.getresponse()
    result = json.loads(response.read())
    assert response.status == 200 and not result.get("success", False) and result.get("reason") == "Invalid agent session", result
    with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as login_proxy:
     login = login_proxy.login_to_simulator({"first":"Access", "last":"Fixture", "passwd":"fixture-password",
                                             "start":"home", "channel":"Fixture", "version":"1.0"})
    assert login.get("login") == "true", login
    home_region_id = "11111111-1111-4111-8111-111111111111"
    with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
     advertised = proxy.get_server_urls({"userID":str(local_agent)})
     assert advertised["SRV_FriendsServerURI"] == "http://127.0.0.1:8002/", advertised
    home_fields = {"METHOD":"sethome", "UserID":str(local_agent), "RegionID":home_region_id,
                   "Position":"<120, 121, 45>", "LookAt":"<0, 1, 0>"}
    c.request("POST", "/griduser", urllib.parse.urlencode(home_fields),
              {"Content-Type":"application/x-www-form-urlencoded"})
    reply = c.getresponse()
    assert reply.status == 200 and b"Success" in reply.read()
    with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
     assert proxy.get_home_region({"userID":str(uuid.uuid4())}) == {"result":"false"}
     saved_home = proxy.get_home_region({"userID":str(local_agent)})
     assert saved_home["result"] == "true" and saved_home["uuid"] == home_region_id, saved_home
     assert saved_home["position"] == "<120, 121, 45>" and saved_home["lookAt"] == "<0, 1, 0>", saved_home
    mismatched = {**unrecognized, "session_id":login["session_id"]}
    c.request("POST", f"/agent/{local_agent}/", json.dumps(mismatched),
              {"Content-Type":"application/json"})
    response = c.getresponse()
    result = json.loads(response.read())
    assert response.status == 200 and result.get("success", False) is False and result.get("reason") == "Invalid agent session", result
    authenticated = {**mismatched, "secure_session_id":login["secure_session_id"],
                     "destination_uuid":str(uuid.uuid4())}
    c.request("POST", f"/agent/{local_agent}/", json.dumps(authenticated),
              {"Content-Type":"application/json"})
    response = c.getresponse()
    result = json.loads(response.read())
    assert response.status == 200 and result.get("reason") == "Region unavailable", result
    admitted = {**authenticated, "destination_uuid":unrecognized["destination_uuid"],
                "circuit_code":str(login["circuit_code"]), "first_name":"Access", "last_name":"Fixture",
                "caps_path":urlsplit(login["seed_capability"]).path.removeprefix("/CAPS/").removesuffix("0000/"),
                "start_pos":"<128,128,45>"}
    if not live_hypergrid:
     c.request("POST", f"/agent/{local_agent}/", json.dumps({**admitted, "teleport_flags":128}),
               {"Content-Type":"application/json"})
     response = c.getresponse()
     result = json.loads(response.read())
     assert response.status == 200 and result.get("success") is True, result
    c.request("POST", f"/agent/{local_agent}/", json.dumps(admitted),
              {"Content-Type":"application/json", "X-SecondLife-Shard":"fixture"})
    response = c.getresponse()
    assert (response.status, response.read()) == (406, b"false")
    if not deny_client:
     source_viewer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
     source_viewer.settimeout(5)
     source_endpoint = ("127.0.0.1", udp)
     pending_hg_offer = f"{uuid.uuid4()};{home_uri};Foreign Sender;abc12345"
     online_foreign = uuid.uuid4()
     online_identifier = f"{online_foreign};{home_uri};Online Sender;abcdef01"
     managed_friends = os.environ.get("OPENSIM_TEST_MANAGED_FRIENDS_URI")
     managed_requests = queue.Queue()
     if managed_friends:
      home.managed_friends = (pending_hg_offer[:36], managed_friends, managed_requests)
     home.register_function(lambda fields: {"SRV_FriendsServerURI":home_uri}, "get_server_urls")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 0)",
                           [(str(local_agent), friend) for friend in (str(offer_recipient), pending_hg_offer)])
      database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 1)",
                           [(str(local_agent), online_identifier), (online_identifier, str(local_agent))])
      database.commit()
     session_bytes = uuid.UUID(login["session_id"]).bytes
     circuit_bytes = struct.pack("<I", int(login["circuit_code"]) & 0xffffffff)
     source_viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                          circuit_bytes + session_bytes + local_agent.bytes, source_endpoint)
     source_viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                          local_agent.bytes + session_bytes + circuit_bytes, source_endpoint)
     for _ in range(64):
      if source_viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
       break
     else:
      raise AssertionError("Source viewer did not enter the region")
     if check_map_features:
      assert viewer_map_url(c, urlsplit(login["seed_capability"]).path) == "https://local.example/map/"
     if walk_crossing:
      destination_config = Path(d) / "destination.ini"
      destination_id = str(uuid.uuid4())
      with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as port_probe:
       port_probe.bind(("127.0.0.1", 0))
       destination_udp = port_probe.getsockname()[1]
      destination_settings = config.read_text(encoding="utf-8").replace(
          f"UDPPort={udp}", f"UDPPort={destination_udp}").replace(
          "Name=Foreign Test", f"Name=Transfer Test\nID={destination_id}\nLocationX=1001")
      if blocked_flight_destination:
       destination_settings = destination_settings.replace("[NativeRegion]\n",
           "[NativeRegion]\nTerrainDatabase=destination-terrain.db\n", 1)
       with closing(sqlite3.connect(Path(d) / "destination-terrain.db")) as database, database:
        database.execute("CREATE TABLE regionsettings (regionUUID TEXT, terrain_raise_limit REAL, "
                         "terrain_lower_limit REAL, block_terraform INTEGER, water_height REAL, block_fly INTEGER)")
        database.execute("INSERT INTO regionsettings VALUES (?,?,?,?,?,?)",
                         (destination_id, 100.0, -100.0, 0, 20.0, 1))
      destination_config.write_text(destination_settings, encoding="utf-8")
      c.request("POST", urlsplit(login["seed_capability"]).path,
                "<llsd><array><string>EventQueueGet</string></array></llsd>",
                {"Content-Type":"application/llsd+xml"})
      response = c.getresponse()
      event_queue_url = ET.fromstring(response.read()).findtext("map/string")
      assert response.status == 200 and event_queue_url
      handle = ((1001 * 256) << 32) | (1000 * 256)

      def movement_animation(packet):
       payload = packet[6 + packet[5]:]
       if len(payload) >= 38 and payload[:17] == b"\x14" + local_agent.bytes and payload[17]:
        return struct.unpack_from("<16si", payload, 18)
       return None

      with running_server([command[0], "--config", str(destination_config)]):
       source_handle = ((1000 * 256) << 32) | (1000 * 256)
       altitude = 70 if fall_crossing else 40 if fly_crossing else 25
       source_viewer.sendto(struct.pack(">BIB", 0x40, 19001, 0) + bytes.fromhex("ffff003f") +
                            local_agent.bytes + session_bytes +
                            struct.pack("<Q6f", source_handle, 255, 128, altitude, 1, 0, 0), source_endpoint)
       for _ in range(64):
        if source_viewer.recv(4096)[6:10] == bytes.fromhex("ffff0040"):
         break
       else:
        raise AssertionError("Viewer could not reach the crossing fixture's starting position")
       # The local teleport starts above ground. Let the physics body land
       # before walking so the source has a distinct movement animation.
       if fall_crossing:
        falling_animation = uuid.UUID("666307d9-a860-572d-6fd4-c3ab8865c094").bytes
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         source_fall = movement_animation(source_viewer.recv(4096))
         if source_fall is not None and source_fall[0] == falling_animation:
          break
        else:
         raise AssertionError("The source avatar did not start falling")
       elif not fly_crossing:
        time.sleep(0.75)
       movement_controls = 0x2001 if fly_crossing else 1
       source_viewer.sendto(struct.pack(">BIBB", 0x40, 19002, 0, 4) +
                            local_agent.bytes + session_bytes + bytes(24) + b"\0" +
                            struct.pack("<13fIB", 255,128,altitude, 1,0,0, 0,1,0, 0,0,1, 64, movement_controls, 0),
                            source_endpoint)
       deadline = time.monotonic() + 12
       crossed = None
       while time.monotonic() < deadline and crossed is None:
        c.request("POST", urlsplit(event_queue_url).path,
                  "<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>",
                  {"Content-Type":"application/llsd+xml"})
        response = c.getresponse()
        body = response.read()
        if response.status == 200:
         crossed = next((event for event in ET.fromstring(body).iter("map")
                         if event.findtext("string") == "CrossedRegion"), None)
       assert crossed is not None, "Viewer did not receive CrossedRegion after moving across the boundary"
       region_data = next((dict(zip((key.text for key in entry[::2]), entry[1::2]))
                           for entry in crossed.iter("map") if entry.findtext("key") == "RegionHandle"), None)
       assert region_data is not None, ET.tostring(crossed)
       endpoint = (socket.inet_ntoa(base64.b64decode(region_data["SimIP"].text)),
                   int(region_data["SimPort"].text))
       assert endpoint == ("127.0.0.1", destination_udp), endpoint
       seed_url = urlsplit(region_data["SeedCapability"].text)
       assert seed_url.hostname == endpoint[0] and seed_url.path.startswith("/CAPS/")

       with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as crossed_viewer:
        crossed_viewer.settimeout(5)
        crossed_viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                              circuit_bytes + session_bytes + local_agent.bytes, endpoint)
        crossed_viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                              local_agent.bytes + session_bytes + circuit_bytes, endpoint)
        destination_animation = None
        for _ in range(64):
         packet = crossed_viewer.recv(4096)
         if destination_animation is None:
          destination_animation = movement_animation(packet)
         if packet[6:10] == bytes.fromhex("ffff00fa"):
          x, y, z = struct.unpack_from("<3f", packet, 42)
          assert 0 <= x < 4 and abs(y - 128) < 1, (x, y)
          if fly_crossing:
           assert abs(z - altitude) < 2, z
          if fall_crossing:
           assert 25 < z < altitude - 2, z
          assert struct.unpack_from("<Q", packet, 66)[0] == handle
          break
        else:
         raise AssertionError("Crossing viewer did not enter the destination region")
        for _ in range(64):
         if destination_animation is not None:
          break
         destination_animation = movement_animation(crossed_viewer.recv(4096))
        assert destination_animation is not None, "The destination did not publish a movement animation"
        assert destination_animation[1] > 1, "The destination restarted the movement animation sequence"
        if fly_crossing:
         fly_animation = uuid.UUID("aec4610c-757f-bc4e-c092-c6e9caf18daf").bytes
         assert destination_animation[0] == fly_animation, "The transfer did not retain the source flight animation"
        if blocked_flight_destination:
         crossed_viewer.sendto(struct.pack(">BIBB", 0x40, 3, 0, 4) +
                               local_agent.bytes + session_bytes + bytes(25) +
                               struct.pack("<13fIB", x,y,z, 1,0,0, 0,1,0, 0,0,1, 64, 0x2010, 0), endpoint)
         deadline = time.monotonic() + 8
         ack_sequence = 200
         while time.monotonic() < deadline:
          packet = crossed_viewer.recv(4096)
          if packet[0] & 0x40:
           ack_sequence += 1
           sequence = struct.unpack_from(">I", packet, 1)[0]
           crossed_viewer.sendto(struct.pack(">BIB", 0, ack_sequence, 0) + bytes.fromhex("fffffffb") +
                                 struct.pack("<BI", 1, sequence), endpoint)
          payload = packet[6 + packet[5]:]
          if packet[0] & 0x80:
           decoded = bytearray()
           cursor = iter(payload)
           for byte in cursor:
            if byte:
             decoded.append(byte)
            else:
             decoded.extend(bytes(next(cursor, 0)))
           payload = decoded
          if payload[:1] == b"\x0c" and payload[17:33] == local_agent.bytes and len(payload) >= 93:
           # The avatar ObjectData block stores position and velocity consecutively.
           current_z, _, _, vertical_speed = struct.unpack_from("<4f", payload, 77)
           if current_z < z - 3 and vertical_speed < 0:
            break
         else:
          raise AssertionError("Avatar did not descend in the no-flight region")
        elif fly_crossing:
         # Observe several physics heartbeats without sending fresh movement input.
         crossed_viewer.settimeout(0.1)
         deadline = time.monotonic() + 0.55
         while time.monotonic() < deadline:
          try:
           animation = movement_animation(crossed_viewer.recv(4096))
          except socket.timeout:
           continue
          assert animation is None or animation == destination_animation, "Flight changed before fresh viewer input"
         crossed_viewer.settimeout(5)
        if fall_crossing:
         assert destination_animation == source_fall, "The falling animation restarted at the crossing"
         landing_animations = {uuid.UUID(name).bytes for name in (
             "7a17b059-12b2-41b1-570a-186368b6aa6f", "f4f00d6e-b9fe-9292-f4cb-0ae06ea58d57",
             "3da1d753-028a-5446-24f3-9c9b856d9422")}
         deadline = time.monotonic() + 10
         ack_sequence = 200
         while time.monotonic() < deadline:
          packet = crossed_viewer.recv(4096)
          if packet[0] & 0x40:
           ack_sequence += 1
           sequence = struct.unpack_from(">I", packet, 1)[0]
           crossed_viewer.sendto(struct.pack(">BIB", 0, ack_sequence, 0) + bytes.fromhex("fffffffb") +
                                 struct.pack("<BI", 1, sequence), endpoint)
          animation = movement_animation(packet)
          if animation is not None and animation[0] in landing_animations:
           break
          assert animation is None or animation == source_fall, "The avatar stopped falling before landing"
         else:
          raise AssertionError("The falling avatar did not land in the destination")
        else:
         for _ in range(128):
          if movement_animation(source_viewer.recv(4096)) == destination_animation:
           break
         else:
          raise AssertionError("The destination did not publish the source movement animation and sequence")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
          presence = database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?",
                                      (login["session_id"],)).fetchone()
         if presence == (destination_id,):
          break
         time.sleep(0.02)
        else:
         raise AssertionError(f"Crossing did not move the grid presence to the destination: {presence}")

        if fall_crossing or blocked_flight_destination:
         home.shutdown()
         print("PASS: avatar descends in the destination region")
         sys.exit(0)

        time.sleep(0.25)
        source_viewer.sendto(struct.pack(">BIBBB", 0, 19003, 0, 1, 127) + bytes(4), source_endpoint)
        for _ in range(64):
         if source_viewer.recv(4096)[6:8] == bytes((2, 127)):
          break
        else:
         raise AssertionError("Source region did not retain the viewer's child circuit")
        with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=15)) as neighbor:
         neighbor.request("POST", seed_url.path, "<llsd><array><string>EventQueueGet</string></array></llsd>",
                          {"Content-Type":"application/llsd+xml"})
         response = neighbor.getresponse()
         neighbor_queue = ET.fromstring(response.read()).findtext("map/string")
         assert response.status == 200 and neighbor_queue
         crossed_viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff003f") +
                               local_agent.bytes + session_bytes +
                               struct.pack("<Q6f", handle, 1, 128, 25, 1, 0, 0), endpoint)
         for _ in range(64):
          if crossed_viewer.recv(4096)[6:10] == bytes.fromhex("ffff0040"):
           break
         else:
          raise AssertionError("Viewer could not reach the return crossing's starting position")
         crossed_viewer.sendto(struct.pack(">BIBB", 0x40, 4, 0, 4) +
                               local_agent.bytes + session_bytes + bytes(24) + b"\0" +
                               struct.pack("<13fIB", 1,128,25, 1,0,0, 0,1,0, 0,0,1, 64, 2, 0), endpoint)
         deadline = time.monotonic() + 12
         returned = None
         while time.monotonic() < deadline and returned is None:
          neighbor.request("POST", urlsplit(neighbor_queue).path,
                           "<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>",
                           {"Content-Type":"application/llsd+xml"})
          response = neighbor.getresponse()
          body = response.read()
          if response.status == 200:
           returned = next((event for event in ET.fromstring(body).iter("map")
                            if event.findtext("string") == "CrossedRegion"), None)
         assert returned is not None, "Viewer did not receive the return CrossedRegion event"
         assert urlsplit(login["seed_capability"]).path in ET.tostring(returned, encoding="unicode")

        # The original UDP connection is already open and can complete the return directly.
        source_viewer.sendto(struct.pack(">BIB", 0x40, 19004, 0) + bytes.fromhex("ffff00f9") +
                             local_agent.bytes + session_bytes + circuit_bytes, source_endpoint)
        for _ in range(64):
         packet = source_viewer.recv(4096)
         if packet[6:10] == bytes.fromhex("ffff00fa") and struct.unpack_from("<Q", packet, 66)[0] == source_handle:
          x, y, _ = struct.unpack_from("<3f", packet, 42)
          if 252 < x < 256 and abs(y - 128) < 1:
           break
        else:
         raise AssertionError("Viewer did not return through the original child circuit")
        time.sleep(0.25)
        crossed_viewer.sendto(struct.pack(">BIB", 0x40, 5, 0) + bytes.fromhex("ffff00fc") +
                              local_agent.bytes + session_bytes, endpoint)
        deadline = time.monotonic() + 5
        with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=5)) as neighbor:
         while time.monotonic() < deadline:
          neighbor.request("POST", seed_url.path, "<llsd><array/></llsd>",
                           {"Content-Type":"application/llsd+xml"})
          response = neighbor.getresponse()
          response.read()
          if response.status == 404:
           break
          time.sleep(0.02)
         else:
          raise AssertionError("Closing the neighbor child circuit did not remove its seed capability")
        with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
         assert database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?",
                                 (login["session_id"],)).fetchone() == ("11111111-1111-4111-8111-111111111111",)
       home.shutdown()
       print("PASS: avatar crosses a region boundary through the viewer event queue")
       sys.exit(0)
     pending_messages = {b"Will you be my friend?": b"Offer Recipient",
                         b"Please confirm this friendship you made while you were on another HG grid":
                             f"Foreign.Sender @127.0.0.1:{home.server_address[1]}".encode()}
     received_foreign_status = False
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0142"):
       assert unrelated_online_friend.bytes not in packet
      if packet[6:10] == bytes.fromhex("ffff0142") and online_foreign.bytes in packet:
       received_foreign_status = True
      for message, name in list(pending_messages.items()):
       if message in packet:
        assert name in packet
        del pending_messages[message]
      if not pending_messages and received_foreign_status:
       break
     else:
      raise AssertionError("Viewer did not receive the persisted offer")
     hg_sender = uuid.UUID(pending_hg_offer.split(";")[0])
     if managed_friends:
      managed_db = Path(os.environ["OPENSIM_TEST_MANAGED_FRIENDS_DB"])
      assert managed_db.is_file()
      with closing(sqlite3.connect(managed_db)) as database:
       database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 0)",
                        (str(local_agent), str(hg_sender)))
       database.commit()
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 89, 0, 0xffff, 297) +
                          local_agent.bytes + session_bytes + hg_sender.bytes + b"\0", source_endpoint)
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       rows = database.execute("SELECT PrincipalID, Friend, Flags FROM Friends WHERE Friend = ? OR PrincipalID = ?",
                               (pending_hg_offer, pending_hg_offer)).fetchall()
       if set(rows) == {(str(local_agent), pending_hg_offer, "1"), (pending_hg_offer, str(local_agent), "1")}:
        break
       time.sleep(0.02)
      else:
       raise AssertionError(f"Hypergrid offer acceptance did not persist: {rows}")
      remote_identifier = f"{local_agent};{uui_grid};Access Fixture;abc12345"
      if managed_friends:
       fields, status, reply = managed_requests.get(timeout=5)
       assert fields == {"METHOD":["newfriendship"], "PrincipalID":[str(hg_sender)],
                         "Friend":[remote_identifier]}, fields
       assert status == 200 and b"<Result>Success</Result>" in reply, (status, reply)
       with closing(sqlite3.connect(managed_db)) as remote:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         rows = remote.execute("SELECT PrincipalID, Friend, Flags FROM Friends WHERE PrincipalID IN (?, ?, ?)",
                               (str(local_agent), str(hg_sender), remote_identifier)).fetchall()
         if set(rows) == {(str(hg_sender), remote_identifier, "1"), (remote_identifier, str(hg_sender), "1")}:
          break
         time.sleep(0.02)
        else:
         raise AssertionError(f"Managed Robust did not persist the native approval: {rows}")
      else:
       remote_accept = home_friend_requests.get(timeout=5)
       assert remote_accept["METHOD"] == ["newfriendship"]
       assert remote_accept["PrincipalID"] == [str(hg_sender)]
       assert remote_accept["Friend"] == [remote_identifier]
      source_viewer.sendto(struct.pack(">BIBHH", 0x40, 103, 0, 0xffff, 300) +
                           local_agent.bytes + session_bytes + hg_sender.bytes, source_endpoint)
      if managed_friends:
       # The managed handler rejects a supplied SECRET, even when it matches.
       fields, status, reply = managed_requests.get(timeout=5)
       assert fields == {"METHOD":["deletefriendship"], "PrincipalID":[str(hg_sender)],
                         "Friend":[str(local_agent)], "SECRET":["abc12345"]}, fields
       assert status == 200 and b"<RESULT>False</RESULT>" in reply, (status, reply)
       with closing(sqlite3.connect(managed_db)) as remote:
        rows = remote.execute("SELECT PrincipalID, Friend, Flags FROM Friends WHERE PrincipalID = ? OR Friend = ?",
                              (remote_identifier, remote_identifier)).fetchall()
        assert set(rows) == {(str(hg_sender), remote_identifier, "1"), (remote_identifier, str(hg_sender), "1")}, rows
      else:
       remote_termination = home_friend_requests.get(timeout=5)
       assert remote_termination == {"METHOD":["deletefriendship"], "PrincipalID":[str(hg_sender)],
                                     "Friend":[str(local_agent)], "SECRET":["abc12345"]}, remote_termination
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       if database.execute("SELECT 1 FROM Friends WHERE Friend = ? OR PrincipalID = ?",
                           (pending_hg_offer, pending_hg_offer)).fetchone() is None:
        break
       time.sleep(0.02)
      else:
       raise AssertionError("Native server retained the terminated friendship")
      declined_foreign = uuid.uuid4()
      malformed_foreign = uuid.uuid4()
      malformed_identifier = f"{malformed_foreign};{home_uri};Malformed Sender;"
      database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 1)",
                           [(str(local_agent), malformed_identifier), (malformed_identifier, str(local_agent))])
      database.commit()
      source_viewer.sendto(struct.pack(">BIBHH", 0x40, 105, 0, 0xffff, 300) +
                           local_agent.bytes + session_bytes + malformed_foreign.bytes, source_endpoint)
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff012c") and packet[42:58] == malformed_foreign.bytes:
        break
      else:
       raise AssertionError("Viewer did not receive malformed friendship termination")
      assert database.execute("SELECT 1 FROM Friends WHERE Friend = ? OR PrincipalID = ?",
                              (malformed_identifier, malformed_identifier)).fetchone() is None
      try:
       unexpected = home_friend_requests.get(timeout=0.5)
      except queue.Empty:
       pass
      else:
       raise AssertionError(f"Unexpected home-grid request for empty secret: {unexpected}")
      declined_identifier = f"{declined_foreign};{home_uri};Declined Sender;def67890"
      database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 0)",
                       (str(local_agent), declined_identifier))
      database.commit()
      source_viewer.sendto(struct.pack(">BIBHH", 0x40, 93, 0, 0xffff, 298) +
                           local_agent.bytes + session_bytes + declined_foreign.bytes, source_endpoint)
      remote_decline = home_friend_requests.get(timeout=5)
      assert remote_decline == {"METHOD":["deletefriendship"], "PrincipalID":[str(declined_foreign)],
                                "Friend":[str(local_agent)], "SECRET":["def67890"]}, remote_decline
      assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                              (str(local_agent), declined_identifier)).fetchone() is None
      database.execute("DELETE FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                       (str(local_agent), str(offer_recipient)))
      database.commit()
     offer_message = b"Viewer fixture offer\0"
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 106, 0, 0xffff, 254) +
                          local_agent.bytes + uuid.uuid4().bytes + b"\0" + offer_recipient.bytes +
                          bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                          struct.pack("<H", len(offer_message)) + offer_message + bytes(2), source_endpoint)
     time.sleep(0.1)
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                              (str(offer_recipient), str(local_agent))).fetchone() is None
     source_viewer.sendto(struct.pack(">BIB", 0x40, 90, 0) + bytes.fromhex("ffff00fe") +
                          local_agent.bytes + session_bytes + b"\0" + offer_recipient.bytes +
                          bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                          struct.pack("<H", len(offer_message)) + offer_message + bytes(2), source_endpoint)
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       pending = database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                                  (str(offer_recipient), str(local_agent))).fetchone()
       if pending == ("0",):
        break
       time.sleep(0.02)
      else:
       raise AssertionError("Viewer offer was not persisted")
     declined_sender = uuid.uuid4()
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 0)",
                           [(str(local_agent), str(sender)) for sender in (offer_recipient, declined_sender)])
      database.execute("DELETE FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                       (str(offer_recipient), str(local_agent)))
      database.commit()
      for sequence, packet_id, sender, tail in ((91, 297, offer_recipient, b"\0"),
                                                (92, 298, declined_sender, b"")):
       source_viewer.sendto(struct.pack(">BIBHH", 0x40, sequence, 0, 0xffff, packet_id) +
                            local_agent.bytes + session_bytes + sender.bytes + tail, source_endpoint)
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       rows = database.execute("SELECT PrincipalID, Friend, Flags FROM Friends WHERE Friend IN (?, ?, ?) "
                               "AND PrincipalID IN (?, ?)",
                               (str(local_agent), str(offer_recipient), str(declined_sender),
                                str(local_agent), str(offer_recipient))).fetchall()
       if set(rows) == {(str(local_agent), str(offer_recipient), "1"),
                        (str(offer_recipient), str(local_agent), "1")}:
        break
       time.sleep(0.02)
      else:
       raise AssertionError(f"Viewer acceptance or decline did not persist: {rows}")
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 107, 0, 0xffff, 254) +
                          local_agent.bytes + session_bytes + b"\0" + offer_recipient.bytes +
                          bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                          struct.pack("<H", len(offer_message)) + offer_message + bytes(2), source_endpoint)
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0087") and b"This person is already your friend." in packet:
       break
     else:
      raise AssertionError("Viewer did not receive the duplicate friendship alert")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      for principal, friend in ((local_agent, offer_recipient), (offer_recipient, local_agent)):
       assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                               (str(principal), str(friend))).fetchone() == ("1",)
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                       (str(local_agent), foreign_uui + ";fixture-status-secret", 1))
      database.commit()
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      notification = {"userID":foreign_friend, "online":"True",
                      "friend_0":f"{local_agent};{uui_grid};Access Fixture;fixture-status-secret"}
      bad_notification = {**notification, "friend_0":notification["friend_0"] + "-wrong"}
      assert proxy.status_notification(bad_notification) == {"result":"No Friends Online"}
      assert proxy.status_notification({**notification, "online": " True "}) == {
          "result": "false", "friend_0": str(local_agent),
      }
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff0142"):
        assert uuid.UUID(foreign_friend).bytes in packet
        break
      else:
       raise AssertionError("Viewer did not receive OnlineNotification")
      c.request("POST", "/hgfriends", urllib.parse.urlencode({
          **notification, "METHOD":"statusnotification", "online":"False"}),
          {"Content-Type":"application/x-www-form-urlencoded"})
      reply = c.getresponse()
      assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "NULL"
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff0143"):
        assert uuid.UUID(foreign_friend).bytes in packet
        break
      else:
       raise AssertionError("Viewer did not receive OfflineNotification")
      for fields, key, expected in (
          (bad_notification, "RESULT", "NULL"),
          ({**notification, "online":"invalid"}, "RESULT", "Failure"),
          (notification, "friend_0", str(local_agent)),
      ):
       c.request("POST", "/hgfriends", urllib.parse.urlencode({
           **fields, "METHOD":"statusnotification"}),
           {"Content-Type":"application/x-www-form-urlencoded"})
       reply = c.getresponse()
       assert reply.status == 200 and ET.fromstring(reply.read()).findtext(key) == expected
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff0142"):
        assert uuid.UUID(foreign_friend).bytes in packet
        break
      else:
       raise AssertionError("Hypergrid status endpoint did not notify the viewer")
     status_fields = {"METHOD":"status", "FromID":foreign_friend, "ToID":str(local_agent), "Online":"True"}
     c.request("POST", "/friends", urllib.parse.urlencode(status_fields),
               {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "True"
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0142"):
       assert uuid.UUID(foreign_friend).bytes in packet
       break
     else:
      raise AssertionError("Simulator status endpoint did not notify the viewer")
     approving_friend = uuid.uuid4()
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                       (str(approving_friend), str(local_agent), 0))
      database.commit()
     c.request("POST", "/hgfriends", urllib.parse.urlencode({
         "METHOD":"newfriendship", "PrincipalID":str(local_agent), "Friend":str(approving_friend)}),
         {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("Result") == "Success"
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if str(approving_friend).encode() in packet and (
          packet[6:10] == bytes.fromhex("ffff00fe") or packet[6:11] == bytes.fromhex("ffff0001fe")):
       payload = packet[6:]
       if packet[0] & 0x80:
        decoded = bytearray()
        index = 0
        while index < len(payload):
         byte = payload[index]
         index += 1
         if byte:
          decoded.append(byte)
         else:
          decoded.extend(bytes(payload[index]))
          index += 1
        payload = decoded
       assert payload[86] == 39
       assert payload[87:103] == bytes(a ^ b for a, b in zip(approving_friend.bytes, local_agent.bytes))
       assert abs(struct.unpack_from("<I", payload, 103)[0] - time.time()) < 30
       break
     else:
      raise AssertionError("Viewer did not receive friendship approval")
     offering_friend = uuid.uuid4()
     c.request("POST", "/hgfriends", urllib.parse.urlencode({
         "METHOD":"friendship_offered", "FromID":str(offering_friend), "ToID":str(uuid.uuid4())}),
         {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "False"
     home.register_function(lambda fields: {"SRV_FriendsServerURI":f"http://127.0.0.1:{port}/"},
                            "get_server_urls")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                       (str(local_agent), str(offering_friend), 0))
      database.commit()
     c.request("POST", "/hgfriends", urllib.parse.urlencode({
         "METHOD":"friendship_offered", "FromID":str(offering_friend), "ToID":str(local_agent),
         "FromName":f"Offering.Friend@127.0.0.1:{home.server_address[1]}",
         "Message":"Verified fixture offer"}), {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "True"
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if b"Verified fixture offer" in packet:
       break
     else:
      raise AssertionError("Viewer did not receive verified friendship offer")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      stored = database.execute("SELECT Friend, Flags FROM Friends WHERE PrincipalID = ? AND Friend LIKE ?",
                                (str(local_agent), str(offering_friend) + ";%")).fetchone()
      assert stored is not None and stored[1] == "0", stored
      sender, uri, display, secret = stored[0].split(";")
      assert (sender, uri, display) == (str(offering_friend), home_uri, "Offering Friend"), stored
      assert len(secret) == 8 and all(c in "0123456789abcdef" for c in secret), stored
     unverified_friend = uuid.uuid4()
     c.request("POST", "/hgfriends", urllib.parse.urlencode({
         "METHOD":"validate_friendship_offered", "PrincipalID":str(unverified_friend), "Friend":str(local_agent)}),
         {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "False"
     c.request("POST", urlsplit(login["seed_capability"]).path,
               "<llsd><array><string>EventQueueGet</string></array></llsd>",
               {"Content-Type":"application/llsd+xml"})
     reply = c.getresponse()
     seed = ET.fromstring(reply.read())
     event_queue_url = seed.findtext("./map/string")
     assert reply.status == 200 and event_queue_url
     c.request("POST", "/friends", urllib.parse.urlencode({
         "METHOD":"friendship_approved", "FromID":str(local_agent),
         "FromName":"Access Fixture", "ToID":str(local_agent)}),
         {"Content-Type":"application/x-www-form-urlencoded"})
     reply = c.getresponse()
     assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "True"
     with closing(sqlite3.connect(Path(d) / "inventory.db")) as database:
      cards = database.execute("SELECT inventoryName, assetType, invType, inventoryCurrentPermissions "
                               "FROM inventoryitems WHERE avatarID = ? AND creatorID = ?",
                               (str(local_agent), str(local_agent))).fetchall()
      assert ("Access Fixture", 2, 2, 0xc000) in cards, cards
      local_card = database.execute("SELECT inventoryName, assetType, invType FROM inventoryitems "
                                    "WHERE avatarID = ? AND creatorID = ?",
                                    (str(local_agent), str(offer_recipient))).fetchone()
      assert local_card == ("Offer Recipient", 2, 2), local_card
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.execute("CREATE TRIGGER reject_fixture_rights BEFORE INSERT ON Friends "
                       "WHEN NEW.Flags = '7' BEGIN SELECT RAISE(IGNORE); END")
      database.commit()
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 108, 0, 0xffff, 320) +
                          local_agent.bytes + session_bytes + b"\x01" + offer_recipient.bytes +
                          struct.pack("<i", 7), source_endpoint)
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0087") and b"Unable to grant rights." in packet:
       break
     else:
      raise AssertionError("Viewer did not receive the failed rights update alert")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                              (str(local_agent), str(offer_recipient))).fetchone() == ("1",)
      database.execute("DROP TRIGGER reject_fixture_rights")
      database.commit()
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 100, 0, 0xffff, 320) +
                          local_agent.bytes + session_bytes + b"\x01" + offer_recipient.bytes +
                          struct.pack("<i", 3), source_endpoint)
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0141"):
       assert packet[27:43] == offer_recipient.bytes and struct.unpack_from("<i", packet, 43)[0] == 3
       break
     else:
      raise AssertionError("Viewer did not receive the rights acknowledgement")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      for principal, friend, flags in ((local_agent, offer_recipient, "3"),
                                       (offer_recipient, local_agent, "1")):
       assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                               (str(principal), str(friend))).fetchone() == (flags,)
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      database.execute("CREATE TRIGGER reject_fixture_termination BEFORE DELETE ON Friends "
                       "BEGIN SELECT RAISE(IGNORE); END")
      database.commit()
      source_viewer.sendto(struct.pack(">BIBHH", 0x40, 109, 0, 0xffff, 300) +
                           local_agent.bytes + session_bytes + offer_recipient.bytes, source_endpoint)
      pending_packets = {bytes.fromhex("ffff0087"), bytes.fromhex("ffff012c")}
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if b"Unable to terminate friendship on this sim." in packet or (
           packet[6:10] == bytes.fromhex("ffff012c") and packet[42:58] == offer_recipient.bytes):
        pending_packets.discard(packet[6:10])
       if not pending_packets:
        break
      else:
       rows = database.execute("SELECT PrincipalID, Friend FROM Friends WHERE PrincipalID IN (?, ?)",
                               (str(local_agent), str(offer_recipient))).fetchall()
       raise AssertionError(f"Missing termination packets: {[p.hex() for p in pending_packets]}, rows={rows}")
      for principal, friend in ((local_agent, offer_recipient), (offer_recipient, local_agent)):
       assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                               (str(principal), str(friend))).fetchone() == (1,)
      database.execute("DROP TRIGGER reject_fixture_termination")
      database.commit()
     source_viewer.sendto(struct.pack(">BIBHH", 0x40, 98, 0, 0xffff, 300) +
                          local_agent.bytes + session_bytes + offer_recipient.bytes, source_endpoint)
     for _ in range(64):
      packet = source_viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff012c") and packet[42:58] == offer_recipient.bytes:
       break
     else:
      raise AssertionError("Viewer did not receive friendship termination")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       rows = database.execute("SELECT PrincipalID, Friend FROM Friends WHERE "
                               "(PrincipalID = ? AND Friend = ?) OR (PrincipalID = ? AND Friend = ?)",
                               (str(local_agent), str(offer_recipient),
                                str(offer_recipient), str(local_agent))).fetchall()
       if not rows:
        break
       time.sleep(0.02)
      else:
       raise AssertionError(f"Native server retained terminated friendship rows: {rows}")
     c.request("POST", urlsplit(event_queue_url).path,
               "<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>",
               {"Content-Type":"application/llsd+xml"})
     reply = c.getresponse()
     events = reply.read()
     assert reply.status == 200 and b"BulkUpdateInventory" in events and b"Access Fixture" in events, events
     destination_config = Path(d) / "destination.ini"
     destination_id = str(uuid.uuid4())
     with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as port_probe:
      port_probe.bind(("127.0.0.1", 0))
      destination_udp = port_probe.getsockname()[1]
     destination_settings = config.read_text(encoding="utf-8").replace(f"UDPPort={udp}", f"UDPPort={destination_udp}") \
         .replace("Name=Foreign Test", f"Name=Transfer Test\nID={destination_id}\nLocationX=1001")
     if live_hypergrid:
      with closing(socket.socket()) as listener_probe:
       listener_probe.bind(("127.0.0.1", 0))
       destination_port = listener_probe.getsockname()[1]
      destination_home = f"http://127.0.0.1:{destination_port}/"
      destination_settings = destination_settings.replace(source_home, destination_home) \
          .replace(f"http_listener_port={source_port}", f"http_listener_port={destination_port}") \
          .replace("ForeignAgentsAllowed=false", "ForeignAgentsAllowed=true") \
          .replace(f"DisallowExcept={home_uri}", "")
      for database in ("users", "regions", "grid", "presence", "inventory", "travel", "friends"):
       destination_settings = destination_settings.replace(f"={database}.db", f"=destination-{database}.db")
     destination_config.write_text(destination_settings, encoding="utf-8")
     if live_hypergrid:
      subprocess.run([command[0], "--config", str(destination_config), "--init-db"], check=True)
     with running_server([command[0], "--config", str(destination_config)]) as destination_post:
      destination_url = next(cell.cell_contents for cell in destination_post.__closure__ if isinstance(cell.cell_contents,str))
      handle = ((1001 * 256) << 32) | (1000 * 256)
      if live_hypergrid:
       search = urllib.parse.urlencode({"METHOD":"get_region_by_name", "NAME":destination_home + "Transfer Test"})
       c.request("POST", "/grid", search, {"Content-Type":"application/x-www-form-urlencoded"})
       result = c.getresponse()
       body = result.read()
       assert result.status == 200 and destination_id.encode() in body, body
       link = ET.fromstring(body).find("result")
       handle = (int(link.findtext("locX")) << 32) | int(link.findtext("locY"))
      if "--teleport-timeout" in sys.argv[2:]:
       source_viewer.sendto(struct.pack(">BIBB", 0x40, 110, 0, 4) +
                            local_agent.bytes + session_bytes + bytes(24) + b"\0" +
                            struct.pack("<13fIB", 128,128,45, 1,0,0, 0,1,0, 0,0,1, 64, 0x2000, 0),
                            source_endpoint)
      source_viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff003f") +
                           local_agent.bytes + session_bytes + struct.pack("<Q6f", handle, 128, 128, 45, 1, 0, 0),
                           source_endpoint)
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff004a"):
        raise AssertionError(f"Source rejected teleport: {packet[27:]!r}")
       if packet[6:10] == bytes.fromhex("ffff0045"):
        finish_sequence = packet[1:5]
        assert packet[30:34] == socket.inet_aton("127.0.0.1"), packet.hex()
        assert struct.unpack_from(">H", packet, 34)[0] == destination_udp
        expected_handle = ((1001 * 256) << 32) | (1000 * 256) if live_hypergrid else handle
        assert struct.unpack_from("<Q", packet, 36)[0] == expected_handle
        length = struct.unpack_from("<H", packet, 44)[0]
        seed_url = urlsplit(packet[46:46+length].rstrip(b"\0").decode())
        expected_url = urlsplit(destination_url)
        assert (seed_url.scheme, seed_url.hostname, seed_url.port) == (
            expected_url.scheme, expected_url.hostname, expected_url.port), seed_url
        assert seed_url.path.startswith("/CAPS/") and seed_url.path.endswith("0000/"), seed_url
        break
      else:
       raise AssertionError("Source did not send TeleportFinish")
      c.request("DELETE", f"/agent/{local_agent}/11111111-1111-4111-8111-111111111111/release/?auth={uuid.uuid4()}")
      release = c.getresponse()
      assert (release.status, release.read()) == (404, b"false")
      source_viewer.sendto(struct.pack(">BIB", 0x40, 4, 0) + bytes.fromhex("ffff003f") +
                           local_agent.bytes + session_bytes + struct.pack("<Q6f", handle, 128, 128, 45, 1, 0, 0),
                           source_endpoint)
      for _ in range(64):
       packet = source_viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff004a"):
        assert b"Previous teleport process incomplete" in packet, packet.hex()
        break
      else:
       raise AssertionError("Source did not reject a duplicate teleport")
      if "--teleport-timeout" in sys.argv[2:]:
       with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=5)) as destination:
        destination.request("DELETE", f"/agent/{local_agent}/{destination_id}/?auth={uuid.uuid4()}")
        response = destination.getresponse()
        response.read()
        assert response.status == 200
        queue_request = "<llsd><array><string>EventQueueGet</string></array></llsd>"
        destination.request("POST", seed_url.path, queue_request, {"Content-Type":"application/llsd+xml"})
        response = destination.getresponse()
        assert response.status == 200, "Wrong session token removed the pending destination seed"
        old_queue = ET.fromstring(response.read()).findtext("map/string")
        assert old_queue
       source_viewer.settimeout(35)
       deadline = time.monotonic() + 35
       while time.monotonic() < deadline:
        packet = source_viewer.recv(4096)
        if packet[6:10] == bytes.fromhex("ffff004a") and b"Destination did not confirm teleport arrival" in packet:
         break
       else:
        raise AssertionError("Source did not report teleport timeout")
       with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=5)) as destination:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         destination.request("POST", seed_url.path, "<llsd><array/></llsd>", {"Content-Type":"application/llsd+xml"})
         response = destination.getresponse()
         response.read()
         if response.status == 404:
          break
         time.sleep(0.02)
        else:
         raise AssertionError("Unused destination seed capability did not expire")
       source_viewer.settimeout(5)
       source_viewer.sendto(struct.pack(">BIB", 0x40, 5, 0) + bytes.fromhex("ffff003f") +
                            local_agent.bytes + session_bytes + struct.pack("<Q6f", handle, 128, 128, 45, 1, 0, 0),
                            source_endpoint)
       for _ in range(64):
        packet = source_viewer.recv(4096)
        if packet[6:10] == bytes.fromhex("ffff0045") and packet[1:5] != finish_sequence:
         length = struct.unpack_from("<H", packet, 44)[0]
         seed_url = urlsplit(packet[46:46+length].rstrip(b"\0").decode())
         break
       else:
        raise AssertionError("Source did not retry the timed-out teleport")
       with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=5)) as destination:
        destination.request("POST", seed_url.path, queue_request, {"Content-Type":"application/llsd+xml"})
        response = destination.getresponse()
        assert response.status == 200
        new_queue = ET.fromstring(response.read()).findtext("map/string")
        assert new_queue and new_queue != old_queue, "Retry reused the expired teleport's event queue"
      with closing(http.client.HTTPConnection("127.0.0.1", urlsplit(destination_url).port, timeout=5)) as destination:
       update = {"message_type":"AgentData", "destination_uuid":destination_id, "agent_uuid":str(local_agent),
                 "session_uuid":login["session_id"], "circuit_code":str(int(login["circuit_code"]) & 0xffffffff),
                 "position":"<125,126,47>", "at_axis":"<0,1,0>",
                 "god_data":{"ViewerUiIsGod":True}}
       for path, status in ((f"/agent/{uuid.uuid4()}/", 400),
                            (f"/agent/{local_agent}/{uuid.uuid4()}/", 404)):
        destination.request("PUT", path, json.dumps(update), {"Content-Type":"application/json"})
        reply = destination.getresponse()
        actual = (reply.status, reply.read())
        assert actual == (status, b"false"), (path, status, actual)
       release_url = f"http://127.0.0.1:{port}/agent/{local_agent}/11111111-1111-4111-8111-111111111111/release/"
       for fields, expected in (({**update, "session_uuid":str(uuid.uuid4())}, b"False"),
                                ({**update, "cb_uri":home_uri + "wrong/release/"}, b"False"),
                                ({**update, "cb_uri":release_url + "?auth=invalid"}, b"False"),
                                (update, b"True")):
        destination.request("PUT", f"/agent/{local_agent}/", json.dumps(fields), {"Content-Type":"application/json"})
        reply = destination.getresponse()
        actual = (reply.status, reply.read())
        assert actual == (200, expected), (fields, actual)
      with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as transfer_viewer:
       transfer_viewer.settimeout(5)
       session_bytes = uuid.UUID(login["session_id"]).bytes
       circuit_bytes = struct.pack("<I", int(login["circuit_code"]) & 0xffffffff)
       endpoint = ("127.0.0.1", destination_udp)
       transfer_viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                              circuit_bytes + session_bytes + local_agent.bytes, endpoint)
       transfer_viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                              local_agent.bytes + session_bytes + circuit_bytes, endpoint)
       entered = revoked = False
       for _ in range(64):
        movement = transfer_viewer.recv(4096)
        if movement[6:10] == bytes.fromhex("ffff00fa"):
         assert struct.unpack_from("<3f", movement, 42) == (125, 126, 47), movement.hex()
         assert struct.unpack_from("<3f", movement, 54) == (0, 1, 0), movement.hex()
         assert struct.unpack_from("<Q", movement, 66)[0] == ((1001 * 256) << 32) | (1000 * 256)
         entered = True
        if movement[6:10] == bytes.fromhex("ffff0102"):
         assert movement[10:42] == local_agent.bytes + session_bytes
         assert movement[42] == 0, "Transfer state granted rights the destination does not recognize"
         revoked = True
        if entered and revoked:
         break
       else:
        raise AssertionError("Transferred viewer did not enter with its unrecognized god mode revoked")
       if "--teleport-timeout" in sys.argv[2:]:
        destination_handle = ((1001 * 256) << 32) | (1000 * 256)
        transfer_viewer.sendto(struct.pack(">BIB", 0x40, 110, 0) + bytes.fromhex("ffff003f") +
                               local_agent.bytes + session_bytes +
                               struct.pack("<Q6f", destination_handle, 125,126,47, 0,1,0), endpoint)
        for _ in range(64):
         packet = transfer_viewer.recv(4096)
         if packet[6:10] == bytes.fromhex("ffff0040"):
          assert struct.unpack_from("<I", packet, 54)[0] == (16 | 8192), packet.hex()
          break
        else:
         raise AssertionError("Destination did not retain the incoming avatar's flight state")
       if not live_hypergrid:
        with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
         deadline = time.monotonic() + 5
         while time.monotonic() < deadline:
          row = database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?", (login["session_id"],)).fetchone()
          if row == (destination_id,):
           break
          time.sleep(0.02)
         else:
          raise AssertionError("Destination presence was not available for notification forwarding")
        with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
         assert proxy.status_notification(notification) == {"result": "false", "friend_0": str(local_agent)}
        for _ in range(64):
         packet = transfer_viewer.recv(4096)
         if packet[6:10] == bytes.fromhex("ffff0142"):
          assert uuid.UUID(foreign_friend).bytes in packet
          break
        else:
         raise AssertionError("Destination viewer did not receive the forwarded status")
        approving_friend = uuid.uuid4()
        with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
         database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                          (str(approving_friend), str(local_agent), 0))
         database.commit()
        c.request("POST", "/hgfriends", urllib.parse.urlencode({
            "METHOD":"newfriendship", "PrincipalID":str(local_agent), "Friend":str(approving_friend)}),
            {"Content-Type":"application/x-www-form-urlencoded"})
        reply = c.getresponse()
        assert reply.status == 200 and ET.fromstring(reply.read()).findtext("Result") == "Success"
        for _ in range(64):
         packet = transfer_viewer.recv(4096)
         if packet[6:10] == bytes.fromhex("ffff00fe") or packet[6:11] == bytes.fromhex("ffff0001fe"):
          assert str(approving_friend).encode() in packet
          break
        else:
         raise AssertionError("Destination viewer did not receive the forwarded approval")
        offering_friend = uuid.uuid4()
        with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
         database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, ?)",
                          (str(local_agent), str(offering_friend), 0))
         database.commit()
        c.request("POST", "/hgfriends", urllib.parse.urlencode({
            "METHOD":"friendship_offered", "FromID":str(offering_friend), "ToID":str(local_agent),
            "FromName":f"Offering.Friend@127.0.0.1:{home.server_address[1]}",
            "Message":"Forwarded fixture offer"}), {"Content-Type":"application/x-www-form-urlencoded"})
        reply = c.getresponse()
        assert reply.status == 200 and ET.fromstring(reply.read()).findtext("RESULT") == "True"
        for _ in range(64):
         packet = transfer_viewer.recv(4096)
         if b"Forwarded fixture offer" in packet:
          break
        else:
         raise AssertionError("Destination viewer did not receive the forwarded offer")
        with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
         second = proxy.login_to_simulator({"first":"Offer", "last":"Recipient",
                                           "passwd":"fixture-password", "start":"last"})
        assert second["login"] == "true", second
        with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
         database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 1)",
                              [(str(local_agent), str(offer_recipient)), (str(offer_recipient), str(local_agent))])
         database.commit()
        with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as second_viewer:
         second_viewer.settimeout(5)
         second_session = uuid.UUID(second["session_id"]).bytes
         second_circuit = struct.pack("<I", int(second["circuit_code"]) & 0xffffffff)
         second_viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                              second_circuit + second_session + offer_recipient.bytes, source_endpoint)
         second_viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                              offer_recipient.bytes + second_session + second_circuit, source_endpoint)
         for _ in range(64):
          if second_viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
           break
         else:
          raise AssertionError("Second viewer did not enter the source simulator")
         for _ in range(64):
          packet = second_viewer.recv(4096)
          if packet[6:10] == bytes.fromhex("ffff0142"):
           assert local_agent.bytes in packet
           break
         else:
          raise AssertionError("Second viewer did not receive the initial online friends list")
         for _ in range(64):
          packet = transfer_viewer.recv(4096)
          if packet[6:10] == bytes.fromhex("ffff0142") and offer_recipient.bytes in packet:
           break
         else:
          raise AssertionError("Destination viewer did not receive the friend's login notification")
         with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
          database.execute("DELETE FROM Friends WHERE (PrincipalID = ? AND Friend = ?) "
                           "OR (PrincipalID = ? AND Friend = ?)",
                           (str(local_agent), str(offer_recipient), str(offer_recipient), str(local_agent)))
          database.commit()
         message = b"Viewer offer across simulators\0"
         second_viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff00fe") +
                              offer_recipient.bytes + second_session + b"\0" + local_agent.bytes +
                              bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                              struct.pack("<H", len(message)) + message + bytes(2), source_endpoint)
         for _ in range(64):
          if message.rstrip(b"\0") in transfer_viewer.recv(4096):
           break
         else:
          raise AssertionError("Destination viewer did not receive the viewer-originated offer")
         transfer_viewer.sendto(struct.pack(">BIBHH", 0x40, 96, 0, 0xffff, 297) +
                                local_agent.bytes + session_bytes + offer_recipient.bytes + b"\0", endpoint)
         for _ in range(64):
          packet = second_viewer.recv(4096)
          if str(local_agent).encode() in packet and (
              packet[6:10] == bytes.fromhex("ffff00fe") or packet[6:11] == bytes.fromhex("ffff0001fe")):
           break
         else:
          raise AssertionError("Source viewer did not receive the forwarded acceptance")
         with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
          for principal, friend in ((local_agent, offer_recipient), (offer_recipient, local_agent)):
           assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                                   (str(principal), str(friend))).fetchone() == ("1",)
         transfer_viewer.sendto(struct.pack(">BIBHH", 0x40, 101, 0, 0xffff, 320) +
                                local_agent.bytes + session_bytes + b"\x01" + offer_recipient.bytes +
                                struct.pack("<i", 2), endpoint)
         received_rights = received_offline = False
         for _ in range(64):
          packet = second_viewer.recv(4096)
          if packet[6:10] == bytes.fromhex("ffff0141"):
           assert packet[10:26] == local_agent.bytes and packet[27:43] == offer_recipient.bytes
           assert struct.unpack_from("<i", packet, 43)[0] == 2
           received_rights = True
          if packet[6:10] == bytes.fromhex("ffff0143") and local_agent.bytes in packet:
           received_offline = True
          if received_rights and received_offline:
           break
         else:
          raise AssertionError("Source viewer did not receive forwarded rights and visibility changes")
         transfer_viewer.sendto(struct.pack(">BIBHH", 0x40, 99, 0, 0xffff, 300) +
                                local_agent.bytes + session_bytes + offer_recipient.bytes, endpoint)
         for _ in range(64):
          packet = second_viewer.recv(4096)
          if packet[6:10] == bytes.fromhex("ffff012c"):
           assert packet[42:58] == local_agent.bytes
           break
         else:
          raise AssertionError("Source viewer did not receive the forwarded termination")
         with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
          for principal, friend in ((local_agent, offer_recipient), (offer_recipient, local_agent)):
           assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                                   (str(principal), str(friend))).fetchone() is None
          database.execute("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 0)",
                           (str(local_agent), str(offer_recipient)))
          database.commit()
         transfer_viewer.sendto(struct.pack(">BIBHH", 0x40, 97, 0, 0xffff, 298) +
                                local_agent.bytes + session_bytes + offer_recipient.bytes, endpoint)
         for _ in range(64):
          packet = second_viewer.recv(4096)
          if str(local_agent).encode() in packet and (
              packet[6:10] == bytes.fromhex("ffff00fe") or packet[6:11] == bytes.fromhex("ffff0001fe")):
           break
         else:
          raise AssertionError("Source viewer did not receive the forwarded decline")
         with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
          assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                                  (str(local_agent), str(offer_recipient))).fetchone() is None
          database.executemany("INSERT INTO Friends (PrincipalID, Friend, Flags) VALUES (?, ?, 1)",
                               [(str(local_agent), str(offer_recipient)), (str(offer_recipient), str(local_agent))])
          database.commit()
         second_viewer.sendto(struct.pack(">BIBHH", 0x40, 102, 0, 0xffff, 252) +
                              offer_recipient.bytes + second_session, source_endpoint)
         for _ in range(64):
          packet = transfer_viewer.recv(4096)
          if packet[6:10] == bytes.fromhex("ffff0143") and offer_recipient.bytes in packet:
           break
         else:
          raise AssertionError("Destination viewer did not receive the friend's logout notification")
       if live_hypergrid and "--return-hypergrid" in sys.argv[2:]:
        seed_path = urlsplit(login["seed_capability"]).path
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         c.request("POST", seed_path, "<llsd><array/></llsd>", {"Content-Type":"application/llsd+xml"})
         seed = c.getresponse()
         seed.read()
         if seed.status == 404:
          break
         time.sleep(0.02)
        else:
         raise AssertionError("Source did not release the outbound viewer")
        with closing(http.client.HTTPConnection("127.0.0.1", destination_port, timeout=5)) as remote_grid:
         search = urllib.parse.urlencode({"METHOD":"get_region_by_name", "NAME":source_home + "Foreign Test"})
         remote_grid.request("POST", "/grid", search, {"Content-Type":"application/x-www-form-urlencoded"})
         response = remote_grid.getresponse()
         link = ET.fromstring(response.read()).find("result")
         assert response.status == 200 and link.findtext("uuid") == home_region_id
        return_handle = (int(link.findtext("locX")) << 32) | int(link.findtext("locY"))
        transfer_viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff003f") +
                               local_agent.bytes + session_bytes + struct.pack("<Q6f", return_handle, 120, 121, 45, 1, 0, 0),
                               endpoint)
        for _ in range(64):
         packet = transfer_viewer.recv(4096)
         if packet[6:10] == bytes.fromhex("ffff004a"):
          raise AssertionError(f"Destination rejected return teleport: {packet[27:]!r}")
         if packet[6:10] == bytes.fromhex("ffff0045"):
          assert struct.unpack_from(">H", packet, 34)[0] == udp
          seed_length = struct.unpack_from("<H", packet, 44)[0]
          return_seed = urlsplit(packet[46:46 + seed_length].rstrip(b"\0").decode()).path
          break
        else:
         raise AssertionError("Viewer did not receive return TeleportFinish")
        with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as return_viewer:
         return_viewer.settimeout(5)
         return_viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                              circuit_bytes + session_bytes + local_agent.bytes, source_endpoint)
         return_viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                              local_agent.bytes + session_bytes + circuit_bytes, source_endpoint)
         for _ in range(64):
          movement = return_viewer.recv(4096)
          if movement[6:10] == bytes.fromhex("ffff00fa"):
           assert struct.unpack_from("<3f", movement, 42) == (120, 121, 45), movement.hex()
           break
         else:
          raise AssertionError("Viewer did not reenter its home region")
         c.request("POST", return_seed, "<llsd><array/></llsd>", {"Content-Type":"application/llsd+xml"})
         seed = c.getresponse()
         assert seed.status == 200
         seed.read()
        with closing(http.client.HTTPConnection("127.0.0.1", destination_port, timeout=5)) as remote_grid:
         deadline = time.monotonic() + 5
         while time.monotonic() < deadline:
          remote_grid.request("POST", seed_url.path, "<llsd><array/></llsd>",
                              {"Content-Type":"application/llsd+xml"})
          seed = remote_grid.getresponse()
          seed.read()
          if seed.status == 404:
           break
          time.sleep(0.02)
         else:
          raise AssertionError("Visited grid did not release the returning viewer")
      destination_presence = "destination-presence.db" if live_hypergrid else "presence.db"
      returned_home = "--return-hypergrid" in sys.argv[2:]
      expected_presence = None if returned_home else (destination_id,)
      with closing(sqlite3.connect(Path(d) / destination_presence)) as database:
       deadline = time.monotonic() + 5
       while time.monotonic() < deadline:
        row = database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?", (login["session_id"],)).fetchone()
        if row == expected_presence:
         break
        time.sleep(0.02)
       else:
        raise AssertionError(f"Destination presence was {row}, expected {expected_presence}")
       deadline = time.monotonic() + 5
       while time.monotonic() < deadline:
        c.request("POST", urlsplit(login["seed_capability"]).path, "<llsd><array/></llsd>",
                  {"Content-Type":"application/llsd+xml"})
        seed = c.getresponse()
        seed.read()
        if seed.status == 404:
         break
        time.sleep(0.02)
       else:
        raise AssertionError("Source seed capability survived destination arrival")
       source_viewer.close()
       assert database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?",
                               (login["session_id"],)).fetchone() == expected_presence
      if "--teleport-timeout" in sys.argv[2:]:
       with closing(http.client.HTTPConnection(seed_url.hostname, seed_url.port, timeout=5)) as destination:
        for token, expected in ((str(uuid.uuid4()), 200), (login["session_id"], 404)):
         destination.request("DELETE", f"/agent/{local_agent}/{destination_id}/?auth={token}")
         response = destination.getresponse()
         assert response.status == 200
         response.read()
         destination.request("POST", seed_url.path, "<llsd><array/></llsd>", {"Content-Type":"application/llsd+xml"})
         response = destination.getresponse()
         assert response.status == expected, (response.status, expected, response.read())
         response.read()
      if live_hypergrid:
       expected_home_presence = (home_region_id,) if returned_home else None
       with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
         row = database.execute("SELECT RegionID FROM Presence WHERE SessionID = ?",
                                (login["session_id"],)).fetchone()
         if row == expected_home_presence:
          break
         time.sleep(0.02)
        else:
         raise AssertionError(f"Home grid presence was {row}, expected {expected_home_presence}")
       home.shutdown()
       sys.exit(0)
   def grid_user_status(connection, user):
    data = urllib.parse.urlencode({"METHOD": "getgriduserinfo", "UserID": user})
    connection.request("POST", "/griduser", data, {"Content-Type": "application/x-www-form-urlencoded"})
    response = connection.getresponse()
    assert response.status == 200
    return response.read()
   with closing(http.client.HTTPConnection("127.0.0.1",port,timeout=8)) as c:
    remote_search = urllib.parse.urlencode({"METHOD":"get_region_by_name", "NAME":home_uri + "Remote Region"})
    c.request("POST", "/grid", remote_search, {"Content-Type":"application/x-www-form-urlencoded"})
    remote = c.getresponse()
    remote_body = remote.read()
    assert remote.status == 200 and remote_id.encode() in remote_body and b"<sizeY>512</sizeY>" in remote_body, remote_body
    assert b"<sizeX>256</sizeX>" in remote_body, remote_body
    assert b"00000000-0000-1111-9999-000000000013" in remote_body, remote_body
    remote_region = ET.fromstring(remote_body).find("result")
    local_x = int(remote_region.findtext("locX"))
    assert 0 <= local_x <= 32766 * 256 and local_x % 256 == 0
    assert remote_region.findtext("locY") == "0"
    assert remote_calls == [("link", {"region_name":"Remote Region"})]
    malformed_search = urllib.parse.urlencode({"METHOD": "get_region_by_name", "NAME": home_uri + "Malformed Link"})
    c.request("POST", "/grid", malformed_search, {"Content-Type": "application/x-www-form-urlencoded"})
    malformed = c.getresponse()
    assert malformed.status == 200 and ET.fromstring(malformed.read()).findtext("result") == "null"
    assert remote_calls[-1] == ("link", {"region_name": "Malformed Link"})
    c.request("POST", "/grid", f"METHOD=get_region_by_uuid&REGIONID={malformed_link_id}",
              {"Content-Type": "application/x-www-form-urlencoded"})
    unregistered = c.getresponse()
    assert unregistered.status == 200 and ET.fromstring(unregistered.read()).findtext("result") == "null"
    if "--native-home-launch" in sys.argv[2:]:
     created = subprocess.run(command + ["--create-user", "Travel", "Fixture"], input="fixture-password\n",
                              capture_output=True, text=True, check=True)
     local_agent = uuid.UUID(created.stdout.rsplit("Created user ", 1)[1].strip())
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      local_login = proxy.login_to_simulator({"first":"Travel", "last":"Fixture", "passwd":"fixture-password",
                                             "start":"home", "channel":"Fixture", "version":"1.0"})
     assert local_login.get("login") == "true", local_login
     local_session = uuid.UUID(local_login["session_id"])
     code_bytes = struct.pack("<I", int(local_login["circuit_code"]) & 0xffffffff)
     endpoint = ("127.0.0.1", udp)
     viewer.settimeout(8)
     viewer.sendto(struct.pack(">BIB", 0x40, 1, 0) + bytes.fromhex("ffff0003") +
                   code_bytes + local_session.bytes + local_agent.bytes, endpoint)
     viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                   local_agent.bytes + local_session.bytes + code_bytes, endpoint)
     for _ in range(64):
      if viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
       break
     else:
      raise AssertionError("Local viewer did not enter the source")
     resolve_hypergrid.set()
     allow_hypergrid_access.set()
     viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff003f") +
                   local_agent.bytes + local_session.bytes + struct.pack("<Q6f", local_x << 32, 120, 121, 45, 1, 0, 0),
                   endpoint)
     if "--deny-native-trip" in sys.argv[2:]:
      for _ in range(64):
       packet = viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff004a"):
        assert b"Your world does not allow you to visit the destination" in packet, packet.hex()
        break
      else:
       raise AssertionError("Viewer did not receive the outbound policy denial")
      assert home_launches.empty(), "Denied trip contacted the destination Gatekeeper"
      home.shutdown()
      sys.exit(0)
     path, launch = home_launches.get(timeout=8)
     assert path == f"/foreignagent/{local_agent}/", (path, launch)
     token = launch["service_session_id"]
     assert token.startswith(home_uri + ";"), launch
     assert launch["serviceurls"]["HomeURI"] == "http://127.0.0.1:8002/", launch
     assert launch["viewer"] == "Fixture 1.0", launch["viewer"]
     assert launch["client_ip"] == "127.0.0.1", launch["client_ip"]
     for _ in range(64):
      if viewer.recv(4096)[6:10] == bytes.fromhex("ffff004a"):
       break
     else:
      raise AssertionError("Viewer did not receive the destination denial")
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      assert proxy.verify_agent({"sessionID":str(local_session), "token":token}) == {"result":"False"}
     accept_home_launch.set()
     viewer.sendto(struct.pack(">BIB", 0x40, 4, 0) + bytes.fromhex("ffff003f") +
                   local_agent.bytes + local_session.bytes + struct.pack("<Q6f", local_x << 32, 120, 121, 45, 1, 0, 0),
                   endpoint)
     path, launch = home_launches.get(timeout=8)
     assert path == f"/foreignagent/{local_agent}/" and launch["service_session_id"] != token, launch
     path, update = outbound_updates.get(timeout=8)
     assert path.startswith(f"/agent/{local_agent}/") and update["session_uuid"] == str(local_session), update
     for _ in range(64):
      packet = viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff0045"):
       assert struct.unpack_from(">H", packet, 34)[0] == 9000, packet.hex()
       break
     else:
      raise AssertionError("Viewer did not receive native HomeAgent TeleportFinish")
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      assert proxy.verify_agent({"sessionID":str(local_session), "token":launch["service_session_id"]}) == {"result":"True"}
     next_launch = {**launch, "gatekeeper_serveruri":home_uri.rstrip("/"), "destination_serveruri":home_uri.rstrip("/")}
     if "--trusted-home-launch" in sys.argv[2:]:
      accept_home_launch.clear()
      c.request("POST", f"/homeagent/{local_agent}/",
                json.dumps({**next_launch, "client_ip": "192.0.2.91"}),
                {"Content-Type": "application/json"})
      reply = c.getresponse()
      assert reply.status == 200 and json.loads(reply.read())["reason"] == "Fixture denied home-agent launch"
      _, rejected_relaunch = home_launches.get(timeout=8)
      assert rejected_relaunch["client_ip"] == launch["client_ip"], rejected_relaunch

      new_session = str(uuid.uuid4())
      next_launch.update(session_id=new_session, secure_session_id=str(uuid.uuid4()), service_session_id="")
      accept_home_launch.clear()
      c.request("POST", f"/homeagent/{local_agent}/", json.dumps(next_launch),
                {"Content-Type":"application/json"})
      reply = c.getresponse()
      denial = json.loads(reply.read())
      assert reply.status == 200 and denial["reason"] == "Fixture denied home-agent launch", denial
      _, rejected_launch = home_launches.get(timeout=8)
      with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
       assert proxy.verify_agent({"sessionID":new_session, "token":rejected_launch["service_session_id"]}) == {"result":"False"}
       assert proxy.verify_agent({"sessionID":str(local_session), "token":launch["service_session_id"]}) == {"result":"True"}
      accept_home_launch.set()
      legacy_launch = {key: value for key, value in next_launch.items() if key != "gatekeeper_serveruri"}
      legacy_launch.update(gatekeeper_host="127.0.0.1", gatekeeper_port=str(home.server_address[1]))
      c.request("POST", f"/homeagent/{local_agent}/", json.dumps(legacy_launch),
                {"Content-Type":"application/json"})
      reply = c.getresponse()
      result = json.loads(reply.read())
      assert reply.status == 200 and result.get("success") is True, result
      path, issued = home_launches.get(timeout=8)
      assert path == f"/foreignagent/{local_agent}/" and issued["session_id"] == new_session
      with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
       assert proxy.verify_agent({"sessionID":new_session, "token":issued["service_session_id"]}) == {"result":"True"}
       assert proxy.verify_agent({"sessionID":str(local_session), "token":launch["service_session_id"]}) == {"result":"False"}
      home.shutdown()
      sys.exit(0)
     c.request("POST", f"/homeagent/{local_agent}/",
               json.dumps({**next_launch, "service_session_id":"invalid"}),
               {"Content-Type":"application/json", "X-Forwarded-For":"192.0.2.1"})
     reply = c.getresponse()
     assert reply.status == 200 and json.loads(reply.read())["reason"] == "Unable to verify identity"
     accept_home_launch.clear()
     c.request("POST", f"/homeagent/{local_agent}/", json.dumps(next_launch),
               {"Content-Type":"application/json"})
     reply = c.getresponse()
     denial = json.loads(reply.read())
     assert reply.status == 200 and not denial.get("success", False) and denial["reason"] == "Fixture denied home-agent launch", denial
     _, denied_launch = home_launches.get(timeout=8)
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      assert proxy.verify_agent({"sessionID":str(local_session), "token":denied_launch["service_session_id"]}) == {"result":"False"}
      assert proxy.verify_agent({"sessionID":str(local_session), "token":launch["service_session_id"]}) == {"result":"True"}
     accept_home_launch.set()
     c.request("POST", f"/homeagent/{local_agent}/", json.dumps(next_launch),
               {"Content-Type":"application/json"})
     reply = c.getresponse()
     assert reply.status == 200 and json.loads(reply.read())["success"] is True
     path, relaunched = home_launches.get(timeout=8)
     assert path == f"/foreignagent/{local_agent}/" and relaunched["service_session_id"] != launch["service_session_id"]
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      assert proxy.verify_agent({"sessionID":str(local_session), "token":launch["service_session_id"]}) == {"result":"False"}
      assert proxy.verify_agent({"sessionID":str(local_session), "token":relaunched["service_session_id"]}) == {"result":"True"}
     home.shutdown()
     sys.exit(0)
    c.request("POST","/",xmlrpc.client.dumps(({"region_name":"Foreign Test"},),methodname="link_region"),{"Content-Type":"text/xml"})
    linked=xmlrpc.client.loads(c.getresponse().read())[0][0]
    assert linked["result"]=="True",linked
    if not sys.argv[2:]:
     assert linked["external_name"] == "http://127.0.0.1:18001/ Foreign Test", linked
    c.request("POST","/",xmlrpc.client.dumps(({"region_uuid":linked["uuid"]},),methodname="get_region"),{"Content-Type":"text/xml"})
    region=xmlrpc.client.loads(c.getresponse().read())[0][0]
    assert int(region["internal_port"])==udp,(region,udp)
    agent,session=uuid.uuid4(),uuid.uuid4()
    code=456789
    gatekeeper_fixture_port = 8002 if sys.argv[2:] else 18001
    payload={"agent_id":str(agent),"session_id":str(session),"secure_session_id":str(uuid.uuid4()),"circuit_code":str(code),"viewer":"Fixture Viewer","mac":"allowed-mac","id0":"allowed-id0","first_name":"Visiting","last_name":"Avatar","caps_path":"foreign-fixture","start_pos":"<128, 128, 70>","destination_uuid":linked["uuid"],"service_session_id":f"http://127.0.0.1:{gatekeeper_fixture_port}/;fixture","client_ip":"127.0.0.1","serviceurls":{"HomeURI":home_uri}}
    for viewer_name, outcome in (("Unknown Viewer", "not allowed"), ("fixture Blocked", "denied")):
     c.request("POST", f"/foreignagent/{agent}/", json.dumps(payload | {"viewer": viewer_name}),
               {"Content-Type": "application/json"})
     denied = c.getresponse()
     assert denied.status == 200 and json.loads(denied.read())["reason"] == f"Login failed: client {viewer_name} is {outcome}"
    denied_identifiers = [("mac", "ABC123", "Mac"), ("id0", "DEF456", "id0")]
    if not sys.argv[2:]:
     denied_identifiers.append(("mac", "cafe\u0301", "Mac"))
    for field, value, label in denied_identifiers:
     c.request("POST", f"/foreignagent/{agent}/", json.dumps(payload | {field: value}),
               {"Content-Type": "application/json"})
     denied = c.getresponse()
     assert denied.status == 200 and json.loads(denied.read())["reason"] == f"Login failed: client with {label} {value} is denied"
    reserved = "6571e388-6218-4574-87db-f9379718315e"
    c.request("POST", f"/foreignagent/{reserved}/", json.dumps(payload | {"agent_id": reserved}),
              {"Content-Type": "application/json"})
    rejected = c.getresponse()
    assert rejected.status == 200 and json.loads(rejected.read())["reason"] == "Invalid account ID"
    c.request("POST", f"/foreignagent/{agent}/", json.dumps(payload),
              {"Content-Type": "application/json", "X-SecondLife-Shard": "fixture"})
    rejected = c.getresponse()
    assert rejected.status == 406 and rejected.read() == b"false"
    admission_payload = {**payload, "serviceurls":{"HomeURI":home_uri, "FriendsServerURI":home_uri}}
    if not sys.argv[2:]:
     def visiting_service_urls(fields):
      urls = {"SRV_FriendsServerURI": home_uri}
      if fields.get("userID") == str(agent):
       urls["SRV_AssetServerURI"] = home_uri
      return urls
     home.register_function(visiting_service_urls, "get_server_urls")
     for rejected_port in (8002, 18003):
      rejected_payload = admission_payload | {"service_session_id":f"http://127.0.0.1:{rejected_port}/;fixture"}
      c.request("POST", f"/foreignagent/{agent}/", json.dumps(rejected_payload),
                {"Content-Type":"application/json"})
      rejected = c.getresponse()
      assert rejected.status == 200 and json.loads(rejected.read())["reason"] == "Unable to verify identity"
     admission_payload["service_session_id"] = "http://127.0.0.1:18002/;fixture"
    if telehub_denial:
     blocked_session = uuid.uuid4()
     blocked_payload = admission_payload | {
         "agent_id": str(blocked_visitor), "session_id": str(blocked_session),
         "secure_session_id": str(uuid.uuid4()), "caps_path": "blocked-telehub",
     }
     c.request("POST", f"/foreignagent/{blocked_visitor}/", json.dumps(blocked_payload),
               {"Content-Type": "application/json"})
     response = c.getresponse()
     denied = json.loads(response.read())
     assert response.status == 200 and not denied.get("success", False), denied
     assert denied["reason"] == "No suitable landing point found", denied
     c.request("POST", "/CAPS/blocked-telehub0000/", "<llsd><array/></llsd>",
               {"Content-Type": "application/llsd+xml"})
     response = c.getresponse()
     assert response.status == 404, response.read()
     response.read()
     with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
      assert database.execute("SELECT 1 FROM Presence WHERE SessionID=?",
                              (str(blocked_session),)).fetchone() is None
     blocked_user = f"{blocked_visitor};{home_uri};Visiting Avatar"
     assert b"<Online>True</Online>" not in grid_user_status(c, blocked_user)
    c.request("POST",f"/foreignagent/{agent}/",json.dumps(admission_payload),{"Content-Type":"application/json"})
    r=c.getresponse()
    body=json.loads(r.read())
    assert r.status==200 and body.get("success") is True, (r.status,body)
    if telehub_denial:
     print("PASS: Gatekeeper denies a fully restricted telehub and admits an unrestricted visitor")
     home.shutdown()
     sys.exit(0)
    c.request("POST", f"/foreignagent/{agent}/",
              json.dumps(admission_payload | {"secure_session_id":str(uuid.uuid4())}),
              {"Content-Type":"application/json"})
    rejected = c.getresponse()
    rejection = json.loads(rejected.read())
    assert rejected.status == 200 and rejection.get("success", False) is False and rejection["reason"] == "Unable to login presence", rejection
    user = f"{agent};{home_uri};Visiting Avatar"
    assert b"<Online>True</Online>" in grid_user_status(c, user)
    c.request("POST", "/CAPS/foreign-fixture0000/", "<llsd><array/></llsd>",
              {"Content-Type": "application/llsd+xml"})
    seed = c.getresponse()
    assert seed.status == 200, seed.status
    seed.read()
   viewer.sendto(struct.pack(">BIB",0x40,1,0)+b"\xff\xff\x00\x03"+struct.pack("<I",code)+session.bytes+agent.bytes,("127.0.0.1",udp))
   viewer.settimeout(2)
   assert viewer.recv(4096)[6:10] == bytes.fromhex("fffffffb")
   viewer.settimeout(16)
   assert calls.get(timeout=3)=={"sessionID":str(session),"token":"127.0.0.1"}
   if deny_client:
    viewer.settimeout(0.5)
    try:
     packet = viewer.recv(4096)
    except TimeoutError:
     packet = None
    assert packet is None, "A denied viewer received a packet after its initial acknowledgment"
    viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff0003") +
                  struct.pack("<I", code) + session.bytes + agent.bytes, ("127.0.0.1", udp))
    assert viewer.recv(4096)[6:10] == bytes.fromhex("fffffffb")
    try:
     repeated = calls.get(timeout=0.5)
    except queue.Empty:
     repeated = None
    assert repeated is None, "A denied circuit reached the home grid again"
    with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as c:
     c.request("POST", "/presence", f"METHOD=getagent&SessionID={session}",
               {"Content-Type": "application/x-www-form-urlencoded"})
     response = c.getresponse()
     assert response.status == 200 and b"<result>null</result>" in response.read()
     assert b"<Online>False</Online>" in grid_user_status(c, user)
     c.request("POST", "/CAPS/foreign-fixture0000/", "<llsd><array/></llsd>",
               {"Content-Type": "application/llsd+xml"})
     seed = c.getresponse()
     assert seed.status == 404, seed.status
     seed.read()
    print("PASS home-grid denial prevents the viewer handshake")
   else:
    packet=viewer.recv(4096)
    payload=packet[6:]
    if packet[0] & 0x80:
     decoded=bytearray()
     iterator=iter(payload)
     for byte in iterator:
      decoded.extend(bytes(next(iterator)) if byte==0 else bytes([byte]))
     payload=decoded
    assert payload.startswith(b"\xff\xff\x00\x94"), payload[:16].hex()
    viewer.sendto(struct.pack(">BIB", 0x40, 2, 0) + bytes.fromhex("ffff00f9") +
                  agent.bytes + session.bytes + struct.pack("<I", code), ("127.0.0.1", udp))
    viewer.settimeout(4)
    for _ in range(64):
     movement = viewer.recv(4096)
     if movement[6:10] == bytes.fromhex("ffff00fa"):
      break
    else:
     raise AssertionError("Viewer did not complete region entry")
    with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as connection:
     if check_map_features:
      assert viewer_map_url(connection, "/CAPS/foreign-fixture0000/") == "https://visitors.example/map/"
     deadline = time.monotonic() + 5
     while True:
      status = ET.fromstring(grid_user_status(connection, user))
      if status.findtext("result/LastRegionID") == linked["uuid"]:
       assert status.findtext("result/UserID") == user, ET.tostring(status)
       break
      if time.monotonic() >= deadline:
       raise AssertionError("GridUser did not record the visitor's region under their Hypergrid identity")
      time.sleep(0.02)
    if not sys.argv[2:]:
     def receive_image():
      wire = viewer.recv(4096)
      decoded = wire[6:]
      if wire[0] & 0x80:
       decoded = bytearray()
       encoded = iter(wire[6:])
       for byte in encoded:
        decoded.extend(bytes(next(encoded)) if byte == 0 else bytes([byte]))
      if wire[0] & 0x40 and (decoded[:1] in (b"\x09", b"\x0a", b"\x11") or
                             decoded[:4] == b"\xff\xff\x00\x9a"):
       sequence = struct.unpack_from(">I", wire, 1)[0]
       viewer.sendto(struct.pack(">BIB", 0, 0, 0) + b"\xff\xff\xff\xfb\x01" +
                     struct.pack("<I", sequence), ("127.0.0.1", udp))
      return decoded
     for sequence, texture_id, data, discard, parts in (
         (2000, remote_texture_id, remote_texture, 0, 1),
         (2001, discard_texture_id, discard_texture, 4, 4)):
      viewer.sendto(struct.pack(">BIBB", 0x40, sequence, 0, 8) + agent.bytes + session.bytes +
                    b"\x01" + texture_id.bytes + struct.pack("<bfIB", discard, 1.0, 1, 0),
                    ("127.0.0.1", udp))
      for _ in range(128):
       image = receive_image()
       if image[:17] != b"\x09" + texture_id.bytes:
        continue
       header = data[:600]
       assert struct.unpack_from("<BIHH", image, 17) == (2, len(data), parts, len(header))
       assert image[26:] == header
       break
      else:
       raise AssertionError("Visiting viewer did not receive its home-grid texture")
     deadline = time.monotonic() + 0.5
     while (remaining := deadline - time.monotonic()) > 0:
      viewer.settimeout(remaining)
      try:
       image = receive_image()
      except TimeoutError:
       break
      assert image[:17] != b"\x0a" + discard_texture_id.bytes, "Discard level sent extra texture packets"
     viewer.settimeout(4)
     viewer.sendto(struct.pack(">BIBB", 0x40, 2002, 0, 8) + agent.bytes + session.bytes +
                   b"\x01" + layered_texture_id.bytes + struct.pack("<bfIB", 2, 1.0, 1, 0),
                   ("127.0.0.1", udp))
     parts = {}
     for _ in range(128):
      image = receive_image()
      if image[:17] == b"\x09" + layered_texture_id.bytes:
       assert struct.unpack_from("<BIHH", image, 17) == (2, len(layered_texture), 37, 600)
       parts[0] = image[26:]
      elif image[:17] == b"\x0a" + layered_texture_id.bytes:
       part, size = struct.unpack_from("<HH", image, 17)
       assert 1 <= part <= 8 and size == 1000
       parts[part] = image[21:]
      if len(parts) == 9:
       break
     assert len(parts) == 9, "Discard level did not reach the decoded JPEG 2000 layer boundary"
     assert b"".join(parts[index] for index in range(9)) == layered_texture[:8600]
     def request_texture(sequence, texture_id, discard, priority, image_type=0):
      viewer.sendto(struct.pack(">BIBB", 0x40, sequence, 0, 8) + agent.bytes + session.bytes +
                    b"\x01" + texture_id.bytes + struct.pack("<bfIB", discard, priority, 1, image_type),
                    ("127.0.0.1", udp))
     try:
      request_texture(2003, slow_texture_id, 0, 1.0, 1)
      assert slow_texture_started.wait(3), "Baked texture lookup did not start"
      request_texture(2004, ready_texture_id, 0, 1.5)
      assert ready_texture_served.wait(3), "Lower priority texture lookup did not finish"
      deadline = time.monotonic() + 0.5
      while (remaining := deadline - time.monotonic()) > 0:
       viewer.settimeout(remaining)
       try:
        image = receive_image()
       except TimeoutError:
        break
       assert image[:17] != b"\x09" + ready_texture_id.bytes, "Texture bypassed higher baked priority"
      request_texture(2005, slow_texture_id, -1, 0.0)
      viewer.settimeout(4)
      for _ in range(128):
       image = receive_image()
       if image[:17] == b"\x09" + ready_texture_id.bytes:
        assert image[26:] == remote_texture
        break
      else:
       raise AssertionError("Canceling the baked texture did not unblock the ready texture")
     finally:
      release_slow_texture.set()
     def request_asset_transfer(sequence, asset_id):
      transfer_id = uuid.uuid4()
      params = asset_id.bytes + struct.pack("<i", 0)
      viewer.sendto(struct.pack(">BIBHH", 0x40, sequence, 0, 0xffff, 153) +
                    transfer_id.bytes + struct.pack("<iifH", 2, 2, 1.0, len(params)) + params,
                    ("127.0.0.1", udp))
      return transfer_id, params
     transfer, params = request_asset_transfer(2006, discard_texture_id)
     header_seen = False
     chunks = {}
     for _ in range(128):
      packet = receive_image()
      if packet[:20] == b"\xff\xff\x00\x9a" + transfer.bytes:
       assert struct.unpack_from("<iiiiH", packet, 20) == (2, 0, 0, len(discard_texture), len(params))
       assert packet[38:] == params
       header_seen = True
      elif packet[:17] == b"\x11" + transfer.bytes:
       channel, index, status, length = struct.unpack_from("<iiiH", packet, 17)
       assert channel == 2 and 0 <= index < 5 and status == (1 if index == 4 else 0) and length == 600
       chunks[index] = packet[31:]
      if header_seen and len(chunks) == 5:
       break
     assert header_seen and len(chunks) == 5, "Viewer did not receive the complete asset transfer"
     assert b"".join(chunks[index] for index in range(5)) == discard_texture
     transfer, params = request_asset_transfer(2007, uuid.uuid4())
     for _ in range(128):
      packet = receive_image()
      if packet[:20] == b"\xff\xff\x00\x9a" + transfer.bytes:
       assert struct.unpack_from("<iiiiH", packet, 20) == (2, 0, -2, 0, len(params))
       assert packet[38:] == params
       break
     else:
      raise AssertionError("Missing asset did not return TransferInfo failure")
     inventory_item = uuid.uuid4()
     with closing(sqlite3.connect(Path(d) / "inventory.db")) as database:
      database.execute(
          "INSERT INTO inventoryitems (inventoryID, avatarID, assetID, assetType, invType, inventoryName) "
          "VALUES (?, ?, ?, 0, 0, 'UDP inventory fixture')",
          (str(inventory_item), str(agent), str(remote_texture_id)))
      database.commit()
     def request_inventory_asset(sequence, asset_id):
      transfer_id = uuid.uuid4()
      params = bytes(64) + inventory_item.bytes + asset_id.bytes + struct.pack("<i", 0)
      viewer.sendto(struct.pack(">BIBHH", 0x40, sequence, 0, 0xffff, 153) +
                    transfer_id.bytes + struct.pack("<iifH", 2, 3, 1.0, len(params)) + params,
                    ("127.0.0.1", udp))
      return transfer_id, params
     transfer, params = request_inventory_asset(2008, remote_texture_id)
     header_seen = data_seen = False
     for _ in range(128):
      packet = receive_image()
      if packet[:20] == b"\xff\xff\x00\x9a" + transfer.bytes:
       assert struct.unpack_from("<iiiiH", packet, 20) == (2, 0, 0, len(remote_texture), len(params))
       assert packet[38:] == params
       header_seen = True
      elif packet[:17] == b"\x11" + transfer.bytes:
       assert struct.unpack_from("<iiiH", packet, 17) == (2, 0, 1, len(remote_texture))
       assert packet[31:] == remote_texture
       data_seen = True
      if header_seen and data_seen:
       break
     assert header_seen and data_seen, "Inventory-authorized transfer did not arrive"
     mismatch, _ = request_inventory_asset(2009, discard_texture_id)
     deadline = time.monotonic() + 0.5
     while (remaining := deadline - time.monotonic()) > 0:
      viewer.settimeout(remaining)
      try:
       packet = receive_image()
      except TimeoutError:
       break
      assert packet[:20] != b"\xff\xff\x00\x9a" + mismatch.bytes
      assert packet[:17] != b"\x11" + mismatch.bytes
     viewer.settimeout(4)
    with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
     sender_login = proxy.login_to_simulator({"first":"Offer", "last":"Recipient",
                                              "passwd":"fixture-password", "start":"last"})
    assert sender_login["login"] == "true", sender_login
    with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as offer_viewer:
     offer_viewer.settimeout(5)
     endpoint = ("127.0.0.1", udp)
     sender_session = uuid.UUID(sender_login["session_id"]).bytes
     sender_circuit = struct.pack("<I", int(sender_login["circuit_code"]) & 0xffffffff)
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 1, 0, 0xffff, 3) +
                         sender_circuit + sender_session + offer_recipient.bytes, endpoint)
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 2, 0, 0xffff, 249) +
                         offer_recipient.bytes + sender_session + sender_circuit, endpoint)
     for _ in range(64):
      if offer_viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
       break
     else:
      raise AssertionError("Offer sender did not enter the region")
     incoming = b"Offer from connected foreign visitor\0"
     viewer.sendto(struct.pack(">BIBHH", 0x40, 110, 0, 0xffff, 254) +
                   agent.bytes + session.bytes + b"\0" + offer_recipient.bytes +
                   bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                   struct.pack("<H", len(incoming)) + incoming + bytes(2), endpoint)
     for _ in range(64):
      if incoming.rstrip(b"\0") in offer_viewer.recv(4096):
       break
     else:
      raise AssertionError("Local viewer did not receive the visitor's offer")
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      assert database.execute("SELECT 1 FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                              (str(offer_recipient), str(agent))).fetchone() is None
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 111, 0, 0xffff, 252) +
                         offer_recipient.bytes + sender_session, endpoint)
     for _ in range(64):
      logout = offer_viewer.recv(4096)[6:]
      if logout.startswith((bytes.fromhex("ffff00fd"), bytes.fromhex("ffff0001fd"))):
       break
     else:
      raise AssertionError("Offer recipient did not receive the logout reply")
     with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       if database.execute("SELECT 1 FROM Presence WHERE SessionID = ?",
                           (sender_login["session_id"],)).fetchone() is None:
        break
       time.sleep(0.02)
      else:
       raise AssertionError("Offer recipient's presence survived logout")
     with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as proxy:
      relogin = proxy.login_to_simulator({"first":"Offer", "last":"Recipient",
                                          "passwd":"fixture-password", "start":"last"})
     assert relogin["login"] == "true", relogin
     new_session = uuid.UUID(relogin["session_id"]).bytes
     assert new_session != sender_session
     sender_session = new_session
     sender_circuit = struct.pack("<I", int(relogin["circuit_code"]) & 0xffffffff)
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 1, 0, 0xffff, 3) +
                         sender_circuit + sender_session + offer_recipient.bytes, endpoint)
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 2, 0, 0xffff, 249) +
                         offer_recipient.bytes + sender_session + sender_circuit, endpoint)
     for _ in range(64):
      if offer_viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
       break
     else:
      raise AssertionError("Offer recipient did not reenter the region")
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 112, 0, 0xffff, 297) +
                         offer_recipient.bytes + sender_session + agent.bytes + b"\0", endpoint)
     try:
      unexpected = home_friend_requests.get(timeout=0.5)
     except queue.Empty:
      pass
     else:
      raise AssertionError(f"Previous-session offer was accepted: {unexpected}")
     viewer.sendto(struct.pack(">BIBHH", 0x40, 113, 0, 0xffff, 254) +
                   agent.bytes + session.bytes + b"\0" + offer_recipient.bytes +
                   bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                   struct.pack("<H", len(incoming)) + incoming + bytes(2), endpoint)
     for _ in range(64):
      if incoming.rstrip(b"\0") in offer_viewer.recv(4096):
       break
     else:
      raise AssertionError("Relogged viewer did not receive a fresh visitor offer")
     managed_friends = os.environ.get("OPENSIM_TEST_MANAGED_FRIENDS_URI")
     if managed_friends:
      managed_db = Path(os.environ["OPENSIM_TEST_MANAGED_FRIENDS_DB"])
      with closing(sqlite3.connect(managed_db)) as database:
       database.execute("INSERT INTO hg_traveling_data (SessionID, UserID, ServiceToken) VALUES (?, ?, ?)",
                        (str(session), str(agent), admission_payload["service_session_id"]))
       database.commit()
      managed_requests = queue.Queue()
      home.managed_friends = (str(agent), managed_friends, managed_requests)
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 114, 0, 0xffff, 297) +
                         offer_recipient.bytes + sender_session + agent.bytes + b"\0", endpoint)
     if managed_friends:
      visitor_offer_acceptance, status, reply = managed_requests.get(timeout=5)
      del home.managed_friends
      assert status == 200 and b"<Result>Success</Result>" in reply, (status, reply)
      with closing(sqlite3.connect(managed_db)) as database:
       rows = database.execute("SELECT Friend, Flags FROM Friends WHERE PrincipalID = ?",
                               (str(agent),)).fetchall()
       assert rows == [(visitor_offer_acceptance["Friend"][0], "0")], rows
     else:
      visitor_offer_acceptance = home_friend_requests.get(timeout=5)
     assert visitor_offer_acceptance["METHOD"] == ["newfriendship"]
     assert visitor_offer_acceptance["PrincipalID"] == [str(agent)]
     assert visitor_offer_acceptance["SESSIONID"] == [str(session)]
     assert visitor_offer_acceptance["KEY"] == [admission_payload["service_session_id"]]
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      record = database.execute("SELECT Friend FROM Friends WHERE PrincipalID = ? AND Friend LIKE ? AND Flags = 1",
                                (str(offer_recipient), str(agent) + ";%")).fetchone()
      assert record and record[0].rsplit(";", 1)[1] == visitor_offer_acceptance["Friend"][0].rsplit(";", 1)[1]
      database.execute("DELETE FROM Friends WHERE Friend = ? OR PrincipalID = ?", (record[0], record[0]))
      database.commit()
     message = b"Offer to connected foreign visitor\0"
     offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 3, 0, 0xffff, 254) +
                         offer_recipient.bytes + sender_session + b"\0" + agent.bytes +
                         bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                         struct.pack("<H", len(message)) + message + bytes(2), endpoint)
     for _ in range(64):
      if message.rstrip(b"\0") in viewer.recv(4096):
       break
     else:
      raise AssertionError("Foreign visitor did not receive viewer offer")
     viewer.sendto(struct.pack(">BIBHH", 0x40, 104, 0, 0xffff, 297) +
                   agent.bytes + session.bytes + offer_recipient.bytes + b"\0", endpoint)
     acceptance = home_friend_requests.get(timeout=5)
     assert acceptance["METHOD"] == ["newfriendship"]
     assert acceptance["PrincipalID"] == [str(agent)]
     assert acceptance["SESSIONID"] == [str(session)]
     assert acceptance["KEY"] == [admission_payload["service_session_id"]]
     secret = acceptance["Friend"][0].rsplit(";", 1)[1]
     assert len(secret) == 8
     with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
      visitor = database.execute("SELECT Friend FROM Friends WHERE PrincipalID = ? AND Friend LIKE ? AND Flags = 1",
                                 (str(offer_recipient), str(agent) + ";%")).fetchone()
      assert visitor and visitor[0].endswith(";" + secret)
      assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                              (visitor[0], str(offer_recipient))).fetchone() == ("1",)
     with HomeServer(("127.0.0.1", 0), requestHandler=HomeRequestHandler, logRequests=False) as second_friends:
      second_friends.friend_requests = queue.Queue()
      second_friends_uri = f"http://127.0.0.1:{second_friends.server_address[1]}/"
      second_friends_thread = threading.Thread(target=second_friends.serve_forever, daemon=True)
      second_friends_thread.start()
      try:
       other_visitor, other_session = uuid.uuid4(), uuid.uuid4()
       other_code = code + 1
       other_payload = {**admission_payload, "agent_id":str(other_visitor), "session_id":str(other_session),
                        "secure_session_id":str(uuid.uuid4()), "circuit_code":str(other_code),
                        "first_name":"Second", "caps_path":"second-foreign-fixture",
                        "service_session_id":f"http://127.0.0.1:{gatekeeper_fixture_port}/;second-fixture"}
       other_payload["serviceurls"] = {**admission_payload["serviceurls"], "FriendsServerURI":second_friends_uri}
       with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as admission:
        admission.request("POST", f"/foreignagent/{other_visitor}/", json.dumps(other_payload),
                          {"Content-Type":"application/json"})
        reply = admission.getresponse()
        assert reply.status == 200 and json.loads(reply.read())["success"]
       with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as other_viewer:
        other_viewer.settimeout(5)
        other_viewer.sendto(struct.pack(">BIBHH", 0x40, 1, 0, 0xffff, 3) +
                             struct.pack("<I", other_code) + other_session.bytes + other_visitor.bytes, endpoint)
        other_viewer.sendto(struct.pack(">BIBHH", 0x40, 2, 0, 0xffff, 249) +
                             other_visitor.bytes + other_session.bytes + struct.pack("<I", other_code), endpoint)
        for _ in range(64):
         if other_viewer.recv(4096)[6:10] == bytes.fromhex("ffff00fa"):
          break
        else:
         raise AssertionError("Second visitor did not enter the region")
        message = b"Offer between two foreign visitors\0"
        viewer.sendto(struct.pack(">BIBHH", 0x40, 115, 0, 0xffff, 254) +
                      agent.bytes + session.bytes + b"\0" + other_visitor.bytes +
                      bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                      struct.pack("<H", len(message)) + message + bytes(2), endpoint)
        for _ in range(64):
         if message.rstrip(b"\0") in other_viewer.recv(4096):
          break
        else:
         raise AssertionError("Second visitor did not receive the offer")
        other_viewer.sendto(struct.pack(">BIBHH", 0x40, 3, 0, 0xffff, 297) +
                             other_visitor.bytes + other_session.bytes + agent.bytes + b"\0", endpoint)
        confirmations = [home_friend_requests.get(timeout=5), second_friends.friend_requests.get(timeout=5)]
        assert confirmations[0]["PrincipalID"] == [str(agent)]
        assert confirmations[1]["PrincipalID"] == [str(other_visitor)]
        by_principal = {item["PrincipalID"][0]:item for item in confirmations}
        secrets = set()
        for principal, authenticated, token, friend in (
            (agent, session, admission_payload["service_session_id"], other_visitor),
            (other_visitor, other_session, other_payload["service_session_id"], agent)):
         form = by_principal[str(principal)]
         assert form["METHOD"] == ["newfriendship"] and form["SESSIONID"] == [str(authenticated)]
         assert form["KEY"] == [token] and form["Friend"][0].startswith(f"{friend};{home_uri};")
         secrets.add(form["Friend"][0].rsplit(";", 1)[1])
        assert len(secrets) == 1 and len(next(iter(secrets))) == 8
        other_viewer.sendto(struct.pack(">BIBHH", 0x40, 4, 0, 0xffff, 252) +
                             other_visitor.bytes + other_session.bytes, endpoint)
        assert home_logouts.get(timeout=5) == {"userID":str(other_visitor), "sessionID":str(other_session)}
        outbound = b"Offer after visitor logout\0"
        offer_viewer.sendto(struct.pack(">BIBHH", 0x40, 116, 0, 0xffff, 254) +
                            offer_recipient.bytes + sender_session + b"\0" + other_visitor.bytes +
                            bytes(32) + bytes((0, 38)) + bytes(20) + b"\0" +
                            struct.pack("<H", len(outbound)) + outbound + bytes(2), endpoint)
        remote_offer = second_friends.friend_requests.get(timeout=5)
        assert remote_offer == {"METHOD":["friendship_offered"], "FromID":[str(offer_recipient)],
                                "ToID":[str(other_visitor)], "FromName":["Offer.Recipient@127.0.0.1:8002"],
                                "Message":["Offer after visitor logout"]}, remote_offer
        with closing(sqlite3.connect(Path(d) / "friends.db")) as database:
         assert database.execute("SELECT Flags FROM Friends WHERE PrincipalID = ? AND Friend = ?",
                                 (str(other_visitor), str(offer_recipient))).fetchone() == ("0",)
       with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as origin:
        origin.request("POST", "/hgfriends", urllib.parse.urlencode({
            "METHOD":"validate_friendship_offered", "PrincipalID":str(offer_recipient),
            "Friend":str(other_visitor)}), {"Content-Type":"application/x-www-form-urlencoded"})
        validated = origin.getresponse()
        assert validated.status == 200 and ET.fromstring(validated.read()).findtext("RESULT") == "True"
      finally:
       second_friends.shutdown()
       second_friends_thread.join(timeout=5)
    handle = struct.unpack_from("<Q", movement, 66)[0]
    viewer.sendto(struct.pack(">BIB", 0x40, 3, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", handle, 120, 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    started = False
    for _ in range(64):
     teleport = viewer.recv(4096)
     if teleport[6:10] == bytes.fromhex("ffff0049"):
      started = True
     if teleport[6:10] == bytes.fromhex("ffff0040"):
      assert started, "TeleportLocal arrived before TeleportStart"
      assert struct.unpack_from("<I3f", teleport, 26) == (2, 120, 121, 45), teleport.hex()
      break
    else:
     raise AssertionError("Viewer did not receive TeleportLocal")
    viewer.sendto(struct.pack(">BIB", 0x40, 4, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", handle, float("nan"), 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    for _ in range(64):
     failure = viewer.recv(4096)
     if failure[6:10] == bytes.fromhex("ffff004a"):
      assert b"Invalid teleport coordinates" in failure, failure.hex()
      break
    else:
     raise AssertionError("Viewer did not receive TeleportFailed")
    linked_handle = (int(remote_region.findtext("locX")) << 32) | int(remote_region.findtext("locY"))
    viewer.sendto(struct.pack(">BIB", 0x40, 7, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", linked_handle, 120, 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    assert hypergrid_queries.get(timeout=5) == {
        "region_uuid": remote_id, "agent_id": str(agent), "agent_home_uri": home_uri}
    for _ in range(64):
     packet = viewer.recv(4096)
     if packet[6:10] == bytes.fromhex("ffff004a") and b"Fixture denied Hypergrid destination" in packet:
      break
    else:
     raise AssertionError("Viewer did not receive the Gatekeeper denial")
    for sequence, remote_reply, message in (
        (70, {"result": "False"}, b"The teleport destination could not be found."),
        (71, {"result": "True", "uuid": remote_id, "internal_port": 9000}, b"Error parsing response from grid."),
        (72, {"result": "True", "uuid": remote_id, "internal_port": "9000", "server_uri": home_uri},
         b"Could not resolve destination Address"),
    ):
     hypergrid_replies.put(remote_reply)
     viewer.sendto(struct.pack(">BIB", 0x40, sequence, 0) + bytes.fromhex("ffff003f") +
                   agent.bytes + session.bytes + struct.pack("<Q6f", linked_handle, 120, 121, 45, 1, 0, 0),
                   ("127.0.0.1", udp))
     assert hypergrid_queries.get(timeout=5)["region_uuid"] == remote_id
     for _ in range(64):
      packet = viewer.recv(4096)
      if packet[6:10] == bytes.fromhex("ffff004a"):
       assert message in packet, packet
       break
     else:
      raise AssertionError(f"Viewer did not receive the Gatekeeper failure {message!r}")
    resolve_hypergrid.set()
    hypergrid_replies.put({
        "result": " True ", "uuid": remote_id, "x": "unknown",
        "hostname": "127.0.0.1", "internal_port": "9000", "server_uri": home_uri,
    })
    viewer.sendto(struct.pack(">BIB", 0x40, 8, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", linked_handle, 120, 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    assert hypergrid_queries.get(timeout=5)["region_uuid"] == remote_id
    path, query = access_queries.get(timeout=5)
    assert path == f"/agent/{agent}/{remote_id}/" and json.loads(query)["agent_home_uri"] == home_uri
    for _ in range(64):
     packet = viewer.recv(4096)
     if packet[6:10] == bytes.fromhex("ffff004a") and b"Fixture denied teleport" in packet:
      resolved_failure_sequence = packet[1:5]
      break
    else:
     raise AssertionError("Viewer did not receive the resolved simulator denial")
    allow_hypergrid_access.set()
    viewer.sendto(struct.pack(">BIB", 0x40, 9, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", linked_handle, 120, 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    assert hypergrid_queries.get(timeout=5)["region_uuid"] == remote_id
    assert access_queries.get(timeout=5)[0] == f"/agent/{agent}/{remote_id}/"
    launch_failures = []
    for _ in range(64):
     packet = viewer.recv(4096)
     if packet[6:10] == bytes.fromhex("ffff004a"):
      launch_failures.append(packet[27:])
     if packet[6:10] == bytes.fromhex("ffff004a") and b"Fixture denied home-agent launch" in packet:
      home_failure_sequence = packet[1:5]
      break
    else:
     raise AssertionError(f"Viewer did not receive the home-agent launch denial: {launch_failures!r}")
    path, launch = home_launches.get(timeout=5)
    assert path == f"/homeagent/{agent}/", path
    assert launch["gatekeeper_serveruri"] == home_uri and launch["destination_uuid"] == remote_id, launch
    missing_handle = (65000 << 32) | 65000
    viewer.sendto(struct.pack(">BIB", 0x40, 5, 0) + bytes.fromhex("ffff003f") +
                  agent.bytes + session.bytes + struct.pack("<Q6f", missing_handle, 120, 121, 45, 1, 0, 0),
                  ("127.0.0.1", udp))
    for _ in range(64):
     failure = viewer.recv(4096)
     if failure[6:10] == bytes.fromhex("ffff004a") and b"The region you tried to teleport to was not found" in failure:
      missing_failure_sequence = failure[1:5]
      break
    else:
     raise AssertionError("Viewer did not receive a failure for the missing region")
    with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=12)) as c:
     prior_region = str(uuid.uuid4())
     registration = {"METHOD":"register", "VERSIONMIN":"1", "VERSIONMAX":"1", "uuid":prior_region,
                     "regionName":"Prior Region", "locX":"51200", "locY":"51200", "sizeX":"256", "sizeY":"256",
                     "serverPort":"9000", "serverIP":"127.0.0.1", "serverURI":home_uri,
                     "serverHttpPort":str(home.server_address[1])}
     c.request("POST", "/grid", urllib.parse.urlencode(registration),
               {"Content-Type":"application/x-www-form-urlencoded"})
     response = c.getresponse()
     assert b"<Result>Success</Result>" in response.read()
     c.request("POST", "/grid", urllib.parse.urlencode({"METHOD":"get_region_by_uuid","REGIONID":prior_region}),
               {"Content-Type":"application/x-www-form-urlencoded"})
     registered = c.getresponse().read()
     assert b"<locX>51200</locX>" in registered and b"<locY>51200</locY>" in registered, registered
     viewer.sendto(struct.pack(">BIB", 0x40, 6, 0) + bytes.fromhex("ffff003f") +
                   agent.bytes + session.bytes + struct.pack("<Q6f", (51200 << 32) | 51200, 120, 121, 45, 1, 0, 0),
                   ("127.0.0.1", udp))
     for _ in range(64):
      failure = viewer.recv(4096)
      if failure[6:10] == bytes.fromhex("ffff004a") and failure[1:5] not in (
          missing_failure_sequence, resolved_failure_sequence, home_failure_sequence):
       assert b"Fixture denied teleport" in failure, failure.hex()
       break
     else:
      raise AssertionError("Viewer did not receive the remote access denial")
     path, query = access_queries.get(timeout=5)
     query = json.loads(query)
     assert path == f"/agent/{agent}/{prior_region}/", path
     assert query["agent_home_uri"] == home_uri and query["viaTeleport"] is True, query
     data = urllib.parse.urlencode({"METHOD": "setposition", "UserID": user, "RegionID": prior_region})
     c.request("POST", "/griduser", data, {"Content-Type": "application/x-www-form-urlencoded"})
     response = c.getresponse()
     assert response.status == 200
     response.read()
     if "--hypergrid-release" in sys.argv[2:]:
      accept_home_launch.set()
      camera_update = struct.pack(
          "<13fIB", 130, 131, 47, 0, 1, 0, -1, 0, 0, 0, 0, 1, 64, 0, 0)
      viewer.sendto(struct.pack(">BIBB", 0x40, 11000, 0, 4) +
                    agent.bytes + session.bytes + bytes(24) + b"\0" + camera_update,
                    ("127.0.0.1", udp))
      viewer.sendto(struct.pack(">BIBHH", 0x40, 11001, 0, 0xffff, 88) +
                    agent.bytes + session.bytes + b"\x01", ("127.0.0.1", udp))
      viewer.sendto(struct.pack(">BIB", 0x40, 10, 0) + bytes.fromhex("ffff003f") +
                    agent.bytes + session.bytes + struct.pack("<Q6f", linked_handle, 120, 121, 45, 1, 0, 0),
                    ("127.0.0.1", udp))
      assert hypergrid_queries.get(timeout=5)["region_uuid"] == remote_id
      assert access_queries.get(timeout=5)[0] == f"/agent/{agent}/{remote_id}/"
      assert home_launches.get(timeout=5)[0] == f"/homeagent/{agent}/"
      _, update = outbound_updates.get(timeout=5)
      assert update["center"] == "<130, 131, 47>", update
      assert update["at_axis"] == "<1, 0, 0>", update
      assert update["always_run"] is True, update
      for _ in range(64):
       packet = viewer.recv(4096)
       if packet[6:10] == bytes.fromhex("ffff0045"):
        break
      else:
       raise AssertionError("Viewer did not receive outbound Hypergrid TeleportFinish")
      callback = urlsplit(update["cb_uri"])
      c.request("DELETE", callback.path + "?" + callback.query)
      release = c.getresponse()
      assert release.status == 200 and str(agent).encode() in release.read()
      deadline = time.monotonic() + 5
      while time.monotonic() < deadline:
       c.request("POST", "/CAPS/foreign-fixture0000/", "<llsd><array/></llsd>",
                 {"Content-Type":"application/llsd+xml"})
       seed = c.getresponse()
       seed.read()
       if seed.status == 404:
        break
       time.sleep(0.02)
      else:
       raise AssertionError("Authenticated cross-grid release did not close the source viewer")
      with closing(sqlite3.connect(Path(d) / "presence.db")) as database:
       deadline = time.monotonic() + 5
       while True:
        row = database.execute("SELECT UserID FROM Presence WHERE SessionID = ?",
                               (str(session),)).fetchone()
        status = ET.fromstring(grid_user_status(c, user))
        if row is None and status.findtext("result/Online") == "False":
         assert status.findtext("result/LastRegionID") == linked["uuid"]
         break
        if time.monotonic() >= deadline:
         raise AssertionError("Completed transfer did not clear the source grid's presence and online status")
        time.sleep(0.02)
      assert home_logouts.empty(), "Completed transfer logged out the visitor from its home grid"
     else:
      duplicate = {
          "session_id": str(uuid.uuid4()),
          "circuit_code": str(code + 1),
          "destination_uuid": str(uuid.uuid4()),
      }
      c.request("POST", f"/foreignagent/{agent}/", json.dumps(admission_payload | duplicate),
                {"Content-Type": "application/json"})
      response = c.getresponse()
      assert response.status == 200 and json.loads(response.read())["reason"].startswith("You appear to be already logged in")
      kick_message = kicks.get(timeout=3)
      assert kick_message["to_agent_id"] == str(agent) and kick_message["dialog"] == "+g=="
      assert kick_message["message_key"] == "fixture-grid-key"
      assert b"<Online>False</Online>" in grid_user_status(c, user)
      c.request("POST", "/", xmlrpc.client.dumps((kick_message,), methodname="grid_instant_message"),
                {"Content-Type": "text/xml"})
      response = c.getresponse()
      assert response.status == 200 and xmlrpc.client.loads(response.read())[0][0]["success"] == "TRUE"
      for _ in range(64):
       kicked = viewer.recv(4096)
       if kicked[6:10] == bytes.fromhex("ffff00a3"):
        break
      else:
       raise AssertionError("Connected viewer did not receive KickUser")
      c.request("POST", "/presence", f"METHOD=getagent&SessionID={session}",
                {"Content-Type": "application/x-www-form-urlencoded"})
      response = c.getresponse()
      assert response.status == 200 and b"<result>null</result>" in response.read()
      assert home_logouts.get(timeout=5) == {"userID":str(agent), "sessionID":str(session)}
    print("PASS foreign circuit, home-grid peer verification and RegionHandshake")
  if not deny_client:
   expired_session, recent_session = str(uuid.uuid4()), str(uuid.uuid4())
   with closing(sqlite3.connect(Path(d) / "travel.db")) as database:
    database.executemany(
        "INSERT INTO hg_traveling_data (SessionID, UserID, ServiceToken, TMStamp) "
        "VALUES (?, ?, ?, datetime('now', ?))",
        [(expired_session, str(uuid.uuid4()), "restart-token", "-49 hours"),
         (recent_session, str(uuid.uuid4()), "restart-token", "-47 hours")])
    database.commit()
   if not sys.argv[2:]:
    settings = config.read_text(encoding="utf-8").replace("GatekeeperURI=HTTP://127.0.0.1:8002/Fixture",
                                          "GatekeeperURI=\nExternalName=")
    config.write_text(settings.replace("[GatekeeperService]\n",
                                       "[GatekeeperService]\nExternalName=HTTP://127.0.0.1:8002/Legacy\n", 1), encoding="utf-8")
   with running_server(command) as post:
    port = urlsplit(next(cell.cell_contents for cell in post.__closure__ if isinstance(cell.cell_contents, str))).port
    with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as restarted:
     if not sys.argv[2:]:
      assert restarted.get_uui({"userID":str(local_agent), "targetUserID":str(local_agent)}) == {
          "UUI":f"{local_agent};http://127.0.0.1:8002/legacy/;Access Fixture"}
     for saved_session, expected in ((expired_session, "False"), (recent_session, "True")):
      assert restarted.verify_agent({"sessionID":saved_session, "token":"restart-token"}) == {"result":expected}
    with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=5)) as c:
     data = urllib.parse.urlencode({"METHOD": "get_region_by_uuid", "REGIONID": remote_id})
     c.request("POST", "/grid", data, {"Content-Type": "application/x-www-form-urlencoded"})
     response = c.getresponse()
     stored = response.read()
     assert response.status == 200 and remote_id.encode() in stored and b"<sizeY>512</sizeY>" in stored, stored
     persisted = ET.fromstring(stored).find("result")
     assert persisted.findtext("locX") == str(local_x)
     assert persisted.findtext("flags") == "524"
     assert persisted.findtext("regionSecret") == "10995116282880"
     assert persisted.findtext("serverURI") == home_uri
     assert persisted.findtext("serverPort") == "0"
     assert persisted.findtext("regionMapTexture") == "00000000-0000-1111-9999-000000000013"
     c.request("POST", "/grid", remote_search, {"Content-Type": "application/x-www-form-urlencoded"})
     response = c.getresponse()
     assert response.status == 200 and remote_id.encode() in response.read()
   assert remote_calls == [
       ("link", {"region_name": "Remote Region"}),
       ("link", {"region_name": "Malformed Link"}),
   ], remote_calls
 home.shutdown()

