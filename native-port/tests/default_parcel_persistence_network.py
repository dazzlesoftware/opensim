"""Verify SQLite persistence for generated parcel edits and large terrain uploads."""

import argparse
from contextlib import closing
import gzip
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
from threading import Thread
import time
import uuid
from urllib.error import HTTPError
from urllib.parse import parse_qs
from urllib.request import Request, urlopen
from xml.etree import ElementTree
from xmlrpc.client import Fault, ServerProxy, dumps, loads

from remote_presence_network import running_server, wait_until


def upload_large_terrain(send, receive, owner, session_id, dimension):
    filename = b"large.raw\0"
    command = (owner.bytes + session_id + bytes(16) + b"\x08terrain\0" + uuid.uuid4().bytes +
               b"\x02\x10upload filename\0" + bytes([len(filename)]) + filename)
    send("ffff0104", command)
    deadline = time.monotonic() + 90

    def matching_packet(predicate):
        while time.monotonic() < deadline:
            payload = receive()
            if predicate(payload):
                return payload
        raise AssertionError("Timed out waiting for terrain transfer")

    request = matching_packet(lambda packet: packet[:4] == bytes.fromhex("ffff009c"))
    transfer_id = struct.unpack_from("<Q", request, 4)[0]
    sample = bytes((21, 128, 20, 0, 0, 0, 0, 255, 255, 255, 255, 21, 128))
    terrain = bytearray(sample * (dimension * dimension))
    assert len(terrain) > 64 * 1024 * 1024
    edited_x, edited_y = dimension - 2, dimension - 3
    offset = 13 * ((dimension - 1 - edited_y) * dimension + edited_x)
    terrain[offset:offset + 2] = bytes((64, 128))
    terrain[offset + 11:offset + 13] = bytes((64, 128))
    loaded = False
    # Standard viewer-sized chunks are covered by the 512-meter round trip.
    chunk_size = 32 * 1024
    for index, start in enumerate(range(0, len(terrain), chunk_size)):
        chunk = terrain[start:start + chunk_size]
        packet_id = index | (0x80000000 if start + len(chunk) == len(terrain) else 0)
        if index == 0:
            chunk = struct.pack("<I", len(terrain)) + chunk
        send("12", struct.pack("<QIH", transfer_id, packet_id, len(chunk)) + chunk)

        def confirmed(payload):
            nonlocal loaded
            loaded |= b"Your terrain was loaded as a .raw file" in payload
            return (len(payload) >= 13 and payload[0] == 19 and
                    struct.unpack_from("<QI", payload, 1) == (transfer_id, packet_id))

        matching_packet(confirmed)
    if not loaded:
        matching_packet(lambda payload: b"Your terrain was loaded as a .raw file" in payload)
    return edited_x, edited_y


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--large-terrain", action="store_true")
    parser.add_argument("--dwell", action="store_true")
    parser.add_argument("--force-owner", action="store_true")
    parser.add_argument("--disable-materials", action="store_true")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work) as directory:
        root = Path(directory)
        config = root / "region.ini"
        dimension = 2304 if args.large_terrain else 256
        settings = (
            "[NativeServer]\nEnableLocalRegion=true\nListenAddress=127.0.0.1\n"
            "UserAccountDatabase=users.db\nAuthenticationDatabase=users.db\n"
            "InventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db\n"
            "[Network]\nhttp_listener_port=0\n"
            "[NativeRegion]\nName=Default Parcel Fixture\nUDPPort=0\n"
            "BindAddress=127.0.0.1\nExternalHost=127.0.0.1\nTerrainDatabase=terrain.db\n"
            f"SizeX={dimension}\nSizeY={dimension}\n"
            "[Terrain]\nInitialTerrain=flat\n"
        )
        if args.dwell:
            settings += "[Dwell]\nDwellModule=DefaultDwellModule\n"
        if args.force_owner:
            materials_enabled = "false" if args.disable_materials else "true"
            settings += (
                "[Permissions]\nimplicit_gods=true\n[Modules]\nLandServiceInConnector=true\n"
                "[Groups]\nEnabled=true\nServicesConnectorModule=Groups Local Service Connector\n"
                "SQLiteMembershipDatabase=groups.db\n"
                f"[Materials]\nenable_materials={materials_enabled}\nMaxMaterialsPerTransaction=73\n"
                "[DataSnapshot]\nindex_sims=true\ndefault_snapshot_period=37\ngridname=Fixture & Grid\n"
                "snapshot_cache_directory=collector-cache\n"
            )
            with closing(sqlite3.connect(root / "groups.db")) as database:
                database.executescript(
                    "CREATE TABLE os_groups_groups (GroupID TEXT, Name TEXT);"
                    "CREATE TABLE os_groups_roles (GroupID TEXT, RoleID TEXT, Powers TEXT);"
                    "CREATE TABLE os_groups_membership (PrincipalID TEXT, GroupID TEXT, SelectedRoleID TEXT);"
                )
        config.write_text(settings, encoding="utf-8")
        command = [args.native, "--config", str(config)]
        subprocess.run(command + ["--init-db"], check=True, timeout=20)
        for last in ("Owner", "Visitor"):
            subprocess.run(command + ["--create-user", "Parcel", last],
                           input="fixture-password\n", text=True, check=True, timeout=20)
        with closing(sqlite3.connect(root / "users.db")) as database:
            owner, visitor = (
                uuid.UUID(database.execute(
                    "SELECT PrincipalID FROM UserAccounts WHERE LastName=?", (last,)
                ).fetchone()[0])
                for last in ("Owner", "Visitor")
            )
        settings = settings.replace("[NativeRegion]", f"[NativeRegion]\nOwnerID={owner}")
        if args.force_owner:
            settings = settings.replace(
                "[DataSnapshot]",
                f"[LandManagement]\nDefaultAdministratorOwnerUUID={owner}\n"
                "DefaultAdministratorParcelName=Public Content Fixture\n[DataSnapshot]",
            )
        config.write_text(settings, encoding="utf-8")

        def login(proxy, last):
            return proxy.login_to_simulator({
                "first": "Parcel", "last": last, "passwd": "fixture-password",
                "start": "uri:Default Parcel Fixture&128&128&25", "version": "fixture",
            })

        with running_server(args.native, config, region=True) as (url, endpoint):
            if args.force_owner:
                with ServerProxy(url + "/landtool.php") as landtool:
                    assert landtool.preflightBuyLandPrep({}) == {
                        "success": True,
                        "currency": {"estimatedCost": 0},
                        "membership": {},
                        "landuse": {"upgrade": False, "action": "http://invaliddomaininvalid.com/"},
                        "confirm": "asdfajsdkfjasdkfjalsdfjasdf",
                    }
                    assert landtool.buyLandPrep({}) == {"success": True}
                    try:
                        landtool.unknownMethod()
                    except Fault as fault:
                        assert (fault.faultCode, fault.faultString) == (
                            -32601, "Requested method [unknownMethod] not found"
                        )
                    else:
                        raise AssertionError("Unknown economy method did not return an XML-RPC fault")
                with ServerProxy(url + "/currency.php") as currency:
                    assert currency.buyCurrency({}) == {"success": True}
                    assert currency.getCurrencyQuote({"currencyBuy": "25"})["currency"]["currencyBuy"] == 0
                quote = dumps(({"currencyBuy": 25},), methodname="getCurrencyQuote").encode()
                request = Request(
                    url + "/currency.php", gzip.compress(quote),
                    {"Content-Type": "text/xml", "X-Content-Encoding": "gzip"},
                )
                with urlopen(request, timeout=5) as response:
                    assert response.headers.get_content_type() == "text/xml"
                    assert response.headers["Access-Control-Allow-Origin"] == "*"
                    assert loads(response.read())[0][0] == {
                        "success": True,
                        "currency": {"estimatedCost": 0, "currencyBuy": 25},
                        "confirm": "asdfad9fj39ma9fj",
                    }
                for body, headers in (
                    (b"<bad/>", {"Content-Type": "text/xml"}),
                    (quote, {"Content-Type": "text/xml", "X-Content-Encoding": "gzip"}),
                ):
                    try:
                        urlopen(Request(url + "/landtool.php", body, headers), timeout=5)
                    except HTTPError as failure:
                        assert failure.code == 404
                        failure.close()
                    else:
                        raise AssertionError("Malformed economy XML-RPC request did not return 404")
                collector = url + "/?method=collector"
                with urlopen(collector, timeout=5) as response:
                    snapshot = ElementTree.fromstring(response.read())
                    assert response.headers.get_content_type() == "text/xml"
                assert snapshot.findtext("expire") == "37"
                assert snapshot.findtext("region/grid/name") == "Fixture & Grid"
                assert snapshot.find("region/data/objectdata") is not None
                assert snapshot.find("region/data/parceldata") is not None
                assert snapshot.find("region/data/parceldata/parcel") is None
                estate = snapshot.find("region/data/estate")
                assert estate.findtext("name") == "My Estate"
                assert estate.findtext("user/name") == "Parcel Owner"
                assert estate.findtext("user/uuid") == str(owner)
                assert estate.find("flags").attrib == {"teleport": "True", "public": "True"}
                cache_directory = root / "collector-cache"
                for suffix, tag in (("", "region"), ("_EstateSnapshot", "estate"),
                                    ("_ObjectSnapshot", "objectdata"), ("_LandSnapshot", "parceldata")):
                    path = cache_directory / f"Default Parcel Fixture{suffix}.xml"
                    assert ElementTree.parse(path).getroot().tag == tag
                with urlopen(collector + "&region=Default+Parcel+Fixture", timeout=5) as response:
                    named = ElementTree.fromstring(response.read())
                assert named.find("expire") is None
                assert named.findtext("region/info/name") == "Default Parcel Fixture"
                try:
                    urlopen(collector, timeout=5)
                except HTTPError as failure:
                    assert failure.code == 503
                    failure.close()
                else:
                    raise AssertionError("Repeated collector requests should be throttled")
            with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                session = login(proxy, "Owner")
                assert session.get("login") == "true", session
                session_id = uuid.UUID(session["session_id"]).bytes
                code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)

                sequence = 0

                def send(packet_id, payload, reliable=True):
                    nonlocal sequence
                    sequence += 1
                    viewer.sendto(struct.pack(">BIB", 0x40 if reliable else 0, sequence, 0) +
                                  bytes.fromhex(packet_id) + payload, endpoint)

                def receive():
                    datagram = viewer.recv(8192)
                    if datagram[0] & 0x40:
                        packet_sequence = struct.unpack_from(">I", datagram, 1)[0]
                        send("fffffffb", struct.pack("<BI", 1, packet_sequence), reliable=False)
                    payload = datagram[6 + datagram[5]:]
                    if not datagram[0] & 0x80:
                        return payload
                    decoded = bytearray()
                    index = 0
                    while index < len(payload):
                        value = payload[index]
                        index += 1
                        if value:
                            decoded.append(value)
                        elif index < len(payload):
                            decoded.extend(bytes(payload[index]))
                            index += 1
                    return bytes(decoded)

                send("ffff0003", code + session_id + owner.bytes)
                viewer.settimeout(5)
                for _ in range(128):
                    if receive()[:4] == bytes.fromhex("ffff0094"):
                        break
                else:
                    raise AssertionError("Viewer did not receive a region handshake")
                if args.force_owner:
                    def parcel_info(x, y, agent=owner):
                        location = struct.pack("<QHHHH", 1099511628032000, x, 0, y, 0)
                        send("ffff0036", agent.bytes + session_id + location)
                        for _ in range(256):
                            payload = receive()
                            if payload[:4] == bytes.fromhex("ffff0037") and payload[20:36] == location:
                                offset = 53 + payload[52]
                                flags = payload[offset + 1 + payload[offset] + 8]
                                return uuid.UUID(bytes=payload[36:52]), flags
                        raise AssertionError("The viewer did not receive parcel information")

                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    assert parcel_info(128, 128)[0] == parcel_info(129, 129)[0] == owner
                    seed_request = (
                        b"<llsd><array><string>ParcelPropertiesUpdate</string>"
                        b"<string>SimulatorFeatures</string><string>RenderMaterials</string>"
                        b"<string>ModifyMaterialParams</string></array></llsd>"
                    )
                    llsd_headers = {"Content-Type": "application/llsd+xml"}
                    with urlopen(Request(session["seed_capability"], seed_request, llsd_headers), timeout=5) as response:
                        entries = list(ElementTree.fromstring(response.read()).find("map"))
                    caps = {key.text: value.text for key, value in zip(entries[::2], entries[1::2])}
                    update_url = caps["ParcelPropertiesUpdate"]
                    with urlopen(caps["SimulatorFeatures"], timeout=5) as response:
                        features = list(ElementTree.fromstring(response.read()).find("map"))
                    feature_values = {key.text: value for key, value in zip(features[::2], features[1::2])}
                    if args.disable_materials:
                        assert "RenderMaterials" not in caps and "ModifyMaterialParams" not in caps
                        assert "RenderMaterialsCapability" not in feature_values
                        assert "MaxMaterialsPerTransaction" not in feature_values
                    else:
                        assert caps["RenderMaterials"] and caps["ModifyMaterialParams"]
                        assert feature_values["RenderMaterialsCapability"].text == "4"
                        assert feature_values["MaxMaterialsPerTransaction"].text == "73"
                    extras = next(value for key, value in zip(features[::2], features[1::2])
                                  if key.text == "OpenSimExtras")
                    currency_uri = next(value.text for key, value in zip(extras[::2], extras[1::2])
                                        if key.text == "currency-base-uri")
                    assert currency_uri == url + "/"
                    update = (
                        b"<llsd><map><key>local_id</key><integer>1</integer>"
                        b"<key>name</key><string>Capability Edit</string>"
                        b"<key>media_url</key><string>https://video.example/parcel</string>"
                        b"<key>media_type</key><string>video/mp4</string>"
                        b"<key>media_width</key><integer>640</integer>"
                        b"<key>media_height</key><integer>360</integer>"
                        b"<key>parcel_flags</key><binary>AAAAAA==</binary></map></llsd>"
                    )
                    with urlopen(Request(update_url, update, llsd_headers), timeout=5) as response:
                        assert response.status == 200
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        assert database.execute("SELECT Name,MediaURL,MediaType,MediaSize FROM land").fetchone() == (
                            "Capability Edit", "https://video.example/parcel", "video/mp4", "640,360"
                        )

                    def field(value):
                        encoded = value.encode() + b"\0"
                        return bytes([len(encoded)]) + encoded

                    parcel_update = (
                        struct.pack("<iIIi", 1, 0, 0, 0) + field("UDP Edit") + field("Updated description")
                        + field("https://audio.example/parcel") + field("") + bytes(16) + bytes(1)
                        + bytes(16) + struct.pack("<ifB", 0, 0, 0) + bytes(32) + struct.pack("<6fB", *([0] * 7))
                    )
                    send("ffff00c6", owner.bytes + session_id + parcel_update)
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        wait_until(
                            lambda: database.execute("SELECT Name,Desc,MusicURL,SeeAVs FROM land").fetchone()
                            == ("UDP Edit", "Updated description", "https://audio.example/parcel", 0),
                            "UDP parcel edit was not saved",
                        )
                    # The legacy packet has no privacy fields. Restore the capability defaults
                    # before checking the unrelated ownership overlay below.
                    with urlopen(Request(update_url, update, llsd_headers), timeout=5) as response:
                        assert response.status == 200
                    banned_agent = uuid.uuid4()
                    access_header = struct.pack("<Ii16siiB", 2, 1, uuid.uuid4().bytes, 1, 1, 1)
                    access_entry = struct.pack("<16siI", banned_agent.bytes, 0, 2)
                    send("ffff00d9", owner.bytes + session_id + access_header + access_entry)
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        wait_until(
                            lambda: database.execute(
                                "SELECT COUNT(*) FROM landaccesslist WHERE AccessUUID=?",
                                (str(banned_agent),),
                            ).fetchone() == (1,),
                            "Parcel ban was not saved before subdivision",
                        )
                    send("ffff00d3", owner.bytes + session_id + struct.pack("<4f", 0, 0, 64, 64))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def split_was_saved():
                            rows = database.execute(
                                "SELECT Area, LandingType, Name, Bitmap FROM land ORDER BY LocalLandID"
                            ).fetchall()
                            if len(rows) != 2:
                                return False
                            original, cut = rows
                            assert original[:3] == (61440, 0, "Capability Edit")
                            assert cut[:3] == (4096, 2, "Capability Edit")
                            assert sum(byte.bit_count() for byte in cut[3]) == 256
                            assert all((a & b) == 0 and (a | b) == 255 for a, b in zip(original[3], cut[3]))
                            assert database.execute(
                                "SELECT COUNT(*) FROM landaccesslist WHERE AccessUUID=?",
                                (str(banned_agent),),
                            ).fetchone() == (2,)
                            return True

                        wait_until(split_was_saved, "Parcel divide did not save both bitmaps")
                    # The join samples from (1, 1) in four-meter steps. Its exclusive
                    # northeast corner is outside the region, but every sample is inside.
                    send("ffff00d2", owner.bytes + session_id + struct.pack("<4f", 1, 1, 257, 257))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        wait_until(
                            lambda: database.execute(
                                "SELECT LocalLandID,Area,Bitmap FROM land"
                            ).fetchall() == [(1, 65536, bytes([255]) * 512)],
                            "Parcel join did not restore the original parcel boundary",
                        )
                        assert database.execute(
                            "SELECT COUNT(*) FROM landaccesslist WHERE AccessUUID=?",
                            (str(banned_agent),),
                        ).fetchone() == (1,)
                    send("ffff00c8", owner.bytes + session_id + struct.pack("<ii", 1, 45))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def clean_time_was_saved():
                            return database.execute("SELECT OtherCleanTime FROM land").fetchone() == (45,)

                        wait_until(clean_time_was_saved, "Generated parcel clean time was not saved")
                    send("ffff00d6", owner.bytes + session_id + visitor.bytes + struct.pack("<i", 1))
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff00c4") and payload[10] == 0xc1:
                            break
                    else:
                        raise AssertionError("The old owner did not receive an updated parcel overlay")
                    assert proxy.land_data({"region_handle": "1099511628032000", "x": "128", "y": "128"})[
                        "OwnerID"] == str(visitor)
                    assert parcel_info(128, 128)[0] == parcel_info(129, 129)[0] == visitor
                elif args.dwell:
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    deadline = time.monotonic() + 165
                    viewer.settimeout(1)
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        assert database.execute("SELECT COUNT(*) FROM land").fetchone() == (0,)
                        while time.monotonic() < deadline:
                            send("ffff00da", owner.bytes + session_id + struct.pack("<i16s", 1, bytes(16)))
                            until = time.monotonic() + 1
                            while time.monotonic() < until:
                                try:
                                    receive()
                                except TimeoutError:
                                    break
                            row = database.execute("SELECT Bitmap,Dwell FROM land").fetchone()
                            if row:
                                bitmap, saved_dwell = row
                                assert bitmap == bytes([255]) * 512 and saved_dwell > 0
                                break
                        else:
                            raise AssertionError("Dwell did not create the generated parcel row")
                elif args.large_terrain:
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    edited_x, edited_y = upload_large_terrain(send, receive, owner, session_id, dimension)
                else:
                    edit = struct.pack("<Ii16siiB", 2, 1, uuid.uuid4().bytes, 1, 1, 1)
                    entry = struct.pack("<16siI", visitor.bytes, 0, 2)
                    send("ffff00d9", owner.bytes + session_id + edit + entry)
                    send("ffff00d7", owner.bytes + session_id + struct.pack("<iIi", 0, 2, 1))
                    for _ in range(128):
                        reply = receive()
                        if reply[:4] == bytes.fromhex("ffff00d8") and visitor.bytes in reply:
                            break
                    else:
                        raise AssertionError("Viewer did not receive the saved ban")

        if args.force_owner:
            sale_flags = 4 | 128 | 65536 | 4096
            group = uuid.uuid4()
            with closing(sqlite3.connect(root / "terrain.db")) as database:
                assert database.execute("SELECT OwnerUUID,GroupUUID FROM land").fetchone() == (
                    str(visitor), str(uuid.UUID(int=0)))
            with running_server(args.native, config, region=True) as (url, _), ServerProxy(url + "/") as proxy:
                assert proxy.land_data({"region_handle": "1099511628032000", "x": "128", "y": "128"})[
                    "OwnerID"] == str(visitor)
            with closing(sqlite3.connect(root / "terrain.db")) as database, database:
                database.execute("UPDATE land SET OwnerUUID=?, GroupUUID=?, IsGroupOwned='True', "
                                 "LandFlags=CAST(LandFlags AS INTEGER)|?", (str(group), str(group), sale_flags))
            with closing(sqlite3.connect(root / "groups.db")) as database, database:
                database.execute("INSERT INTO os_groups_groups VALUES (?, 'Parcel Group')", (str(group),))

            announcements = []
            group_record_requests = []

            class GroupRecordHandler(BaseHTTPRequestHandler):
                def do_GET(self):
                    target, _, query = self.path.partition("?")
                    fields = parse_qs(query)
                    if target not in ("/collector", "/extra") or fields.get("fixture") != ["yes"]:
                        self.send_error(400)
                        return
                    secret = fields["secret"][0]
                    callback = f'http://{fields["host"][0]}:{fields["port"][0]}/?method=validate&secret={secret}'
                    with urlopen(callback, timeout=4) as response:
                        validated = response.status == 200 and response.headers.get_content_type() == "text/xml"
                    announcements.append((target, fields["service"][0], secret, validated))
                    self.send_response(200)
                    self.send_header("Content-Length", "0")
                    self.end_headers()

                def do_POST(self):
                    fields = parse_qs(self.rfile.read(int(self.headers["Content-Length"])).decode())
                    if self.path != "/groups" or fields != {
                        "METHOD": ["GETGROUP"], "GroupID": [str(group)],
                        "RequestingAgentID": [str(uuid.UUID(int=0))],
                    }:
                        self.send_error(400)
                        return
                    group_record_requests.append(fields)
                    name = b"Remote &amp; Group" if len(group_record_requests) == 1 else b"Refreshed Group"
                    body = (b'<ServerResponse><RESULT type="List"><GroupName>' + name +
                            b'</GroupName></RESULT></ServerResponse>')
                    self.send_response(200)
                    self.send_header("Content-Type", "text/xml")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)

                def log_message(self, *args):
                    pass

            with ThreadingHTTPServer(("127.0.0.1", 0), GroupRecordHandler) as groups_server:
                thread = Thread(target=groups_server.serve_forever, daemon=True)
                thread.start()
                try:
                    remote_config = root / "remote-groups.ini"
                    collector_root = f"http://127.0.0.1:{groups_server.server_port}"
                    remote_config.write_text(config.read_text(encoding="utf-8").replace(
                        "ServicesConnectorModule=Groups Local Service Connector\nSQLiteMembershipDatabase=groups.db",
                        "ServicesConnectorModule=Groups Remote Service Connector\n"
                        f"GroupsServerURI=http://127.0.0.1:{groups_server.server_port}",
                    ).replace("default_snapshot_period=37", "default_snapshot_period=1").replace(
                        "snapshot_cache_directory=collector-cache", "snapshot_cache_directory=remote-cache") +
                        f"data_services={collector_root}/collector?fixture=yes\n"
                        f"DATA_SRV_EXTRA={collector_root}/extra?fixture=yes\n", encoding="utf-8")
                    with running_server(args.native, remote_config, region=True) as (url, _):
                        wait_until(lambda: len(announcements) == 2, "DataSnapshot did not register with both collectors")
                        try:
                            urlopen(url + "/?method=validate&secret=invalid", timeout=5)
                        except HTTPError as failure:
                            assert failure.code == 403
                            failure.close()
                        else:
                            raise AssertionError("DataSnapshot accepted an invalid validation secret")
                        with urlopen(url + "/?method=collector", timeout=5) as response:
                            remote_parcel = ElementTree.fromstring(response.read()).find("region/data/parceldata/parcel")
                        assert remote_parcel.findtext("group/groupname") == "Remote & Group"
                        assert remote_parcel.findtext("owner/name") == "Remote & Group"
                        fragment_paths = [
                            root / "remote-cache" / f"Default Parcel Fixture_{name}Snapshot.xml"
                            for name in ("Estate", "Object", "Land")
                        ]
                        original_times = [path.stat().st_mtime_ns for path in fragment_paths]
                        time.sleep(1.1)
                        with urlopen(url + "/?method=collector&region=Default+Parcel+Fixture", timeout=5) as response:
                            refreshed = ElementTree.fromstring(response.read()).find("region/data/parceldata/parcel")
                        assert refreshed.findtext("group/groupname") == "Refreshed Group"
                        refreshed_times = [path.stat().st_mtime_ns for path in fragment_paths]
                        assert refreshed_times[:2] == original_times[:2]
                        assert refreshed_times[2] > original_times[2]
                    assert [(target, status) for target, status, _, _ in announcements] == [
                        ("/extra", "online"), ("/collector", "online"),
                        ("/extra", "offline"), ("/collector", "offline"),
                    ]
                    assert all(validated for _, _, _, validated in announcements)
                    assert len({uuid.UUID(secret) for _, _, secret, _ in announcements}) == 1
                finally:
                    groups_server.shutdown()
                    thread.join()

            with running_server(args.native, config, region=True) as (url, endpoint):
                with urlopen(url + "/?method=collector", timeout=5) as response:
                    snapshot = ElementTree.fromstring(response.read())
                listed = snapshot.find("region/data/parceldata/parcel")
                assert listed is not None
                assert listed.attrib["showinsearch"] == listed.attrib["forsale"] == "true"
                assert listed.findtext("owner/uuid") == listed.findtext("group/groupuuid") == str(group)
                assert listed.findtext("owner/name") == listed.findtext("group/groupname") == "Parcel Group"
                assert uuid.UUID(listed.findtext("infouuid")).int != 0
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Owner")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + owner.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    assert parcel_info(128, 128) == (group, 0x84)
                    send("ffff00d6", owner.bytes + session_id + visitor.bytes + struct.pack("<i", 1))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def owner_was_saved():
                            return database.execute("SELECT OwnerUUID FROM land").fetchone() == (str(visitor),)

                        wait_until(owner_was_saved, "Forced parcel owner was not saved")
                        flags, group_id, is_group = database.execute(
                            "SELECT CAST(LandFlags AS INTEGER), GroupUUID, IsGroupOwned FROM land"
                        ).fetchone()
                        assert not flags & sale_flags and group_id == str(uuid.UUID(int=0))
                        assert str(is_group).lower() in ("false", "0")
                    assert parcel_info(128, 128) == (visitor, 0)
                    (cache_directory / "Default Parcel Fixture.xml").unlink()
                    with urlopen(url + "/?method=collector&region=Default+Parcel+Fixture", timeout=5) as response:
                        cached = ElementTree.fromstring(response.read()).find("region/data/parceldata/parcel")
                    assert cached.findtext("owner/uuid") == str(group)
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Visitor")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + visitor.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", visitor.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    send("ffff00d4", visitor.bytes + session_id + struct.pack("<i", 1))
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff00c4") and payload[10] == 0xc1:
                            break
                    else:
                        raise AssertionError("The abandoning owner did not receive an updated parcel overlay")
                    assert proxy.land_data({"region_handle": "1099511628032000", "x": "128", "y": "128"})[
                        "OwnerID"] == str(owner)
            with closing(sqlite3.connect(root / "terrain.db")) as database, database:
                assert database.execute("SELECT OwnerUUID FROM land").fetchone() == (str(owner),)
                columns = {row[1].lower() for row in database.execute("PRAGMA table_info(land)")}
                for column, kind in (("AuthbuyerID", "TEXT"), ("AnyAVSounds", "INTEGER"), ("GroupAVSounds", "INTEGER")):
                    if column.lower() not in columns:
                        database.execute(f"ALTER TABLE land ADD COLUMN {column} {kind}")
                database.execute(
                    "UPDATE land SET OwnerUUID=?, ClaimDate=1, SalePrice=123, AuthbuyerID=?, "
                    "SeeAVs=0, AnyAVSounds=0, GroupAVSounds=0, LandFlags=CAST(LandFlags AS INTEGER)|?",
                    (str(visitor), str(visitor), sale_flags),
                )
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Owner")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + owner.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    assert parcel_info(128, 128) == (visitor, 0x80)
                    send("ffff00d0", owner.bytes + session_id + struct.pack("<i", 1))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def reclaim_was_saved():
                            return database.execute("SELECT OwnerUUID FROM land").fetchone() == (str(owner),)

                        wait_until(reclaim_was_saved, "Reclaimed parcel was not saved")
                        claim_date, price, buyer, see, sounds, group_sounds, flags = database.execute(
                            "SELECT ClaimDate, SalePrice, AuthbuyerID, SeeAVs, AnyAVSounds, GroupAVSounds, "
                            "CAST(LandFlags AS INTEGER) FROM land"
                        ).fetchone()
                        assert abs(int(time.time()) - int(claim_date)) < 10
                        assert (int(price), buyer, int(see), int(sounds), int(group_sounds)) == (
                            0, str(uuid.UUID(int=0)), 1, 1, 1)
                        assert not flags & sale_flags
                    assert parcel_info(128, 128) == (owner, 0)
            role = str(uuid.uuid4())
            with closing(sqlite3.connect(root / "groups.db")) as database, database:
                database.execute("INSERT INTO os_groups_roles VALUES (?, ?, '0')", (str(group), role))
                database.execute("INSERT INTO os_groups_membership VALUES (?, ?, ?)",
                                 (str(visitor), str(group), role))
            with closing(sqlite3.connect(root / "terrain.db")) as database, database:
                database.execute(
                    "UPDATE land SET OwnerUUID=?, GroupUUID=?, LandFlags=CAST(LandFlags AS INTEGER)|?",
                    (str(visitor), str(group), sale_flags),
                )
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Visitor")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + visitor.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", visitor.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    deed = visitor.bytes + session_id + group.bytes + struct.pack("<i", 1)
                    send("ffff00cf", deed)
                    # A parcel info request on the same circuit checks that the first deed was denied.
                    assert parcel_info(128, 128, visitor)[0] == visitor
                    with closing(sqlite3.connect(root / "groups.db")) as database, database:
                        database.execute("UPDATE os_groups_roles SET Powers='4096' WHERE RoleID=?", (role,))
                    send("ffff00cf", deed)
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff00c4") and payload[10] == 0xc2:
                            break
                    else:
                        raise AssertionError("The group member did not receive the deeded parcel overlay")
                    assert parcel_info(128, 128, visitor)[0] == group
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        owner_id, group_id, is_group, flags = database.execute(
                            "SELECT OwnerUUID, GroupUUID, IsGroupOwned, CAST(LandFlags AS INTEGER) FROM land"
                        ).fetchone()
                        assert owner_id == group_id == str(group)
                        assert str(is_group).lower() in ("true", "1")
                        assert not flags & sale_flags
                        assert database.execute("SELECT OtherCleanTime FROM land").fetchone() == (45,)

                    send("ffff00c8", visitor.bytes + session_id + struct.pack("<ii", 1, 90))
                    assert parcel_info(128, 128, visitor)[0] == group
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        assert database.execute("SELECT OtherCleanTime FROM land").fetchone() == (45,)
                    with closing(sqlite3.connect(root / "groups.db")) as database, database:
                        database.execute("UPDATE os_groups_roles SET Powers='4194304' WHERE RoleID=?", (role,))
                    send("ffff00c8", visitor.bytes + session_id + struct.pack("<ii", 1, 90))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def group_clean_time_was_saved():
                            return database.execute("SELECT OtherCleanTime FROM land").fetchone() == (90,)

                        wait_until(group_clean_time_was_saved, "Group parcel clean time was not saved")
            with closing(sqlite3.connect(root / "terrain.db")) as database, database:
                database.execute(
                    "UPDATE land SET SalePrice=25, AuthbuyerID=?, LandFlags=CAST(LandFlags AS INTEGER)|?",
                    (str(visitor), sale_flags),
                )
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Visitor")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + visitor.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", visitor.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    purchase = visitor.bytes + session_id + group.bytes + struct.pack("<BBiB", 0, 0, 1, 0)
                    send("ffff00d5", purchase + struct.pack("<ii", 24, 65536))
                    assert parcel_info(128, 128, visitor)[0] == group
                    send("ffff00d5", purchase + struct.pack("<ii", 25, 65536))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def purchase_was_saved():
                            row = database.execute(
                                "SELECT OwnerUUID,GroupUUID,ClaimPrice,SalePrice,AuthbuyerID,LandFlags,IsGroupOwned "
                                "FROM land"
                            ).fetchone()
                            if row[0] != str(visitor):
                                return False
                            assert row[1:5] == (str(group), 25, 0, str(uuid.UUID(int=0)))
                            assert not int(row[5]) & sale_flags
                            assert str(row[6]).lower() in ("false", "0")
                            return True

                        wait_until(purchase_was_saved, "Parcel purchase did not save ownership and sale fields")
                    assert parcel_info(128, 128, visitor)[0] == visitor
                    send("ffff00e3", visitor.bytes + session_id + struct.pack("<i", 1))
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff0086") and b"Request denied." in payload:
                            break
                    else:
                        raise AssertionError("Non-administrator parcel content reset was not denied")
                    assert parcel_info(128, 128, visitor)[0] == visitor
            with closing(sqlite3.connect(root / "terrain.db")) as database, database:
                database.execute("UPDATE land SET PassHours=1, PassPrice=5, LandFlags=2048")
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Owner")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + owner.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    buy_pass = owner.bytes + session_id + struct.pack("<i", 1)
                    started = int(time.time())
                    send("ffff00ce", buy_pass)
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        wait_until(
                            lambda: database.execute(
                                "SELECT COUNT(*) FROM landaccesslist WHERE AccessUUID=? AND Flags=1",
                                (str(owner),),
                            ).fetchone() == (1,),
                            "Sample-economy parcel pass was not saved",
                        )

                    def access_expiry():
                        send("ffff00d7", owner.bytes + session_id + struct.pack("<iIi", 0, 1, 1))
                        for _ in range(256):
                            payload = receive()
                            if payload[:4] == bytes.fromhex("ffff00d8") and payload[33:49] == owner.bytes:
                                return struct.unpack_from("<i", payload, 49)[0]
                        raise AssertionError("Parcel pass was absent from the viewer access list")

                    expiry = access_expiry()
                    assert started + 3600 <= expiry <= int(time.time()) + 3600
                    send("ffff00ce", buy_pass)
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff0087") and b"You already have a pass valid" in payload:
                            break
                    else:
                        raise AssertionError("A duplicate pass purchase did not report its remaining time")
                    assert access_expiry() == expiry
                    send("ffff00e3", owner.bytes + session_id + struct.pack("<i", 1))
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        def content_parcel_was_saved():
                            row = database.execute(
                                "SELECT OwnerUUID,GroupUUID,Name,Area,CAST(LandFlags AS INTEGER),"
                                "LandingType,PassPrice,OtherCleanTime,MusicURL,MediaType,Dwell FROM land"
                            ).fetchone()
                            if row[2] != "Public Content Fixture":
                                return False
                            assert row == (
                                str(owner), str(uuid.UUID(int=0)), "Public Content Fixture",
                                65536, 0x2800204B, 2, 0, 0, "", "none/none", 0,
                            )
                            assert database.execute("SELECT COUNT(*) FROM landaccesslist").fetchone() == (0,)
                            return True

                        wait_until(content_parcel_was_saved, "Administrator parcel content reset was not saved")
                    assert parcel_info(128, 128)[0] == owner
            administrator_group = uuid.uuid4()
            group_content_config = root / "group-content.ini"
            group_content_config.write_text(
                config.read_text(encoding="utf-8").replace(
                    f"DefaultAdministratorOwnerUUID={owner}",
                    f"DefaultAdministratorGroupUUID={administrator_group}",
                ),
                encoding="utf-8",
            )
            with running_server(args.native, group_content_config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Owner")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + owner.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00f9", owner.bytes + session_id + code)
                    while receive()[:4] != bytes.fromhex("ffff00fa"):
                        pass
                    mark_content = owner.bytes + session_id + struct.pack("<i", 1)
                    send("ffff00e3", mark_content)
                    for _ in range(256):
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff0086") and b"Please check ini files." in payload:
                            break
                    else:
                        raise AssertionError("Missing administrator group was accepted")
                    assert parcel_info(128, 128)[0] == owner
                    with closing(sqlite3.connect(root / "groups.db")) as database, database:
                        database.execute("INSERT INTO os_groups_groups VALUES (?, '')", (str(administrator_group),))
                    send("ffff00e3", mark_content)
                    with closing(sqlite3.connect(root / "terrain.db")) as database:
                        wait_until(
                            lambda: database.execute(
                                "SELECT OwnerUUID,GroupUUID,IsGroupOwned FROM land"
                            ).fetchone() == (str(administrator_group), str(administrator_group), "True"),
                            "Existing administrator group was not accepted",
                        )
                    assert parcel_info(128, 128)[0] == administrator_group
            print("PASS: parcel ownership, purchases, access passes and administrator resets persist in SQLite")
            return
        if args.dwell:
            with running_server(args.native, config, region=True) as (url, endpoint):
                with ServerProxy(url + "/") as proxy, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
                    session = login(proxy, "Owner")
                    assert session.get("login") == "true", session
                    session_id = uuid.UUID(session["session_id"]).bytes
                    code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
                    sequence = 0
                    viewer.settimeout(5)
                    send("ffff0003", code + session_id + owner.bytes)
                    while receive()[:4] != bytes.fromhex("ffff0094"):
                        pass
                    send("ffff00da", owner.bytes + session_id + struct.pack("<i16s", 1, bytes(16)))
                    while True:
                        payload = receive()
                        if payload[:4] == bytes.fromhex("ffff00db"):
                            assert struct.unpack_from("<f", payload, 40) == (float(saved_dwell),)
                            break
            print("PASS: live dwell creates the default parcel and survives restart")
            return

        with closing(sqlite3.connect(root / "terrain.db")) as database:
            if args.large_terrain:
                for table in ("terrain", "bakedterrain"):
                    revision, blob = database.execute(f"SELECT Revision,Heightfield FROM {table}").fetchone()
                    assert revision == 23
                    raw = gzip.decompress(blob)
                    assert struct.unpack_from("<II", raw) == (dimension, dimension)
                    assert struct.unpack_from("<f", raw, 8 + 4 * (edited_y * dimension + edited_x))[0] == 64
                print("PASS: LLRAW upload above 64 MiB persists active and baked terrain")
                return
            parcel, bitmap, flags = database.execute("SELECT UUID,Bitmap,LandFlags FROM land").fetchone()
            assert bitmap == bytes([255]) * 512 and int(flags) & 1024
            assert database.execute(
                "SELECT LandUUID,AccessUUID,CAST(Flags AS INTEGER) FROM landaccesslist"
            ).fetchone() == (parcel, str(visitor), 2)
        with running_server(args.native, config, region=True) as (url, _):
            with ServerProxy(url + "/") as proxy:
                denied = login(proxy, "Visitor")
                assert denied.get("login") == "false", denied
                accepted = login(proxy, "Owner")
                assert accepted.get("login") == "true", accepted
    print("PASS: generated parcel and ban survive restart")


if __name__ == "__main__":
    main()
