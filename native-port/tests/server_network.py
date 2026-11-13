"""Exercise the native executable through actual sockets and persistent databases."""
import argparse
import base64
import time
import concurrent.futures
import contextlib
import gzip
import hashlib
import http.client
import json
from pathlib import Path
import queue
import re
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import threading
import urllib.parse
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client
import zlib


def with_settings(text, section, **settings):
    """Update one section without relying on duplicate INI keys/sections."""
    lines = text.splitlines(keepends=True)
    headers = [i for i, line in enumerate(lines) if line.strip() == "[" + section + "]"]
    if len(headers) > 1:
        raise ValueError("Fixture contains duplicate section " + section)
    if not headers:
        return text + ("" if text.endswith("\n") else "\n") + "[" + section + "]\n" + "".join(key + " = " + str(value) + "\n" for key, value in settings.items())
    start = headers[0] + 1
    end = next((i for i in range(start, len(lines)) if lines[i].lstrip().startswith("[")), len(lines))
    pending = dict(settings)
    for i in range(start, end):
        key, equals, _ = lines[i].partition("=")
        if equals and key.strip() in pending:
            name = key.strip()
            lines[i] = name + " = " + str(pending.pop(name)) + "\n"
    if pending:
        if end and not lines[end - 1].endswith("\n"):
            lines[end - 1] += "\n"
        lines[end:end] = [key + " = " + str(value) + "\n" for key, value in pending.items()]
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--http-only", action="store_true", help="Select plaintext coverage; the normal suite still verifies TLS")
    args = parser.parse_args()
    executable = str(Path(args.native).resolve())
    args.work.mkdir(parents=True, exist_ok=True)
    features = json.loads(subprocess.check_output([executable, "--features"], text=True))
    checks = []
    skipped = []

    def check(condition, name):
        if not condition:
            raise AssertionError(name)
        checks.append(name)

    with tempfile.TemporaryDirectory(prefix="native-network-", dir=args.work) as temporary:
        root = Path(temporary) / "config-試験"
        root.mkdir()
        config = root / "server.ini"
        database = root / "native.db"
        base_config = """[Startup]
ServiceConnectors=Hypergrid@0/OpenSim.Server.Handlers.dll:HeloServiceInConnector,0/OpenSim.Server.Handlers.dll:MuteListServiceConnector
[MuteListService]
LocalServiceModule = OpenSim.Services.MuteListService.dll:MuteListService
AuthType = BasicHttpAuthentication
HttpAuthUsername = mutes
HttpAuthPassword = fixture-mute-password
[MuteListStore]
StorageProvider = OpenSim.Data.SQLite.dll
ConnectionString = Data Source=mutes.db
[NativeServer]
ListenAddress = 127.0.0.1
UserAccountDatabase = native.db
AuthenticationDatabase = native.db
MaximumRequestBytes = 8192
HttpTimeoutSeconds = 2
EnablePresenceEndpoint = true
EnableGridEndpoint = true
EnableGatekeeperDiscovery = true
EnableAccountEndpoint = true
EnableGridUserEndpoint = true
EnableBakedTextureEndpoint = true
EnableAgentPreferencesEndpoint = true
AgentPreferencesDatabase = preferences.db
GridUserDatabase = grid-user.db
GridRegionProvider = SQLite
GridRegionDatabase = regions.db
[Network]
http_listener_port = 0
[BakedTextureService]
BaseDirectory = bakes
AuthType = BasicHttpAuthentication
HttpAuthUsername = bakes
HttpAuthPassword = fixture-bakes-password
[AgentPreferencesService]
AuthType = BasicHttpAuthentication
HttpAuthUsername = bakes
HttpAuthPassword = fixture-bakes-password
[LoginService]
RequireInventory = false
AllowRemoteSetLoginLevel = true
[GridService]
DefaultRegionFlags = +DefaultRegion +FallbackRegion
[UserAccountService]
AllowSetAccount = true
AllowCreateUser = true
[GridInfoService]
gridname = Native & Test <Grid>
gridnick = native-test
[GatekeeperService]
ExternalName = http://127.0.0.1:9000
GatekeeperURIAlias = http://ALIAS.EXAMPLE/,bad/path
"""
        config.write_text(base_config, encoding="utf-8")

        def cli(*options, password=None, expected=0):
            result = subprocess.run([executable, "--config", str(config), *options],
                                    input=password, capture_output=True, text=True, encoding="utf-8", timeout=20)
            check(
                result.returncode == expected,
                f"CLI {' '.join(options[:1])} exit {expected}: {result.stderr if result.returncode != expected else ''}",
            )
            if password:
                check(password.strip() not in result.stdout + result.stderr, "password not printed")
            return result

        cli("--init-db")
        grid_migration = Path(__file__).resolve().parents[2] / "OpenSim/Data/PGSQL/Resources/GridStore.migrations"
        grid_sql = grid_migration.read_text()
        expected_columns = set(re.findall(r'(?im)^\s*"?([A-Za-z][A-Za-z0-9_]*)"? (?:varchar|uuid|bigint|int|integer)\b', grid_sql))
        expected_columns.update(re.findall(r'(?i)ALTER TABLE regions ADD\s+"?(\w+)"?\s+(?:varchar|uuid|int|integer)\b', grid_sql))
        with contextlib.closing(sqlite3.connect(root / "regions.db")) as connection:
            columns = connection.execute("PRAGMA table_info(regions)").fetchall()
            check({row[1] for row in columns} == expected_columns, "native SQLite region columns match canonical PostgreSQL GridStore field names")
        with contextlib.closing(sqlite3.connect(database)) as connection:
            check(
                connection.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("AuthStore",2),("UserAccount",3),("migrations",1)],
                "account/authentication initialization executes original migrations and records v3 metadata",
            )
        before = database.read_bytes()
        cli("--init-db", expected=1)
        check(database.read_bytes() == before, "initialization refuses existing database without changing it")
        cli("--create-user", "Test", "User", password="test-password\n")
        cli("--create-user", "Admin", "User", "--user-level", "200", password="admin-password\n")
        cli("--create-user", "O'Connor", "試験", password="another-試験-password\n")
        cli("--create-user", "Test", "User", password="duplicate-password\n", expected=1)
        with contextlib.closing(sqlite3.connect(database)) as connection, connection:
            row = connection.execute("SELECT u.PrincipalID,a.passwordHash,a.passwordSalt,a.webLoginKey FROM UserAccounts u JOIN auth a ON u.PrincipalID=a.UUID WHERE FirstName='Test'").fetchone()
            principal, stored_hash, salt, web_key = row
            expected = hashlib.md5((hashlib.md5(b"test-password").hexdigest() + ":" + salt).encode()).hexdigest()
            check(stored_hash == expected and web_key == str(uuid.UUID(int=0)), "database uses source-compatible salted password and web-key fields")
            check(connection.execute("SELECT count(*) FROM UserAccounts").fetchone()[0] == 3, "duplicate user transaction did not leave an account")

        class RunningServer:
            def __init__(self, extra=()):
                self.process = subprocess.Popen([executable, "--config", str(config), "--console", *extra],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, encoding="utf-8")
                lines = queue.Queue()
                def read_stdout():
                    for line in self.process.stdout:
                        lines.put(line)
                    lines.put(None)
                self.reader = threading.Thread(target=read_stdout, daemon=True)
                self.reader.start()
                try:
                    deadline = time.monotonic() + 15
                    startup = []
                    while True:
                        line = lines.get(timeout=max(0, deadline - time.monotonic()))
                        if line is None:
                            raise RuntimeError("Server did not start: " + "".join(startup) + self.process.stderr.read())
                        if line.startswith("LISTENING "):
                            break
                        startup.append(line)
                    self.uri = urllib.parse.urlsplit(line.split(" ", 1)[1].strip())
                    self.lines = lines
                except BaseException:
                    self.process.kill()
                    self.process.wait(timeout=5)
                    raise

            def request(self, method, path, body=None, content_type=None, headers=None, connector_port=None):
                if connector_port is not None:
                    connection = http.client.HTTPConnection(self.uri.hostname, connector_port, timeout=8)
                elif self.uri.scheme == "https":
                    context = ssl.create_default_context(cafile=str(Path(__file__).parent / "fixtures/localhost-cert.pem"))
                    connection = http.client.HTTPSConnection(self.uri.hostname, self.uri.port, timeout=8, context=context)
                else:
                    connection = http.client.HTTPConnection(self.uri.hostname, self.uri.port, timeout=8)
                try:
                    fields = dict(headers or {})
                    if content_type:
                        fields["Content-Type"] = content_type
                    connection.request(method, path, body, fields)
                    response = connection.getresponse()
                    return response.status, dict(response.getheaders()), response.read()
                finally:
                    connection.close()

            def login(self, data, method="login_to_simulator", connector_port=None):
                body = xmlrpc.client.dumps((data,), methodname=method, allow_none=True).encode()
                status, headers, result = self.request("POST", "/", body, "text/xml", connector_port=connector_port)
                check(status == 200, "XML-RPC returns HTTP 200")
                return xmlrpc.client.loads(result)[0][0]

            def stop(self):
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.wait(timeout=10)
                self.reader.join(timeout=2)
                tail = []
                while not self.lines.empty():
                    tail.append(self.lines.get())
                error = self.process.stderr.read()
                check(self.process.returncode == 0 and "STOPPED\n" in tail, "console shutdown joins listener/workers cleanly: " + error)
                self.process.stdin.close()
                self.process.stdout.close()
                self.process.stderr.close()

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                try:
                    self.stop()
                finally:
                    if self.process.poll() is None:
                        self.process.kill()
                        self.process.wait(timeout=5)

        credentials = {"first": "Test", "last": "User", "passwd": "test-password"}
        grid_user = "00000000-0000-0000-0000-000000000001"
        grid_region = "00000000-0000-0000-0000-000000000003"
        with RunningServer() as server:
            mute_agent = str(uuid.uuid4())
            mute_id = str(uuid.uuid4())
            mute_auth = base64.b64encode(b"mutes:fixture-mute-password").decode()

            def mute_request(method, **fields):
                form = urllib.parse.urlencode({"METHOD": method, "agentid": mute_agent, **fields})
                status, _, body = server.request(
                    "POST", "/mutelist", form, "application/x-www-form-urlencoded",
                    {"Authorization": f"Basic {mute_auth}"},
                )
                check(status == 200, f"Mute list {method} returns service XML")
                return ET.fromstring(body).findtext("result")

            check(server.request("POST", "/mutelist", "METHOD=get")[0] == 401,
                  "Mute list requires authentication")
            check(mute_request("get", mutecrc="0") == "", "Empty mute list has an empty payload")
            check(mute_request("get", mutecrc="-1") == "Failure", "Mute list rejects an invalid CRC")
            mute_fields = {"muteid": mute_id, "mutename": "Muted & Test"}
            check(mute_request("update", **mute_fields, mutetype="1", muteflags="2", mutestamp="123") == "Success",
                  "Mute list accepts an update and explicit timestamp")
            with sqlite3.connect(root / "mutes.db") as connection:
                check(connection.execute("SELECT Stamp FROM MuteList").fetchall() == [(123,)],
                      "Mute list persists the explicit timestamp")
            mute_data = f"1 {mute_id} Muted & Test|2\n".encode()
            check(base64.b64decode(mute_request("get", mutecrc="0")) == mute_data,
                  "Mute list returns the viewer payload in base64")
            check(mute_request("get", mutecrc=zlib.crc32(mute_data)) == "AQ==",
                  "Mute list returns the cache-hit marker")
            check(mute_request("delete", **mute_fields) == "Success", "Mute list removes an existing entry")
            check(mute_request("delete", **mute_fields) == "Failure", "Mute list reports a missing entry")
            bake_id = "01234567-89ab-4cde-8fab-0123456789ab"
            bake_path = "/bakes/" + bake_id
            bake_auth = {"Authorization": "Basic " + base64.b64encode(b"bakes:fixture-bakes-password").decode()}
            def preference_request(method, **fields):
                status, _, payload = server.request("POST", "/agentprefs", urllib.parse.urlencode(dict(METHOD=method, **fields)),
                                                    "application/x-www-form-urlencoded", bake_auth)
                check(status == 200, "Authenticated agent preferences request succeeds")
                return {node.tag: node.text for node in ET.fromstring(payload)}
            check(server.request("POST", "/agentprefs", "METHOD=getagentprefs")[0] == 401, "Agent preferences require authentication")
            check(preference_request("getagentprefs", UserID=bake_id) == {}, "Missing preferences return an empty result")
            values = dict(PrincipalID=bake_id, AccessPrefs="M", HoverHeight="0.125", Language="fr-ca",
                          LanguageIsPublic="False", PermEveryone="0", PermGroup="0", PermNextOwner="532480")
            check(preference_request("setagentprefs", **values) == {"result": "Success"}, "Agent preferences are stored")
            check(preference_request("getagentprefs", UserID=bake_id) == values, "Agent preferences round-trip over the legacy protocol")
            check(preference_request("getagentlang", UserID=bake_id) == {"Language": "en-us"}, "Private language is withheld")
            values["LanguageIsPublic"] = "True"
            preference_request("setagentprefs", **values)
            check(preference_request("getagentlang", UserID=bake_id) == {"Language": "fr-ca"}, "Public language is returned")
            check(server.request("GET", bake_path)[0] == 401, "XBakes requires configured authentication")
            check(server.request("GET", bake_path, headers=bake_auth)[::2] == (200, b""), "XBakes missing entries return empty data")
            for data in (b"first\x00bake", b"replacement\xff"):
                check(server.request("POST", bake_path, data, "application/octet-stream", bake_auth)[0] == 200, "XBakes stores bytes")
                check(server.request("GET", bake_path, headers=bake_auth)[::2] == (200, data), "XBakes returns the latest bytes")
            check((root / "bakes/01/23/45/67-8" / bake_id).read_bytes() == data, "XBakes uses the legacy storage layout")
            server.process.stdin.write(f"delete bakes {bake_id}\n")
            server.process.stdin.flush()
            deadline = time.monotonic() + 5
            while "Bakes deleted" not in server.lines.get(timeout=max(.01, deadline - time.monotonic())):
                check(time.monotonic() < deadline, "XBakes console confirms deletion")
            check(server.request("GET", bake_path, headers=bake_auth)[::2] == (200, b""), "XBakes console deletion removes the stored payload")
            def account_request(method, **values):
                status, _, body = server.request("POST", "/accounts", urllib.parse.urlencode({"METHOD": method, **values}))
                check(status == 200, "account service HTTP 200")
                return ET.fromstring(body)
            check(
                account_request("getaccount", FirstName="Test", LastName="User").findtext("result/PrincipalID") == principal,
                "account name lookup through HTTP",
            )
            check(
                account_request("getaccount", UserID=principal, PrincipalID=str(uuid.uuid4())).findtext("result/PrincipalID") == principal,
                "account UserID takes precedence",
            )
            check(
                account_request("getaccount", UserID="invalid", FirstName="Test", LastName="User").findtext("result") == "null",
                "invalid account ID suppresses lower-priority lookup",
            )
            check(
                account_request("setaccount", PrincipalID=principal, UserTitle="Network <&> 試験", Email="network@example").findtext("result/UserTitle") == "Network <&> 試験",
                "account update uses full SQLite replacement",
            )
            check(
                account_request("getaccount", Email="network@example").findtext("result/PrincipalID") == principal,
                "account email lookup sees update",
            )
            check(
                account_request("getaccounts", query="Tes").findtext("account0/PrincipalID") == principal,
                "account search through SQLite and HTTP",
            )
            status, _, body = server.request("POST", "/accounts", "METHOD=getmultiaccounts&IDS[]=" + principal)
            check(status == 200 and ET.fromstring(body).findtext("result") == "null", "SQLite multi-account query retains original empty result")
            created_id=str(uuid.uuid4())
            created=account_request("createuser", PrincipalID=created_id, ScopeID=str(uuid.uuid4()), FirstName=" New ", LastName=" Provisioned ", Password="provisioned-password", Email="new@example")
            check(
                created.findtext("result/PrincipalID") == created_id
                and created.findtext("result/ScopeID") == str(uuid.UUID(int=0)),
                "account creation retains supplied principal and original zero-scope behavior",
            )
            check(
                created.findtext("result/FirstName") == "New"
                and created.findtext("result/LastName") == "Provisioned",
                "account creation trims names",
            )
            check(
                created.findtext("result/ServiceURLs") == "HomeURI*;InventoryServerURI*;AssetServerURI*;",
                "account creation stores original initial service URLs",
            )
            check(
                account_request("createuser", FirstName="New", LastName="Provisioned", Password="other").findtext("result") == "Failure",
                "duplicate names reject provisioning",
            )
            with contextlib.closing(sqlite3.connect(database)) as connection:
                password_hash,password_salt=connection.execute("SELECT passwordHash,passwordSalt FROM auth WHERE UUID=?",(created_id,)).fetchone()
                expected=hashlib.md5((hashlib.md5(b"provisioned-password").hexdigest()+":"+password_salt).encode()).hexdigest()
                check(password_hash == expected, "HTTP account provisioning sets the source salted password hash")
            check(server.request("GET", "/accounts")[0] == 405, "account endpoint requires POST")
            check(
                server.request("POST", "/accounts", "METHOD=getaccount", headers={"X-SecondLife-Shard":""})[0] == 403,
                "account endpoint excludes script requests",
            )
            region_id = "00000000-0000-0000-0000-000000000010"
            registration = {
                "METHOD": "register",
                "VERSIONMIN": "1",
                "VERSIONMAX": "1",
                "uuid": region_id,
                "regionName": "Network Region",
                "locX": "25600",
                "locY": "25600",
                "sizeX": "256",
                "sizeY": "256",
                "serverPort": "9000",
                "serverIP": "127.0.0.1",
            }
            def registry(values, headers=None):
                status, _, body = server.request("POST", "/grid", urllib.parse.urlencode(values), headers=headers)
                check(status == 200, "grid registry HTTP response")
                return ET.fromstring(body)
            check(registry({"METHOD":"register"}).findtext("Result") == "Failure", "grid registration protocol version required")
            check(registry(registration).findtext("Result") == "Success", "grid region registration through real HTTP")
            check(
                registry({"METHOD":"get_region_by_name", "NAME":"Network Region"}).findtext("result/uuid") == region_id,
                "registered region name lookup through HTTP",
            )
            linked = server.login({"region_name": "Network Region"}, "link_region")
            check(linked["result"] == "True" and linked["uuid"] == region_id, "Gatekeeper links a registered region")
            destination = server.login({"region_uuid": region_id}, "get_region")
            check(destination["result"] == "true" and destination["internal_port"] == "9000", "Gatekeeper resolves a linked region")
            check(server.login({"region_uuid": "00000000-0000-0000-0000-000000000000"}, "get_region")["result"] == "false",
                  "Gatekeeper reports a missing region")

            def check_gatekeeper_types(connection, port=None):
                invalid_calls = (
                    ("link_region", []),
                    ("link_region", {"region_name": 7}),
                    ("get_region", {"region_uuid": 7}),
                    ("get_region", {"region_uuid": region_id, "agent_id": True}),
                    ("get_region", {"region_uuid": region_id, "agent_home_uri": []}),
                )
                for method, fields in invalid_calls:
                    try:
                        connection.login(fields, method, connector_port=port)
                    except xmlrpc.client.Fault as fault:
                        check(fault.faultCode == -32603, f"{method} faults on an invalid XML-RPC type")
                    else:
                        check(False, f"{method} accepted an invalid XML-RPC type")

            check_gatekeeper_types(server)
            check(
                registry({"METHOD":"get_region_by_name", "NAME":"http://127.0.0.1:9000/Network Region"}).findtext("result/uuid") == region_id,
                "configured local grid URL resolves through HTTP",
            )
            check(
                registry({"METHOD":"get_localregion_by_name", "NAME":"http://127.0.0.1:9000/Network Region"}).findtext("result/uuid") == region_id,
                "configured local-only grid URL resolves through HTTP",
            )
            check(registry({"METHOD":"get_default_regions"}).findtext("region0/uuid") == region_id, "grid default flags applied through HTTP")
            check(
                registry({"METHOD":"get_online_regions", "MC":"0"}).findtext("result") == "null",
                "grid online HTTP count zero skips service query",
            )
            check(registry({"METHOD":"get_region_flags", "REGIONID":region_id}).findtext("result") == "7", "grid wire flags query")
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                results = list(pool.map(lambda _:registry({"METHOD":"get_region_by_uuid", "REGIONID":region_id}).findtext("result/uuid"), range(12)))
            check(all(value == region_id for value in results), "concurrent grid HTTP reads")
            check(registry({"METHOD":"get_grid_extra_features"}).findtext("ExportSupported") == "true", "grid default extra feature through HTTP")
            check(
                registry({"METHOD":"get_grid_extra_features"}).findtext("GridURLAlias") == "http://alias.example:80,",
                "grid alias normalization through real HTTP",
            )
            check(
                server.request("POST", "/grid", "METHOD=get_default_regions", headers={"X-SecondLife-Shard":""})[0] == 403,
                "grid script requests excluded",
            )
            check(server.request("GET", "/grid")[0] == 405, "grid URI requires POST")
            homed_id=str(uuid.uuid4())
            check(
                account_request("createuser", PrincipalID=homed_id, FirstName="Home", LastName="Provisioned", Password="home-password").findtext("result/PrincipalID") == homed_id,
                "account creation connects the default-region service",
            )
            status, _, body = server.request("POST", "/griduser", "METHOD=getgriduserinfo&UserID="+homed_id)
            home=ET.fromstring(body)
            check(
                status == 200
                and home.findtext("result/HomeRegionID") == region_id
                and home.findtext("result/HomePosition") == "<128, 128, 0>"
                and home.findtext("result/HomeLookAt") == "<0, 1, 0>",
                "account creation persists source default home and look-at",
            )
            check(registry({"METHOD":"deregister", "REGIONID":region_id}).findtext("Result") == "Success", "grid deregistration through HTTP")
            check(registry({"METHOD":"get_region_by_uuid", "REGIONID":region_id}).findtext("result") == "null", "deregistered grid entry removed")
            persisted_id = "00000000-0000-0000-0000-000000000011"
            persisted_scope = "00000000-0000-0000-0000-000000000020"
            check(
                registry({**registration, "uuid":persisted_id, "SCOPEID":persisted_scope, "regionName":"Persistent Region", "locX":"51200", "locY":"51200", "sizeY":"512"}).findtext("Result") == "Success",
                "scoped durable grid registration",
            )
            def grid_request(method, **values):
                status, _, body = server.request("POST", "/griduser", urllib.parse.urlencode({"METHOD": method, **values}))
                check(status == 200, "grid-user HTTP response")
                return ET.fromstring(body)
            check(grid_request("getgriduserinfo", UserID=grid_user).findtext("result") == "Failure", "grid-user absent record failure")
            check(grid_request("loggedin", UserID=grid_user).findtext("result/Online") == "True", "grid-user login stored through HTTP")
            check(
                grid_request("sethome", UserID=grid_user, RegionID=f" {grid_region} ", Position="<10,20,30>").findtext("result") == "Success",
                "grid-user home accepts a space-padded SDK region UUID",
            )
            check(
                grid_request("sethome", UserID=grid_user, RegionID="{" + grid_region + "}").findtext("result") == "Failure",
                "grid-user home rejects a brace-wrapped region UUID",
            )
            check(
                grid_request("setposition", UserID=grid_user, RegionID=grid_region, Position="<40,50,60>").findtext("result") == "Success",
                "grid-user last position updated through HTTP",
            )
            check(
                grid_request("loggedout", UserID=grid_user, RegionID=grid_region, Position="<70,80,90>").findtext("result") == "Success",
                "grid-user logout through HTTP",
            )
            statistics_region = str(uuid.uuid4())
            check(
                registry({**registration, "uuid": statistics_region, "regionName": "Statistics Region",
                          "locX": "76800", "locY": "76800", "sizeX": "512", "sizeY": "512"}).findtext("Result")
                == "Success",
                "register a four-tile region for grid statistics",
            )
            now = int(time.time())
            with contextlib.closing(sqlite3.connect(root / "grid-user.db")) as connection, connection:
                residents = connection.execute("SELECT COUNT(*) FROM GridUser WHERE UserID NOT LIKE '%;%'").fetchone()[0]
                connection.executemany(
                    "INSERT INTO GridUser (UserID, Login) VALUES (?, ?)",
                    [(f"{uuid.uuid4()};http://foreign.example/;Foreign User", now),
                     (str(uuid.uuid4()), now - 2592001),
                     (str(uuid.uuid4()), "invalid")],
                )
            status, headers, stats = server.request("GET", "/get_grid_stats")
            counts = ET.fromstring(stats)
            check(
                status == 200 and counts.tag == "gridstats"
                and counts.findtext("residents") == str(residents + 2)
                and counts.findtext("active_users") == "2"
                and counts.findtext("region_count") == "6",
                "grid statistics count region tiles, local residents, and recent local and foreign users",
            )
            check(
                registry({"METHOD": "deregister", "REGIONID": statistics_region}).findtext("Result") == "Success"
                and server.request("GET", "/get_grid_stats")[2] == stats,
                "grid statistics keep the cached response after deregistration",
            )
            for path in ("/get_grid_stats", "/get_grid_info", "/json_grid_info"):
                check(server.request("HEAD", path)[0] == 405, f"{path} requires GET")
            check(
                server.request("POST", "/griduser", "METHOD=loggedin&UserID=blocked", headers={"X-SecondLife-Shard":""})[0] == 403,
                "grid-user script request excluded",
            )
            check(server.request("GET", "/griduser")[0] == 405, "grid-user URI requires POST")
            presence_body = b"METHOD=getagents&uuids[]=missing"
            status, _, result = server.request("POST", "/presence", presence_body, "application/x-www-form-urlencoded")
            check(status == 200 and ET.fromstring(result).findtext("result") == "null", "presence endpoint runs through real HTTP listener")
            status, _, result = server.request("POST", "/presence", b"METHOD=login&UserID=remote&SessionID=" + str(uuid.uuid4()).encode())
            check(status == 200 and ET.fromstring(result).findtext("result") == "Failure", "presence endpoint refuses remote login")
            check(
                server.request("POST", "/presence", presence_body, headers={"X-SecondLife-Shard": ""})[0] == 403,
                "service request headers enforce script exclusion",
            )
            check(server.request("GET", "/presence")[0] == 405, "presence URI requires POST")
            status, headers, body = server.request("GET", "/health")
            health = json.loads(body)
            check(
                status == 200
                and health["authentication_ready"]
                and not health["simulation_ready"]
                and not health["login_success_available"],
                "health reports actual migration readiness",
            )
            check(server.request("GET", "/ready")[0] == 503, "simulator readiness is unavailable")
            check(server.request("GET", "/")[0] == 405, "login URI requires POST")
            check(server.request("GET", "/not-registered")[0] == 404, "unknown path returns 404")
            status, headers, body = server.request("GET", "/get_grid_info")
            info = ET.fromstring(body)
            check(status == 200 and info.findtext("gridname") == "Native & Test <Grid>", "grid discovery escapes XML")
            check(info.findtext("login") == server.uri.geturl(), "ephemeral listener advertises its actual port")
            check(json.loads(server.request("GET", "/json_grid_info")[2])["platform"] == "OpenSim", "JSON grid discovery")
            for method in ("GET", "HEAD"):
                status, headers, body = server.request(method, "/helo/" if method == "GET" else "/helo")
                check(status == 200 and {key.lower(): value for key, value in headers.items()}.get(
                    "x-handlers-provided") == "opensim-robust" and not body,
                      f"Hypergrid HELO {method} discovery")
            check(server.request("POST", "/helo")[0] == 405, "Hypergrid HELO rejects POST")
            check(server.login({}, "get_grid_info")["gridnick"] == "native-test", "XML-RPC grid discovery")
            bound = cli("--port", str(server.uri.port), expected=1)
            check("bind" in bound.stderr.lower(), "occupied port fails visibly")
            check(server.login({})["message"].startswith("Incomplete login credentials"), "missing XML-RPC credentials match source failure")
            check(server.login({**credentials, "passwd": "wrong"})["message"].startswith("Could not authenticate"), "wrong credentials rejected")
            authenticated = server.login(credentials)
            check(
                server.login({"first":"New","last":"Provisioned","passwd":"provisioned-password"}) == authenticated,
                "HTTP-provisioned credentials authenticate through viewer login intake",
            )
            check(
                authenticated["login"] == "false"
                and authenticated["message"].startswith("Error connecting to the desired location"),
                "valid password reaches unavailable-region response without fake session",
            )
            check(
                server.login({**credentials, "passwd": "$1$" + hashlib.md5(b"test-password").hexdigest()}) == authenticated,
                "viewer prehashed credential accepted",
            )
            check(
                server.login({"first": "O'Connor", "last": "試験", "passwd": "another-試験-password"}) == authenticated,
                "Unicode CLI/account/login and SQL parameter binding",
            )
            check(server.login({**credentials, "scope_id": str(uuid.uuid4())}) == authenticated, "unscoped account falls back for requested scope")
            check(
                server.login({**credentials, "passwd": None, "web_login_key": str(uuid.uuid4())})["message"].startswith("Incomplete"),
                "null password does not use web-key fallback",
            )
            try:
                server.login({}, "unknown_method")
                raise AssertionError("Expected XML-RPC fault")
            except xmlrpc.client.Fault as fault:
                check(fault.faultCode == -32601, "unknown RPC method fault")
            check(server.request("POST", "/", b"<broken", "text/xml")[0] == 404, "malformed XML-RPC follows source HTTP failure status")
            check(server.request("POST", "/", b"<llsd><array/></llsd>", "application/llsd+xml")[0] == 400, "LLSD root must be a map")
            llsd = b"<llsd><map><key>first</key><string>Test</string><key>last</key><string>User</string><key>passwd</key><string>test-password</string></map></llsd>"
            status, headers, body = server.request("POST", "/", llsd, "application/llsd+xml; charset=utf-8")
            check(status == 200 and ET.fromstring(body).find("map") is not None and b"desired location" in body, "LLSD XML login over TCP")
            check(server.request("POST", "/", b"x" * 9000, "text/xml")[0] == 413, "HTTP payload limit enforced before parsing")
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                replies = list(pool.map(lambda _: server.login({**credentials, "passwd": "wrong"}), range(48)))
            check(all(reply["reason"] == "key" for reply in replies), "concurrent requests remain isolated and responsive")
            key = str(uuid.uuid4())
            with contextlib.closing(sqlite3.connect(database)) as connection, connection:
                connection.execute("UPDATE auth SET webLoginKey=? WHERE UUID=?", (key, principal))
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                replies = list(pool.map(lambda _: server.login({"first": "Test", "last": "User", "web_login_key": key}), range(2)))
            check(sum(reply == authenticated for reply in replies) == 1, "web key is consumed exactly once under concurrent requests")
            admin = {"first": "Admin", "last": "User", "passwd": hashlib.md5(b"admin-password").hexdigest(), "level": 100}
            check(server.login(admin, "set_login_level")["success"] is True, "administrator can set login level with legacy prehash")
            check(server.login(credentials)["reason"] == "presence", "minimum login level enforced")
            check(server.login({**admin, "level": 0}, "set_login_level")["success"] is True, "administrator can restore login level")
            check(server.login({**credentials, "level": 100}, "set_login_level")["success"] == "false", "ordinary user cannot set login level")
            body = xmlrpc.client.dumps((credentials,), methodname="login_to_simulator").encode()
            chunks = [body[:50], body[50:]]
            raw = b"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            for part in chunks:
                raw += f"{len(part):x}\r\n".encode() + part + b"\r\n"
            raw += b"0\r\n\r\n"
            with socket.create_connection((server.uri.hostname, server.uri.port), timeout=8) as wire:
                wire.sendall(raw)
                response = b""
                while part := wire.recv(65536):
                    response += part
            check(response.startswith(b"HTTP/1.1 200") and b"desired location" in response, "HTTP chunked request framing")
            compressed = gzip.compress(body)
            status, _, response = server.request("POST", "/", compressed, "text/xml", {"Content-Encoding": "gzip"})
            if features["gzip"]:
                check(status == 200 and xmlrpc.client.loads(response)[0][0] == authenticated, "gzip login request")
            else:
                check(status >= 400, "unsupported gzip fails explicitly")
                skipped.append("gzip support not compiled")
        legacy_grid_user = str(uuid.uuid4())
        with contextlib.closing(sqlite3.connect(root / "grid-user.db")) as connection, connection:
            connection.execute(
                "UPDATE GridUser SET HomeRegionID=?, LastRegionID=? WHERE UserID=?",
                (f" {grid_region} ", "{" + grid_region + "}", grid_user),
            )
            connection.execute(
                "INSERT INTO GridUser (UserID, Login, Logout, Online) VALUES (?, ?, ?, ?)",
                (legacy_grid_user, "+-1", "-1\0", "\0 True \0"),
            )
        with RunningServer() as restarted:
            status, _, body = restarted.request("POST", "/accounts", "METHOD=getaccount&UserID=" + created_id)
            check(status == 200 and ET.fromstring(body).findtext("result/FirstName") == "New", "provisioned account survives restart")
            status, _, body = restarted.request("POST", "/accounts", "METHOD=getaccount&UserID=" + principal)
            check(
                status == 200
                and ET.fromstring(body).findtext("result/UserTitle") == "Network <&> 試験",
                "account metadata persists after server restart",
            )
            status, _, result = restarted.request("POST", "/grid", urllib.parse.urlencode({"METHOD":"get_region_by_uuid", "SCOPEID":persisted_scope, "REGIONID":persisted_id}))
            region = ET.fromstring(result).find("result")
            check(
                status == 200
                and region.findtext("regionName") == "Persistent Region"
                and region.findtext("sizeY") == "512",
                "grid region fields survive server restart",
            )
            status, _, result = restarted.request("POST", "/grid", urllib.parse.urlencode({"METHOD":"get_region_by_uuid", "SCOPEID":str(uuid.uuid4()), "REGIONID":persisted_id}))
            check(status == 200 and ET.fromstring(result).findtext("result") == "null", "durable region scope filter survives restart")
            status, _, result = restarted.request("POST", "/griduser", "METHOD=getgriduserinfo&UserID="+grid_user)
            info = ET.fromstring(result).find("result")
            check(
                status == 200
                and info.findtext("Online") == "False"
                and info.findtext("HomePosition") == "<10, 20, 30>"
                and info.findtext("LastPosition") == "<70, 80, 90>",
                "grid-user home and logout position survive server restart",
            )
            check(info.findtext("HomeRegionID") == grid_region and info.findtext("LastRegionID") == str(uuid.UUID(int=0)),
                  "stored grid-user region IDs use SDK parsing after restart")
            status, _, result = restarted.request("POST", "/griduser", "METHOD=getgriduserinfo&UserID=" + legacy_grid_user)
            legacy = ET.fromstring(result).find("result")
            check(status == 200 and legacy.findtext("Login") == "01/01/1970 00:00:00"
                  and legacy.findtext("Logout") == "12/31/1969 23:59:59" and legacy.findtext("Online") == "True",
                  "grid-user dates and online state follow .NET parsing for legacy stored values")
            check(restarted.login(credentials)["message"].startswith("Error connecting"), "account/password persistence survives server restart")
        config.write_text(base_config + f"\n[GatekeeperService]\nScopeID={persisted_scope}\nAllowTeleportsToAnyRegion=false\n", encoding="utf-8")
        with RunningServer() as restricted:
            try:
                restricted.login({"region_uuid": persisted_id}, "get_region")
            except xmlrpc.client.Fault as fault:
                check(fault.faultCode == -32603, "Gatekeeper faults before linking a default region")
            else:
                check(False, "Gatekeeper faults before linking a default region")
            linked = restricted.login({"region_name": "Unlisted"}, "link_region")
            check(linked["result"] == "True" and linked["uuid"] == persisted_id, "Gatekeeper links the configured default region")
            destination = restricted.login({"region_uuid": str(uuid.uuid4())}, "get_region")
            check(destination["uuid"] == persisted_id and destination["message"] == "Teleporting to the default region.",
                  "Gatekeeper redirects arbitrary region requests to the default region")
        with socket.socket() as probe, socket.socket() as discovery_probe:
            probe.bind(("127.0.0.1", 0))
            public_port = probe.getsockname()[1]
            discovery_probe.bind(("127.0.0.1", 0))
            discovery_port = discovery_probe.getsockname()[1]
        split_config = with_settings(base_config, "GridService",
                                     LocalServiceModule="OpenSim.Services.GridService.dll:GridService")
        split_config = split_config.replace("EnableGridEndpoint = true\n", "").replace(
            "EnableGridUserEndpoint = true\n", "")
        travel_session = str(uuid.uuid4())
        expired_session = str(uuid.uuid4())
        travel_schema = (Path(__file__).resolve().parents[2] /
                         "OpenSim/Data/SQLite/Resources/HGTravelStore.migrations").read_text(encoding="utf-8")
        with contextlib.closing(sqlite3.connect(root / "split-travel.db")) as travel, travel:
            travel.executescript(travel_schema.split("\n", 1)[1].replace("hg_traveling_data", '"travel history"'))
            travel.execute('INSERT INTO "travel history" (SessionID, UserID, ServiceToken) VALUES (?, ?, ?)',
                           (travel_session, str(uuid.uuid4()), "legacy-travel-token"))
            travel.execute(
                'INSERT INTO "travel history" (SessionID, UserID, ServiceToken, TMStamp) '
                "VALUES (?, ?, ?, datetime('now', '-3 day'))",
                (expired_session, str(uuid.uuid4()), "expired-travel-token"))
        config.write_text(split_config + f"\n[GatekeeperService]\nScopeID={persisted_scope}\n[ServiceList]\n"
                          f"GatekeeperServiceInConnector={public_port}/OpenSim.Server.Handlers.dll:GatekeeperServiceInConnector\n"
                          f"UserAgentServerConnector={public_port}/OpenSim.Server.Handlers.dll:UserAgentServerConnector\n"
                          f"HeloServiceInConnector={public_port}/OpenSim.Server.Handlers.dll:HeloServiceInConnector\n"
                          f"BakedTextureService={public_port}/OpenSim.Server.Handlers.dll:XBakesConnector\n"
                          f"MuteListConnector={public_port}/OpenSim.Server.Handlers.dll:MuteListServiceConnector\n"
                          f"PresenceServiceConnector={public_port}/OpenSim.Server.Handlers.dll:PresenceServiceConnector\n"
                          f"GridServiceConnector={public_port}/OpenSim.Server.Handlers.dll:GridServiceConnector\n"
                          f"GridUserServiceConnector={public_port}/OpenSim.Server.Handlers.dll:GridUserServiceConnector\n"
                          f"GridInfoServerInConnector={discovery_port}/OpenSim.Server.Handlers.dll:GridInfoServerInConnector\n"
                          "[UserAgentService]\n"
                          "LocalServiceModule=OpenSim.Services.HypergridService.dll:UserAgentService\n"
                          "StorageProvider=OpenSim.Data.SQLite.dll\n"
                          "ConnectionString=Data Source=split-travel.db\nRealm=travel history\n"
                          "[PresenceService]\nLocalServiceModule=OpenSim.Services.PresenceService.dll:PresenceService\n"
                          "[GridUserService]\nLocalServiceModule=OpenSim.Services.UserAccountService.dll:GridUserService\n"
                          "AuthType=BasicHttpAuthentication\nHttpAuthUsername=griduser\n"
                          "HttpAuthPassword=fixture-griduser-password\n",
                          encoding="utf-8")
        with RunningServer() as split:
            with contextlib.closing(sqlite3.connect(root / "split-travel.db")) as travel:
                version = travel.execute(
                    "SELECT version FROM migrations WHERE name = 'HGTravelStore'").fetchone()
                check(version == (2,), "The travel store records the original SQLite migration version")
                default_realm = travel.execute("SELECT COUNT(*) FROM hg_traveling_data").fetchone()
                check(default_realm == (0,), "A custom travel realm still initializes the original schema")
            check(split.login({"sessionID": travel_session, "token": "legacy-travel-token"},
                              "verify_agent", connector_port=public_port) == {"result": "True"},
                  "Home-agent verification reads an existing SQLite travel realm")
            check(split.login({"sessionID": expired_session, "token": "expired-travel-token"},
                              "verify_agent", connector_port=public_port) == {"result": "False"},
                  "Home-agent startup expires old records in the configured SQLite realm")
            identity = {"first": credentials["first"], "last": credentials["last"]}
            check(split.login(identity, "get_uuid", connector_port=public_port)["UUID"] != str(uuid.UUID(int=0)),
                  "Home-agent lookup shares the configured Hypergrid listener")
            try:
                split.login(identity, "get_uuid")
            except xmlrpc.client.Fault as fault:
                check(fault.faultCode == -32601, "Home-agent lookup is scoped to its configured listener")
            else:
                check(False, "Home-agent lookup is scoped to its configured listener")
            home_path = f"/homeagent/{uuid.uuid4()}/"
            check(split.request("GET", home_path, connector_port=public_port)[0] == 405
                  and split.request("GET", home_path)[0] == 404,
                  "Home-agent admission is scoped to the same listener as its XML-RPC methods")
            for path in ("/get_grid_info", "/json_grid_info", "/get_grid_stats"):
                check(split.request("GET", path, connector_port=discovery_port)[0] == 200
                      and split.request("GET", path)[0] == 404,
                      f"{path} is served only on the configured grid discovery port")
            discovery = split.login({}, "get_grid_info", connector_port=discovery_port)
            check(discovery["gridnick"] == "native-test" and discovery["login"] == split.uri.geturl(),
                  "grid discovery XML-RPC runs on the configured port and advertises the login listener")
            try:
                split.login({}, "get_grid_info")
            except xmlrpc.client.Fault as fault:
                check(fault.faultCode == -32601, "the login listener does not register split grid discovery XML-RPC")
            else:
                check(False, "the login listener does not register split grid discovery XML-RPC")
            check(split.request("POST", "/", xmlrpc.client.dumps((credentials,), methodname="login_to_simulator"),
                                "text/xml", connector_port=discovery_port)[0] == 404,
                  "the standalone grid discovery port does not accept viewer login")
            check(split.request("POST", "/mutelist", connector_port=public_port)[0] == 401,
                  "Mute list shares its configured service listener")
            check(split.request("POST", "/mutelist")[0] == 404,
                  "Mute list is scoped to its configured service listener")
            status, _, body = split.request("POST", "/presence", presence_body, connector_port=public_port)
            check(status == 200 and ET.fromstring(body).findtext("result") == "null",
                  "Presence shares the existing connector listener")
            check(split.request("POST", "/presence", presence_body)[0] == 404,
                  "Presence is scoped to its configured service listener")
            grid_lookup = urllib.parse.urlencode(
                {"METHOD": "get_region_by_uuid", "SCOPEID": persisted_scope, "REGIONID": persisted_id})
            status, _, body = split.request("POST", "/grid", grid_lookup, connector_port=public_port)
            check(status == 200 and ET.fromstring(body).findtext("result/uuid") == persisted_id,
                  "Grid service reads persistent regions through the configured listener")
            check(split.request("POST", "/grid", grid_lookup)[0] == 404,
                  "Grid service is scoped to its configured listener")
            grid_user_lookup = urllib.parse.urlencode({"METHOD": "getgriduserinfo", "UserID": grid_user})
            check(split.request("POST", "/griduser", grid_user_lookup, connector_port=public_port)[0] == 401,
                  "Grid-user connector requires its configured authentication")
            grid_user_auth = {"Authorization": "Basic " +
                              base64.b64encode(b"griduser:fixture-griduser-password").decode()}
            status, _, body = split.request("POST", "/griduser", grid_user_lookup,
                                            headers=grid_user_auth, connector_port=public_port)
            check(status == 200 and ET.fromstring(body).findtext("result/UserID") == grid_user,
                  "Grid-user service reads persistent accounts through the configured listener")
            check(split.request("POST", "/griduser", grid_user_lookup, headers=grid_user_auth)[0] == 404,
                  "Grid-user service is scoped to its configured listener")
            check(split.login({"region_uuid": persisted_id}, "get_region", public_port)["uuid"] == persisted_id,
                  "Gatekeeper resolves regions on its configured port")
            check_gatekeeper_types(split, public_port)
            check(split.request("HEAD", "/helo", connector_port=public_port)[0] == 200,
                  "HELO and Gatekeeper share the public listener")
            check(split.request("POST", bake_path, b"shared listener bake", headers=bake_auth, connector_port=public_port)[0] == 200,
                  "XBakes shares the configured service listener")
            check(split.request("GET", bake_path, headers=bake_auth, connector_port=public_port)[::2] == (200, b"shared listener bake"),
                  "Shared service listener returns the baked payload")
            for method, port in (("get_region", None), ("login_to_simulator", public_port)):
                try:
                    split.login({}, method, port)
                except xmlrpc.client.Fault as fault:
                    check(fault.faultCode == -32601, f"{method} is isolated to its configured listener")
                else:
                    check(False, f"{method} is isolated to its configured listener")
        config.write_text(with_settings(base_config, "NativeServer", GridRegionProvider="Null"), encoding="utf-8")
        with RunningServer() as volatile:
            status, _, result = volatile.request("POST", "/grid", "METHOD=get_region_by_uuid&REGIONID="+persisted_id)
            check(
                status == 200
                and ET.fromstring(result).findtext("result") == "null",
                "Null region provider remains independent of durable storage",
            )
        config.write_text(with_settings(base_config, "NativeServer", GridRegionProvider="unknown"), encoding="utf-8")
        result = cli(expected=1)
        check("GridRegionProvider" in result.stderr, "unknown region provider rejected explicitly")
        for section,key,message in [("Const","GatekeeperURI","GatekeeperURI configuration error"),("Const","HomeURI","HomeURI configuration error"),("GridInfo","SearchServerURI","SearchServerURI configuration error")]:
            config.write_text(base_config+"\n["+section+"]\n"+key+"=bad/path\n", encoding="utf-8")
            result = cli(expected=1)
            check(message in result.stderr, "invalid "+key+" rejected before listening")
        config.write_text(with_settings(with_settings(base_config, "PresenceService", AuthType="BasicHttpAuthentication", HttpAuthUsername="service", HttpAuthPassword="test-service-password"), "UserAccountService", AuthType="BasicHttpAuthentication", HttpAuthUsername="service", HttpAuthPassword="test-service-password"), encoding="utf-8")
        with RunningServer() as protected:
            check(
                protected.request("POST", "/accounts", "METHOD=getaccount&UserID="+principal)[0] == 401,
                "account endpoint requires configured Basic authentication",
            )
            check(
                protected.request("POST", "/accounts", "METHOD=getaccount&UserID="+principal, headers={"Authorization":"Basic "+base64.b64encode(b"service:test-service-password").decode()})[0] == 200,
                "account endpoint accepts configured Basic authentication",
            )
            status, headers, _ = protected.request("POST", "/presence", presence_body)
            check(status == 401 and headers.get("WWW-Authenticate") == 'Basic realm = "Asset Server"', "presence Basic challenge through HTTP")
            authorization = "Basic " + base64.b64encode(b"service:test-service-password").decode()
            status, _, result = protected.request("POST", "/presence", presence_body, headers={"Authorization": authorization})
            check(status == 200 and ET.fromstring(result).findtext("result") == "null", "presence Basic credentials accepted through HTTP")
            check(
                protected.request("POST", "/presence", presence_body, headers={"Authorization": authorization, "X-SecondLife-Shard": "test"})[0] == 403,
                "script exclusion precedes Basic authentication",
            )
            status, _, result = protected.request("POST", "/presence", presence_body, headers={"Authorization": "Basic invalid!"})
            check(
                status == 500
                and b"test-service-password" not in result,
                "malformed service authentication propagates to HTTP error without credentials",
            )
        startup_presence = with_settings(
            base_config.replace("EnablePresenceEndpoint = true\n", ""), "Startup",
            ServiceConnectors="0/OpenSim.Server.Handlers.dll:PresenceServiceConnector",
        )
        config.write_text(with_settings(startup_presence, "NativeServer", PresenceDatabase="presence-rejected.db"),
                          encoding="utf-8")
        invalid = cli(expected=1)
        check("PresenceService.LocalServiceModule is required" in invalid.stderr and
              not (root / "presence-rejected.db").exists(),
              "Legacy presence registration validates the module before opening storage")
        startup_presence = with_settings(startup_presence, "PresenceService",
                                         LocalServiceModule="OpenSim.Services.PresenceService.dll:PresenceService")
        config.write_text(startup_presence, encoding="utf-8")
        with RunningServer() as legacy:
            status, _, body = legacy.request("POST", "/presence", presence_body)
            check(status == 200 and ET.fromstring(body).findtext("result") == "null",
                  "Legacy Startup.ServiceConnectors registers presence on the primary listener")
        private_grid_config = with_settings(startup_presence, "NativeServer", EnablePresenceEndpoint="false",
                                            EnableGridUserEndpoint="false", EnableGridEndpoint="false",
                                            EnableAccountEndpoint="false")
        config.write_text(private_grid_config, encoding="utf-8")
        with RunningServer() as disabled:
            check(disabled.request("POST", "/accounts", "METHOD=getaccount")[0] == 404, "account route can be disabled")
            check(disabled.request("POST", "/presence", presence_body)[0] == 404, "presence route can be disabled")
            check(disabled.request("POST", "/grid", "METHOD=get_default_regions")[0] == 404, "grid registry route can be disabled")
            check(disabled.request("POST", "/griduser", "METHOD=loggedin")[0] == 404, "grid-user route can be disabled")
            check(disabled.request("GET", "/get_grid_stats")[0] == 405, "statistics require local grid and grid-user services")
        statistics_config = with_settings(
            private_grid_config, "GridService", LocalServiceModule="OpenSim.Services.GridService.dll:GridService",
        )
        config.write_text(statistics_config, encoding="utf-8")
        with RunningServer() as statistics_only:
            status, _, body = statistics_only.request("GET", "/get_grid_stats")
            check(status == 200 and ET.fromstring(body).findtext("region_count") == "2",
                  "grid statistics can read persistent regions with the service endpoints disabled")
            check(statistics_only.request("POST", "/grid", "METHOD=get_default_regions")[0] == 404
                  and statistics_only.request("POST", "/griduser", "METHOD=loggedin")[0] == 404,
                  "statistics-only storage does not expose grid service routes")
        config.write_text(with_settings(
            statistics_config, "NativeServer", GridRegionDatabase="missing-statistics-regions.db",
        ), encoding="utf-8")
        with RunningServer() as unavailable_statistics:
            check(unavailable_statistics.request("GET", "/get_grid_stats")[0] == 405
                  and unavailable_statistics.request("GET", "/get_grid_info")[0] == 200,
                  "missing statistics storage leaves grid discovery running")
        config.write_text(with_settings(base_config, "GridInfoService", DisableStatsEndpoint="true"), encoding="utf-8")
        with RunningServer() as disabled_stats:
            check(disabled_stats.request("GET", "/get_grid_stats")[0] == 404, "grid statistics route can be disabled")
        discovery_config = with_settings(
            base_config, "Startup", HomeURI="http://127.0.0.1:8101/", GatekeeperURI="http://127.0.0.1:8102/",
            HomeURIAlias="http://127.0.0.1:8103/", GatekeeperURIAlias="http://127.0.0.1:8104/",
        )
        discovery_config = with_settings(
            discovery_config, "Hypergrid", HomeURI="http://127.0.0.1:8201/", GatekeeperURI="http://127.0.0.1:8202/",
            HomeURIAlias="http://127.0.0.1:8203/", GatekeeperURIAlias="http://127.0.0.1:8204/",
        )
        config.write_text(discovery_config, encoding="utf-8")
        with RunningServer() as discovery:
            expected = {
                "home": "http://127.0.0.1:8201/",
                "gatekeeper": "http://127.0.0.1:8202/",
                "homealias": "http://127.0.0.1:8203/",
                "gatekeeperalias": "http://127.0.0.1:8204/",
            }
            xml = ET.fromstring(discovery.request("GET", "/get_grid_info")[2])
            payloads = (
                {element.tag: element.text for element in xml},
                json.loads(discovery.request("GET", "/json_grid_info")[2]),
                discovery.login({}, "get_grid_info"),
            )
            check(
                all(all(payload.get(key) == value for key, value in expected.items()) for payload in payloads),
                "all grid discovery formats give Hypergrid URI settings precedence over Startup",
            )
        fallback_config = with_settings(base_config, "LoginService", SRV_HomeURI="http://127.0.0.1:8301/")
        fallback_config = with_settings(fallback_config, "GridInfoService", home="http://127.0.0.1:8302/")
        config.write_text(fallback_config, encoding="utf-8")
        with RunningServer() as discovery:
            check(discovery.login({}, "get_grid_info")["home"] == "http://127.0.0.1:8302/",
                  "configured grid discovery home takes precedence over the login-service fallback")
        config.write_text(with_settings(fallback_config, "Hypergrid", HomeURI=""), encoding="utf-8")
        with RunningServer() as discovery:
            check(discovery.login({}, "get_grid_info")["home"] == "http://127.0.0.1:8301/",
                  "an empty Hypergrid home uses the login-service fallback")
        config.write_text(base_config, encoding="utf-8")
        try:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as probe:
                probe.bind(("::1", 0))
        except OSError:
            skipped.append("IPv6 loopback unavailable on host")
        else:
            with RunningServer(("--host", "::1")) as ipv6:
                check(ipv6.request("GET", "/health")[0] == 200, "IPv6 listener and peer transport")
        fixture = Path(__file__).parent / "fixtures"
        certificate = root / "証明書.pem"
        private_key = root / "秘密鍵.pem"
        certificate.write_bytes((fixture / "localhost-cert.pem").read_bytes())
        private_key.write_bytes((fixture / "localhost-key.pem").read_bytes())
        config.write_text(with_settings(
            base_config, "NativeServer", TlsCertificate=certificate.name, TlsPrivateKey=private_key.name,
        ), encoding="utf-8")
        if args.http_only:
            skipped.append("TLS not selected for explicit HTTP-only coverage")
        elif features["tls"]:
            with RunningServer() as secure:
                try:
                    check(
                        secure.uri.scheme == "https"
                        and secure.login(credentials)["message"].startswith("Error connecting"),
                        "HTTPS login with certificate verification",
                    )
                except ssl.SSLCertVerificationError as error:
                    fixture_der = ssl.PEM_cert_to_DER_cert((fixture / "localhost-cert.pem").read_text(encoding="ascii"))
                    expected_fingerprint = hashlib.sha256(fixture_der).hexdigest()
                    report = {"passed": False, "checksBeforeFailure": len(checks), "features": features,
                              "tlsVerificationError": str(error), "observations": checks,
                              "expectedPeerSha256": expected_fingerprint}
                    # Collect the presented certificate without sending an HTTP request or credentials.
                    diagnostic = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                    diagnostic.check_hostname = False
                    diagnostic.verify_mode = ssl.CERT_NONE
                    try:
                        with socket.create_connection((secure.uri.hostname, secure.uri.port), timeout=8) as raw:
                            with diagnostic.wrap_socket(raw, server_hostname=secure.uri.hostname) as peer:
                                fingerprint = hashlib.sha256(peer.getpeercert(binary_form=True)).hexdigest()
                        report["observedPeerSha256"] = fingerprint
                        report["peerMatchesFixture"] = fingerprint == expected_fingerprint
                    except OSError as diagnostic_error:
                        report["peerDiagnosticError"] = str(diagnostic_error)
                    (args.work / "network-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                    raise
        else:
            result = cli(expected=1)
            check("TLS requested" in result.stderr, "TLS config cannot silently fall back to plaintext")
            skipped.append("TLS support not compiled")
        config.write_text(with_settings(with_settings(base_config, "UserAccountService", ConnectionString="untouched-accounts.db"), "AuthenticationService", ConnectionString="untouched-auth.db"), encoding="utf-8")
        cli("--init-db", expected=1)
        check(
            not (root / "untouched-accounts.db").exists()
            and not (root / "untouched-auth.db").exists(),
            "existing service database rejected before account/auth initialization writes",
        )
        # Both spellings run through real startup, account provisioning and login.
        # In the bundled provider a comma belongs to the filename, while a
        # semicolon separates properties (quoted here through the INI layer).
        for tag, account_setting, auth_setting, account_file, auth_file in [
            ("comma", "URI=file:accounts.db,version=3", "URI=file:auth.db,version=3", "accounts.db,version=3", "auth.db,version=3"),
            ("semicolon", '"DataSource=accounts-options.db;URI=invalid;Version=+3"', '"Data_Source=auth-options.db;Pooling=off;Version=03"', "accounts-options.db", "auth-options.db")]:
            config.write_text(with_settings(with_settings(with_settings(base_config, "UserAccountService", ConnectionString=account_setting), "AuthenticationService", ConnectionString=auth_setting), "NativeServer", GridUserDatabase=f"separate-grid-user-{tag}.db", GridRegionDatabase=f"separate-regions-{tag}.db"), encoding="utf-8")
            cli("--init-db")
            cli("--create-user", "Separate", "User", password="separate-password\n")
            with RunningServer() as separated:
                check(separated.login({"first": "Separate", "last": "User", "passwd": "separate-password"})["message"].startswith("Error connecting"),
                      "separate account/auth SQLite ConnectionStrings " + tag)
            with contextlib.closing(sqlite3.connect(root / auth_file)) as connection:
                check(
                    connection.execute("SELECT count(*) FROM tokens").fetchone()[0] > 0,
                    "authentication tokens persist in selected auth file " + tag,
                )
                check(
                    connection.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("AuthStore",2),("migrations",1)],
                    "authentication migrations select independent file "+tag,
                )
            with contextlib.closing(sqlite3.connect(root / account_file)) as connection:
                check(connection.execute("SELECT count(*) FROM UserAccounts").fetchone()[0] == 1, "account persists in selected account file " + tag)
        check(
            not (root / "accounts.db").exists()
            and not (root / "auth.db").exists(),
            "comma connection strings do not silently redirect to truncated filenames",
        )
        # The real executable must use the configured account realm after the
        # original provider has already reached its migration revision.
        with contextlib.closing(sqlite3.connect(root/"accounts-options.db")) as connection, connection:
            source_id=connection.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Separate'").fetchone()[0]
            connection.execute("ALTER TABLE UserAccounts RENAME TO AccountRealm")
        with contextlib.closing(sqlite3.connect(root/"auth-options.db")) as connection, connection:
            connection.execute("ALTER TABLE auth RENAME TO AuthRealm")
        option_config=config.read_text(encoding="utf-8")
        config.write_text(with_settings(with_settings(option_config, "UserAccountService", Realm="AccountRealm"), "AuthenticationService", Realm="AuthRealm"),encoding="utf-8")
        cli("--create-user","Realm","User",password="realm-password\n")
        with RunningServer() as realm_server:
            check(
                realm_server.login({"first":"Realm","last":"User","passwd":"realm-password"})["message"].startswith("Error connecting"),
                "configured account and authentication realms support creation and login without default tables",
            )
        with contextlib.closing(sqlite3.connect(root/"accounts-options.db")) as connection:
            check(connection.execute("SELECT count(*) FROM AccountRealm").fetchone()[0]==2,"account writes use configured realm")
        legacy=root/"legacy-accounts.db"
        with contextlib.closing(sqlite3.connect(legacy)) as connection, connection:
            connection.execute("CREATE TABLE users(UUID,username,surname,created)")
            connection.execute("INSERT INTO users VALUES(?,?,?,?)",(source_id,"Legacy","User",111))
        legacy_auth=root/"legacy-auth.db"
        with contextlib.closing(sqlite3.connect(root/"auth-options.db")) as connection:
            credentials_row=connection.execute("SELECT UUID,passwordHash,passwordSalt,webLoginKey FROM AuthRealm WHERE UUID=?",(source_id,)).fetchone()
        with contextlib.closing(sqlite3.connect(legacy_auth)) as connection, connection:
            connection.execute("CREATE TABLE users(UUID,passwordHash,passwordSalt,webLoginKey)")
            connection.execute("INSERT INTO users VALUES(?,?,?,?)",credentials_row)
        config.write_text(option_config.replace("accounts-options.db","legacy-accounts.db").replace("auth-options.db","legacy-auth.db"),encoding="utf-8")
        for attempt in range(2):
            with RunningServer() as legacy_server:
                check(
                    legacy_server.login({"first":"Legacy","last":"User","passwd":"separate-password"})["message"].startswith("Error connecting"),
                    "legacy account import supports authenticated login at restart "+str(attempt),
                )
            with contextlib.closing(sqlite3.connect(legacy)) as connection, connection:
                check(
                    connection.execute("SELECT PrincipalID,FirstName,LastName,Created,active FROM UserAccounts").fetchall()==[(source_id,"Legacy","User",111,1)],
                    "legacy user migration preserves identity, metadata and v3 active default at restart "+str(attempt),
                )
                check(
                    connection.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("UserAccount",3),("migrations",1)],
                    "account restart preserves migration versions without duplicate stamps "+str(attempt),
                )
                if attempt==0:connection.execute("UPDATE users SET username='Changed',created=222")
            with contextlib.closing(sqlite3.connect(legacy_auth)) as connection, connection:
                check(
                    connection.execute("SELECT name,version FROM migrations ORDER BY name").fetchall()==[("AuthStore",2),("migrations",1)],
                    "legacy authentication migration versions survive restart "+str(attempt),
                )
                check(
                    connection.execute("SELECT UUID,passwordHash,passwordSalt FROM auth").fetchall()==[credentials_row[:3]],
                    "legacy credentials migrate without reimport on restart "+str(attempt),
                )
                if attempt==0:connection.execute("UPDATE users SET passwordHash='changed',passwordSalt='changed'")

    report = {
        "passed": True,
        "checks": len(checks),
        "features": features,
        "httpOnly": args.http_only,
        "skipped": skipped,
        "observations": checks,
        "regionSchemaSourceHash": hashlib.sha256(grid_migration.read_bytes()).hexdigest(),
    }
    (args.work / "network-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(checks)} native process/network/persistence observations; skipped: {skipped}")


if __name__ == "__main__":
    main()
