"""Exercise connected login over HTTP, including durable inventory and rollback."""
import argparse
import contextlib
import gzip
import http.client
import http.server
import json
from pathlib import Path
import queue
import re
import sqlite3
import subprocess
import tempfile
import threading
import urllib.parse
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    executable = str(Path(args.native).resolve())
    args.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(condition, name):
        if not condition:
            raise AssertionError(name)
        checks.append(name)

    with tempfile.TemporaryDirectory(prefix="connected-login-", dir=args.work) as temporary:
        root = Path(temporary)
        config = root / "server.ini"
        settings = """[NativeServer]
ListenAddress=127.0.0.1
EnableLoginOrchestration=true
EnableGridEndpoint=true
EnableGridUserEndpoint=true
EnablePresenceEndpoint=true
EnableAccountEndpoint=true
EnableAvatarEndpoint=true
EnableFriendsEndpoint=true
FriendsDatabase=friends.db
AvatarDatabase=avatars.db
GridRegionProvider=SQLite
GridRegionDatabase=regions.db
GridUserDatabase=users.db
InventoryDatabase=inventory.db
UserAccountDatabase=accounts.db
AuthenticationDatabase=accounts.db
[Network]
http_listener_port=0
[LoginService]
RequireInventory=true
[UserAccountService]
AllowCreateUser=true
CreateDefaultAvatarEntries=true
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[AvatarService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=avatar
HttpAuthPassword=service-password
[Messaging]
MessageKey=connected-message-key
[FriendsService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=avatar
HttpAuthPassword=service-password
"""
        config.write_text(settings, encoding="utf-8")

        def cli(*options, password=None, expected=0):
            result = subprocess.run([executable, "--config", str(config), *options], input=password,
                                    capture_output=True, text=True, encoding="utf-8", timeout=20)
            check(result.returncode == expected, f"CLI {options}: {result.stderr if result.returncode != expected else 'exit matches'}")
            return result

        cli("--init-db")
        cli("--create-user", "Connected", "User", password="connected-password\n")
        with contextlib.closing(sqlite3.connect(root/"accounts.db")) as db:
            check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("AuthStore",2),("UserAccount",3),("migrations",1)],"account initialization preserves original v3 migration metadata independently of other services")

        with contextlib.closing(sqlite3.connect(root/"users.db")) as db:
            check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("GridUserStore",1),("migrations",1)],"grid-user initialization executes original legacy script and stamps versions")
            check(next(row for row in db.execute("PRAGMA table_info(GridUser)") if row[1]=="UserID")[3]==0,"grid-user initialization preserves original SQLite nullable primary-key schema")

        with contextlib.closing(sqlite3.connect(root / "accounts.db")) as db:
            principal = db.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Connected'").fetchone()[0]
        check((root / "inventory.db").exists(), "dedicated inventory database initialized")
        legacy_owner,legacy_friend=str(uuid.uuid4()),str(uuid.uuid4())
        with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:
            db.executescript("DROP TABLE IF EXISTS Friends;DROP TABLE IF EXISTS migrations;CREATE TABLE userfriends(ownerID,friendID,friendPerms);")
            db.executemany("INSERT INTO userfriends VALUES(?,?,?)",[(legacy_owner,legacy_friend,5),(legacy_friend,legacy_owner,9)]);db.commit()

        process = subprocess.Popen([executable, "--config", str(config), "--console"], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        output = queue.Queue()
        def read_output():
            for line in process.stdout:
                output.put(line.strip())
        thread = threading.Thread(target=read_output, daemon=True)
        thread.start()
        try:
            while True:
                line = output.get(timeout=20)
                if line.startswith("LISTENING "):
                    port = urllib.parse.urlsplit(line.split(" ", 1)[1]).port
                    break

            def request(path, body=None, content_type="application/x-www-form-urlencoded", headers=None):
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                try:
                    connection.request("GET" if body is None else "POST", path, body=body,
                                       headers=({} if body is None else {"Content-Type": content_type}) | (headers or {}))
                    response = connection.getresponse()
                    return response.status, response.read()
                finally:
                    connection.close()

            def service(path, **fields):
                status, data = request(path, urllib.parse.urlencode(fields, doseq=True), headers={"Authorization":"Basic YXZhdGFyOnNlcnZpY2UtcGFzc3dvcmQ="} if path in ("/avatar","/friends") else None)
                check(status == 200, f"{path} HTTP status")
                return ET.fromstring(data)

            def login(start="home", password="connected-password"):
                data = xmlrpc.client.dumps(({"first": "Connected", "last": "User", "passwd": password, "start": start},), methodname="login_to_simulator")
                status, body = request("/", data.encode(), "text/xml")
                check(status == 200, "XML-RPC login HTTP status")
                return xmlrpc.client.loads(body)[0][0]

            with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:
                check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("FriendsStore",2),("migrations",1)],"friends startup executes legacy migration and records original versions")
                check(db.execute("SELECT Flags,Offered FROM Friends WHERE PrincipalID=? AND Friend=?",(legacy_owner,legacy_friend)).fetchone()==("5","0"),"friends startup imports original userfriends permissions and Offered default")
                check(db.execute("SELECT count(*) FROM userfriends").fetchone()[0]==2,"friends migration preserves original legacy table")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
                check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("Avatar",1),("migrations",1)],"avatar startup migrates independently of friends and records original version")
            with contextlib.closing(sqlite3.connect(root/"users.db")) as db:
                check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("GridUserStore",1),("migrations",1)],"grid-user startup preserves independent migration versions")
            legacy=service("/friends",METHOD="getfriends",PRINCIPALID=legacy_owner)
            check(len(legacy)==1 and legacy.findtext("friend0/Friend")==legacy_friend and legacy.findtext("friend0/MyFlags")=="5" and legacy.findtext("friend0/TheirFlags")=="9","HTTP friends retrieval serves migrated directional permissions")
            response = login()
            check(response["message"].startswith("Error connecting to the desired location"), "inventory exists and empty grid fails at destination selection")
            region = str(uuid.uuid4())
            registered = service("/grid", METHOD="register", VERSIONMIN="1", VERSIONMAX="1", uuid=region,
                                 regionName="Connected Region", locX="25600", locY="25600", sizeX="256", sizeY="256", serverPort="9000", serverIP="127.0.0.1")
            check(registered.findtext("Result") == "Success", "register default destination")
            avatar_id=str(uuid.uuid4())
            created=service("/accounts",METHOD="createuser",PrincipalID=avatar_id,FirstName="Default",LastName="Avatar",Password="avatar-password")
            check(created.findtext("result/PrincipalID") == avatar_id,"HTTP creation with default avatar entries succeeds")
            wearable_source=(Path(__file__).resolve().parents[2]/"OpenSim/Framework/AvatarWearable.cs").read_text(encoding="utf-8-sig")
            names=[("Eyes","EYES",3,13),("Shape","BODY",0,13),("Skin","SKIN",1,13),("Hair","HAIR",2,13),("Shirt","SHIRT",4,5),("Pants","PANTS",5,5)]
            with contextlib.closing(sqlite3.connect(root/"inventory.db")) as db:
                db.row_factory=sqlite3.Row
                folders={row["type"]:row["folderID"] for row in db.execute("SELECT type,folderID FROM inventoryfolders WHERE agentID=?",(avatar_id,))}
                items=db.execute("SELECT * FROM inventoryitems WHERE avatarID=?",(avatar_id,)).fetchall()
                check(len(items)==12 and len({r["inventoryID"] for r in items})==12,"default avatar persists six wearables and six outfit links")
                for name,constant,flag,type_ in names:
                    wearable=next(r for r in items if r["inventoryName"]=="Default "+name and r["assetType"]==type_)
                    asset=re.search(r'DEFAULT_'+constant+r'_ASSET = new UUID\("([^\"]+)"\)',wearable_source).group(1)
                    check(wearable["assetID"]==asset and wearable["flags"]==flag and wearable["parentFolderID"]==folders[type_],"default "+name+" asset, type and folder match source")
                    permissions=["inventoryNextPermissions","inventoryCurrentPermissions","inventoryBasePermissions","inventoryEveryOnePermissions","inventoryGroupPermissions"]
                    check(all(wearable[p]==0x8e000 for p in permissions) and wearable["creatorID"]==avatar_id and wearable["invType"]==18,"default "+name+" permissions and ownership match source")
                    link=next(r for r in items if r["assetType"]==24 and r["assetID"]==wearable["inventoryID"])
                    check(link["parentFolderID"]==folders[46] and link["flags"]==flag and link["inventoryName"]==wearable["inventoryName"] and all(link[p]==32768 for p in permissions),"default "+name+" Current Outfit link matches source")
                check(all(uuid.UUID(r["inventoryID"]).version==4 and r["creationDate"]>1700000000 for r in items),"default inventory items have generated UUIDs and creation timestamps")
            fallback_id=str(uuid.uuid4())
            check(service("/accounts",METHOD="createuser",PrincipalID=fallback_id,FirstName="Fallback",LastName="Avatar",Password="password",Model="Missing Model").findtext("result/PrincipalID")==fallback_id,"missing appearance model falls back to default items")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
                template=dict(db.execute("SELECT Name,Value FROM Avatars WHERE PrincipalID=?",(avatar_id,)))
                check(template["AvatarType"]=="1" and "Wearable 0:0" in template and len(template["VisualParams"])>255,"default appearance persists source-shaped avatar metadata and long visual parameters")
            avatar_auth={"Authorization":"Basic YXZhdGFyOnNlcnZpY2UtcGFzc3dvcmQ="}
            check(request("/avatar")[0]==405,"avatar endpoint only accepts POST")
            check(request("/avatar","METHOD=getavatar&UserID="+avatar_id)[0]==401,"avatar endpoint enforces configured service authentication")
            check(request("/avatar","METHOD=getavatar&UserID="+avatar_id,headers=avatar_auth|{"X-SecondLife-Shard":"fixture"})[0]==403,"avatar endpoint rejects script-origin requests before credentials")
            remote=service("/avatar",METHOD="getavatar",UserID=avatar_id)
            check(remote.findtext("result/Wearable_x0020_0_x003A_0")==template["Wearable 0:0"],"HTTP avatar response encodes original wearable field names")
            remote_id=str(uuid.uuid4())
            check(service("/avatar",METHOD="getavatar",UserID=remote_id).findtext("result/AvatarType")=="1","missing remote avatar returns source default type")
            saved=service("/avatar",METHOD="setavatar",UserID=remote_id,AvatarType="1",AvatarHeight="bad",VERSIONMIN="1",VERSIONMAX="1",**{"Wearable 0:0":"opaque:value","_x0041_":"literal","試験":"🦊"})
            check(saved.findtext("result")=="Success","remote avatar replacement persists arbitrary fields")
            remote=service("/avatar",METHOD="getavatar",UserID=remote_id)
            check(remote.findtext("result/AvatarHeight")=="1.771488" and remote.findtext("result/_x005F_x0041_")=="literal" and remote.findtext("result/試験")=="🦊","HTTP avatar response preserves height repair, literal escape keys and Unicode")
            check(all(remote.find("result/"+key) is None for key in ["METHOD","UserID","VERSIONMIN","VERSIONMAX"]),"avatar request metadata is excluded from stored fields")
            check(service("/avatar",METHOD="setitems",UserID=remote_id,**{"Names[]":["z","a","z"],"Values[]":["first","second","last"]}).findtext("result")=="Success","remote avatar item batch preserves duplicate update ordering")
            check(service("/avatar",METHOD="setitems",UserID=remote_id,**{"Names[]":["should-not-write","extra"],"Values[]":["one"]}).findtext("result")=="Failure","remote avatar rejects mismatched item arrays")
            check(service("/avatar",METHOD="removeitems",UserID=remote_id,**{"Names[]":["a","missing"]}).findtext("result")=="Success","remote avatar removal ignores missing fields")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
                persisted=dict(db.execute("SELECT Name,Value FROM Avatars WHERE PrincipalID=?",(remote_id,)))
                check(persisted["z"]=="last" and "a" not in persisted and "should-not-write" not in persisted,"remote avatar batch results are committed to SQLite")
            check(service("/avatar",METHOD="resetavatar",UserID=remote_id).findtext("result")=="Success","remote avatar reset deletes persisted appearance")
            check(service("/avatar",METHOD="resetavatar",UserID=remote_id).findtext("result")=="Failure","repeated remote avatar reset preserves SQLite false result")
            check(service("/avatar",METHOD="getavatar",UserID=remote_id).findtext("result/AvatarType")=="1","remote avatar reset restores source default data")
            check(service("/avatar",METHOD="setitems",UserID=principal,**{"Names[]":["AvatarType"],"Values[]":["bad"]}).findtext("result")=="Success","remote avatar update can reproduce original malformed metadata")
            corrupt_login=login()
            check(corrupt_login["reason"]=="Internal Error" and corrupt_login["message"]=="Error generating Login Response","login retrieves avatar after destination selection and preserves conversion failure")
            check(service("/presence",METHOD="getagents",**{"uuids[]":principal}).findtext("result")=="null","avatar conversion failure cleans up login presence")
            check(service("/avatar",METHOD="resetavatar",UserID=principal).findtext("result")=="Success","remote reset repairs malformed login appearance")
            repaired_login=login()
            check(repaired_login["reason"]=="key" and repaired_login["message"]=="","repaired avatar resumes source missing-connector failure without successful admission")
            copied_id=str(uuid.uuid4())
            check(service("/accounts",METHOD="createuser",PrincipalID=copied_id,FirstName="Copied",LastName="Avatar",Password="password",Model="Default Avatar").findtext("result/PrincipalID")==copied_id,"HTTP provisioning loads an existing model from the avatar service")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
                copied_avatar=dict(db.execute("SELECT Name,Value FROM Avatars WHERE PrincipalID=?",(copied_id,)))
                check(dict(db.execute("SELECT Name,Value FROM Avatars WHERE PrincipalID=?",(avatar_id,)))==template,"model appearance remains unchanged by HTTP copy")
            with contextlib.closing(sqlite3.connect(root/"inventory.db")) as db:
                rows=db.execute("SELECT inventoryID,assetID,avatarID FROM inventoryitems WHERE avatarID=?",(copied_id,)).fetchall();copied_items={r[0]:r for r in rows}
                check(len(rows)==12,"HTTP model copy persists six wearables and six outfit links")
                for i in range(6):
                    item,asset=copied_avatar[f"Wearable {i}:0"].split(':');source_item,source_asset=template[f"Wearable {i}:0"].split(':')
                    check(item!=source_item and asset==source_asset and copied_items[item][1:]==(asset,copied_id),"persisted copied wearable references destination inventory")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db,db:
                db.execute("INSERT INTO Avatars(PrincipalID,Name,Value) VALUES (?,?,?)",(principal,"AvatarType","bad"))
            failed_id=str(uuid.uuid4())
            check(service("/accounts",METHOD="createuser",PrincipalID=failed_id,FirstName="Model",LastName="Failure",Password="password",Model="Connected User").findtext("result")=="Failure","malformed model avatar metadata preserves original failure")
            with contextlib.closing(sqlite3.connect(root/"inventory.db")) as db:
                check(db.execute("SELECT count(*) FROM inventoryitems WHERE avatarID=?",(fallback_id,)).fetchone()[0]==12,"fallback defaults persist in SQLite")
                check(db.execute("SELECT count(*) FROM inventoryfolders WHERE agentID=?",(failed_id,)).fetchone()[0]>0 and db.execute("SELECT count(*) FROM inventoryitems WHERE avatarID=?",(failed_id,)).fetchone()[0]==0,"model failure preserves prior inventory setup without creating fake appearance items")
            with contextlib.closing(sqlite3.connect(root/"accounts.db")) as db:
                check(db.execute("SELECT count(*) FROM UserAccounts u JOIN auth a ON u.PrincipalID=a.UUID WHERE u.PrincipalID=?",(failed_id,)).fetchone()[0]==1,"appearance failure preserves preceding account/password writes")
            with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db,db:db.execute("DELETE FROM Avatars WHERE PrincipalID=?",(principal,))
            response = login()
            check(response["login"] == "false" and response["reason"] == "key" and response["message"] == "", "prepared login reaches original no-simulation-connector failure")
            response = login("invalid-start")
            check(response["reason"] == "Internal Error", "malformed destination returns original internal error")
            check(login(password="wrong")["message"].startswith("Could not authenticate"), "bad credentials stop before service preparation")
            state = service("/presence", METHOD="getagents", **{"uuids[]": principal})
            check(state.findtext("result") == "null" and state.find("presence0") is None, "failed admissions leave no active presence")
            check(request("/ready")[0] == 503, "simulator readiness remains unavailable")
            messages = queue.Queue()
            class KickReceiver(http.server.BaseHTTPRequestHandler):
                reply = "FALSE"
                def log_message(self, *args):
                    pass
                def do_POST(self):
                    raw = self.rfile.read(int(self.headers["Content-Length"]))
                    messages.put((self.path, xmlrpc.client.loads(raw)))
                    if self.reply == "fault":
                        data = xmlrpc.client.dumps(xmlrpc.client.Fault(42, "fixture fault"))
                    elif self.reply == "malformed":
                        data = "not-xml"
                    else:
                        data = xmlrpc.client.dumps(({"success": self.reply},), methodresponse=True)
                    body = data.encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/xml")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
            with http.server.ThreadingHTTPServer(("127.0.0.1", 0), KickReceiver) as receiver:
                worker = threading.Thread(target=receiver.serve_forever, daemon=True)
                worker.start()
                try:
                    service("/grid", METHOD="register", VERSIONMIN="1", VERSIONMAX="1", uuid=region,
                            regionName="Connected Region", locX="25600", locY="25600", sizeX="256", sizeY="256", serverPort="9000", serverIP="127.0.0.1",
                            serverURI=f"http://127.0.0.1:{receiver.server_port}/kick?attempt=1")
                    service("/griduser", METHOD="setposition", UserID=principal, RegionID=region, Position="<1,2,3>", LookAt="<0,1,0>")
                    for reply in ("FALSE", "fault", "malformed", "TRUE"):
                        KickReceiver.reply = reply
                        service("/griduser", METHOD="loggedin", UserID=principal)
                        response = login()
                        check(response["reason"] == "presence", f"duplicate login rejected after IM reply {reply}")
                        path, (parameters, method) = messages.get(timeout=5)
                        message = parameters[0]
                        check(path == "/kick?attempt=1/" and method == "grid_instant_message", "kick uses source-normalized region URL and XML-RPC method")
                        expected = {"from_agent_id": "6571e388-6218-4574-87db-f9379718315e", "from_agent_session": str(uuid.UUID(int=0)),
                                    "to_agent_id": principal, "im_session_id": str(uuid.UUID(int=0)), "from_agent_name": "GRID", "message": "New login detected",
                                    "from_group": "FALSE", "parent_estate_id": "0", "position_x": "0", "position_y": "0", "position_z": "0",
                                    "region_id": str(uuid.UUID(int=0)), "binary_bucket": "AA==", "dialog": "+g==", "offline": "AA==", "region_handle": 0,
                                    "message_key": "connected-message-key"}
                        check({key: value for key, value in message.items() if key != "timestamp"} == expected and int(message["timestamp"]) > 0, "duplicate kick preserves original IM wire fields and MessageKey")
                        check(service("/griduser", METHOD="getgriduserinfo", UserID=principal).findtext("result/Online") == "False", "stale online state cleared even when IM delivery fails")
                finally:
                    receiver.shutdown()
                    worker.join(timeout=5)
            service("/griduser", METHOD="loggedin", UserID=principal)
            check(login()["reason"] == "presence", "connection refusal still rejects duplicate login with original response")
            check(service("/griduser", METHOD="getgriduserinfo", UserID=principal).findtext("result/Online") == "False", "connection refusal still clears stale online state")
        finally:
            if process.poll() is None:
                process.stdin.write("quit\n")
                process.stdin.flush()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            thread.join(timeout=5)
        check(process.returncode == 0, "connected server shuts down cleanly")
        with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
            check(dict(db.execute("SELECT Name,Value FROM Avatars WHERE PrincipalID=?",(copied_id,)))==copied_avatar,"copied avatar metadata survives server shutdown and database reopen")
        with contextlib.closing(sqlite3.connect(root/"inventory.db")) as db:
            check(db.execute("SELECT count(*) FROM inventoryitems WHERE avatarID=?",(avatar_id,)).fetchone()[0]==12,"default avatar items survive server shutdown")
        with contextlib.closing(sqlite3.connect(root / "users.db")) as db:
            check(db.execute("SELECT count(*) FROM GridUser WHERE Online='True'").fetchone()[0] == 0, "failed admission never persists an online user")
        with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:
            db.execute("UPDATE userfriends SET friendPerms=77");db.commit()
        friend_id=str(uuid.uuid4());foreign_id=str(uuid.uuid4());pending_id=str(uuid.uuid4());foreign_uui=foreign_id+";https://foreign.example/;Foreign Friend"
        with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:
            db.executemany("INSERT INTO Friends (PrincipalID,Friend,Flags,Offered) VALUES (?,?,?,?)",[(principal,friend_id,"3","123"),(friend_id,principal,"5","0"),(principal,foreign_uui,"7","0"),(foreign_uui,principal,"1","0"),(principal,pending_id,"1","0"),(principal,"bad-friend","7","0"),("bad-friend",principal,"7","0")]);db.commit()
        expected_buddies={friend_id:(5,3),foreign_id:(1,7)}
        # Restart the actual server with the remote simulation connector, retaining
        # the same SQLite accounts, inventory, grid-user and region services.
        class Simulator(http.server.BaseHTTPRequestHandler):
            requests=[]
            def log_message(self,*args):pass
            def receive(self):
                body=self.rfile.read(int(self.headers["Content-Length"]))
                if self.headers.get("X-Content-Encoding")=="gzip":body=gzip.decompress(body)
                self.requests.append((self.command,self.path,json.loads(body)))
                data=json.dumps({"success":True,"reason":"","negotiated_inbound_version":0.4,"negotiated_outbound_version":0.4,"features":[]}).encode()
                self.send_response(200);self.send_header("Content-Type","application/json");self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data)
            do_QUERYACCESS=receive
            do_POST=receive
        with http.server.ThreadingHTTPServer(("127.0.0.1",0),Simulator) as simulator:
            simulation_thread=threading.Thread(target=simulator.serve_forever,daemon=True);simulation_thread.start()
            library=(Path(__file__).resolve().parents[2]/"bin/inventory/Libraries.xml").as_posix()
            remote_settings=settings.replace("EnableLoginOrchestration=true","EnableRemoteSimulationConnector=true").replace("RequireInventory=true","RequireInventory=true\nWelcomeMessage=Hello <USERNAME>\nDSTZone=none")+"\n[LibraryService]\nDefaultLibrary="+library+"\n"
            config.write_text(remote_settings,encoding="utf-8")
            process=subprocess.Popen([executable,"--config",str(config),"--console"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8")
            output=queue.Queue();thread=threading.Thread(target=read_output,daemon=True);thread.start()
            try:
                while True:
                    line=output.get(timeout=20)
                    if line.startswith("LISTENING "):port=urllib.parse.urlsplit(line.split(" ",1)[1]).port;break
                with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:
                    check(db.execute("SELECT Flags FROM Friends WHERE PrincipalID=? AND Friend=?",(legacy_owner,legacy_friend)).fetchone()==("5",),"friends restart does not reimport already migrated legacy permissions")
                    check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("FriendsStore",2),("migrations",1)],"friends restart preserves version stamps without duplicate rows")
                with contextlib.closing(sqlite3.connect(root/"avatars.db")) as db:
                    check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("Avatar",1),("migrations",1)],"avatar restart preserves independent migration stamps without duplicate rows")
                with contextlib.closing(sqlite3.connect(root/"users.db")) as db:
                    check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("GridUserStore",1),("migrations",1)],"grid-user restart preserves metadata without duplicate migration rows")
                with contextlib.closing(sqlite3.connect(root/"accounts.db")) as db:
                    check(db.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("AuthStore",2),("UserAccount",3),("migrations",1)],"account restart preserves original migration versions without duplicate rows")
                service("/grid",METHOD="register",VERSIONMIN="1",VERSIONMAX="1",uuid=region,regionName="Connected Region",locX="25600",locY="25600",sizeX="256",sizeY="256",serverPort="9000",serverIP="127.0.0.1",serverURI=f"http://127.0.0.1:{simulator.server_port}/")
                success=login()
                check(success.get("login")=="true","actual native server completes successful XML-RPC login through remote simulation connector")
                check(success["agent_id"]==principal and success["message"]=="Hello Connected User" and success["sim_ip"]=="127.0.0.1" and success["sim_port"]==9000,"successful HTTP response preserves identity, configured welcome and simulator endpoint")
                check({b["buddy_id"]:(b["buddy_rights_has"],b["buddy_rights_given"]) for b in success["buddy-list"]}==expected_buddies,"successful XML-RPC login loads persisted directional friend rights, normalizes foreign UUI and filters pending/invalid entries")
                check(success["seed_capability"].startswith(f"http://127.0.0.1:{simulator.server_port}/CAPS/") and success["seed_capability"].endswith("0000/"),"successful HTTP response uses admitted circuit capability path")
                check(len(success["inventory-skeleton"])>0 and len(success["inventory-skel-lib"])==19,"successful HTTP login includes persisted inventory and complete bundled library skeleton")
                check([r[0] for r in Simulator.requests]==["QUERYACCESS","POST"],"HTTP login queries access before create-agent")
                circuit=Simulator.requests[1][2]
                check(circuit["agent_id"]==principal and circuit["session_id"]==success["session_id"] and int(circuit["circuit_code"])==success["circuit_code"],"admitted HTTP circuit identity matches viewer response")
                check(service("/griduser",METHOD="getgriduserinfo",UserID=principal).findtext("result/Online")=="True","successful admission persists online grid-user state")
                check(request("/friends")[0]==405,"friends endpoint accepts POST only")
                check(request("/friends","METHOD=getfriends&PRINCIPALID="+principal)[0]==401,"friends endpoint enforces its configured service authentication")
                check(request("/friends","METHOD=getfriends&PRINCIPALID="+principal,headers={"Authorization":"Basic YXZhdGFyOnNlcnZpY2UtcGFzc3dvcmQ=","X-SecondLife-Shard":"fixture"})[0]==403,"friends endpoint rejects script-origin requests")
                friend_response=service("/friends",METHOD="getfriends",PRINCIPALID=principal)
                check(len(friend_response)==4 and {f.findtext("Friend") for f in friend_response}=={friend_id,foreign_uui,pending_id,"bad-friend"},"friends HTTP service returns all stored records before login-specific filtering")
                check(service("/friends",METHOD="getfriends_string",PRINCIPALID=principal).findtext("result")=="null","friends HTTP string lookup preserves original empty-result behavior")
                extra=str(uuid.uuid4())
                check(service("/friends",METHOD="storefriend",PrincipalID=principal,Friend=extra,MyFlags="invalid").findtext("Result")=="Success","friends HTTP store accepts source CamelCase fields and defaults invalid flags to zero")
                with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:check(db.execute("SELECT Flags,Offered FROM Friends WHERE PrincipalID=? AND Friend=?",(principal,extra)).fetchone()==("0","0"),"friends HTTP store persists flags and original schema default metadata")
                check(service("/friends",METHOD="deletefriend",PRINCIPALID=principal,FRIEND=extra).findtext("Result")=="Success","friends HTTP UUID delete removes the relationship")
                check(service("/friends",METHOD="storefriend",PrincipalID="foreign-owner",Friend="foreign-friend",MyFlags="7").findtext("Result")=="Success","friends HTTP store supports string principals")
                check(service("/friends",METHOD="deletefriend_string",PRINCIPALID="foreign-owner",FRIEND="foreign-friend").findtext("Result")=="Success","friends HTTP string delete preserves string identity")
                failure=service("/friends",METHOD="unknown");check(failure.findtext("Result")=="Failure" and failure.find("Message") is not None,"friends HTTP failure preserves uppercase result and empty Message element")

                check(request("/ready")[0]==503,"remote login does not claim a native simulator scene is ready")
                service("/presence",METHOD="logout",SessionID=success["session_id"])
                service("/griduser",METHOD="loggedout",UserID=principal,RegionID=region,Position="<128,128,30>",LookAt="<0,1,0>")
                fields={"first":"Connected","last":"User","passwd":"connected-password","start":"home","version":"Native Viewer","channel":"Test"}
                llsd_request=ET.Element("llsd");llsd_map=ET.SubElement(llsd_request,"map")
                for key,value in fields.items():ET.SubElement(llsd_map,"key").text=key;ET.SubElement(llsd_map,"string").text=value
                status,body=request("/",ET.tostring(llsd_request),"application/llsd+xml");llsd_root=ET.fromstring(body).find("map");nodes=list(llsd_root);values={nodes[i].text:nodes[i+1] for i in range(0,len(nodes),2)}
                check(status==200 and values["login"].text=="true" and values["agent_id"].tag=="uuid" and values["agent_id"].text==principal,"actual HTTP LLSD handler produces typed successful viewer response")
                check("http_port" not in values and values["message"].text=="Hello Connected User" and len(values["inventory-skel-lib"])==19,"HTTP LLSD success preserves protocol-specific fields and library skeleton")
                buddy_maps=[]
                for buddy in values["buddy-list"]:
                    entries=list(buddy);buddy_maps.append({entries[i].text:entries[i+1].text for i in range(0,len(entries),2)})
                check({b["buddy_id"]:(int(b["buddy_rights_has"]),int(b["buddy_rights_given"])) for b in buddy_maps}==expected_buddies,"successful HTTP LLSD login loads the same persisted directional friend list")
                # A second native login process uses the first process's /friends
                # endpoint, rather than opening its own friends database.
                service("/presence",METHOD="logout",SessionID=values["session_id"].text)
                service("/griduser",METHOD="loggedout",UserID=principal,RegionID=region,Position="<128,128,30>",LookAt="<0,1,0>")
                backend_port=port;proxy_config=root/"remote-friends.ini"
                proxy_settings=remote_settings.replace("EnableFriendsEndpoint=true","EnableFriendsEndpoint=true\nFriendsProvider=Remote").replace("FriendsDatabase=friends.db","FriendsDatabase=remote-must-not-open.db").replace("[FriendsService]","[FriendsService]\nFriendsServerURI=http://127.0.0.1:"+str(backend_port))
                proxy_config.write_text(proxy_settings,encoding="utf-8")
                proxy=subprocess.Popen([executable,"--config",str(proxy_config),"--console"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8");proxy_output=queue.Queue()
                def read_proxy():
                    for line in proxy.stdout:proxy_output.put(line.strip())
                proxy_thread=threading.Thread(target=read_proxy,daemon=True);proxy_thread.start()
                try:
                    while True:
                        line=proxy_output.get(timeout=20)
                        if line.startswith("LISTENING "):port=urllib.parse.urlsplit(line.split(" ",1)[1]).port;break
                    proxy_success=login()
                    check(proxy_success.get("login")=="true" and {b["buddy_id"]:(b["buddy_rights_has"],b["buddy_rights_given"]) for b in proxy_success["buddy-list"]}==expected_buddies,"native login retrieves friend rights over authenticated HTTP from another native server")
                    check(not (root/"remote-must-not-open.db").exists(),"remote friends provider does not open a local friends database")
                    check(len(service("/friends",METHOD="getfriends",PRINCIPALID=principal))==4,"remote-provider friends endpoint forwards UUID retrieval to the native backend")
                finally:
                    port=backend_port
                    if proxy.poll() is None:proxy.stdin.write("quit\n");proxy.stdin.flush()
                    try:proxy.wait(timeout=15)
                    except subprocess.TimeoutExpired:proxy.kill();proxy.wait(timeout=5)
                    proxy_thread.join(timeout=5)
                check(proxy.returncode==0,"remote-friends native login process shuts down cleanly")



            finally:
                if process.poll() is None:process.stdin.write("quit\n");process.stdin.flush()
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
                thread.join(timeout=5);simulator.shutdown();simulation_thread.join(timeout=5)
            check(process.returncode==0,"remote simulation login server shuts down cleanly")
            with contextlib.closing(sqlite3.connect(root/"friends.db")) as db:check(db.execute("SELECT Flags,Offered FROM Friends WHERE PrincipalID=? AND Friend=?",(principal,friend_id)).fetchone()==("3","123"),"login retrieval preserves friends data and Offered metadata across server shutdown")

        # Preflight must reject an existing inventory destination before creating account files.
        config.write_text(settings.replace("accounts.db", "untouched.db"), encoding="utf-8")
        cli("--init-db", expected=1)
        check(not (root / "untouched.db").exists(), "service preflight occurs before account initialization")
        config.write_text("[NativeServer]\nEnableFriendsService=true\nFriendsDatabase=friends.db\nUserAccountDatabase=friends-preflight.db\nAuthenticationDatabase=friends-preflight.db\n",encoding="utf-8")
        cli("--init-db",expected=1)
        check(not (root/"friends-preflight.db").exists(),"friends initialization preflight rejects existing storage before creating account databases")

    (args.work / "login-orchestration-network-results.json").write_text(json.dumps({"passed": True, "checks": checks}, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(checks)} connected login HTTP/persistence observations")


if __name__ == "__main__":
    main()
