"""Verify embedded legacy material migration across a SQLite-backed region restart."""

import argparse
import base64
from contextlib import closing
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
from threading import Thread
from urllib.request import Request, urlopen
from uuid import UUID
from xml.etree import ElementTree
from xmlrpc.client import ServerProxy

from remote_presence_network import running_server, wait_until


REGION = "11111111-1111-4111-8111-111111111111"
DESTINATION = "11111111-1111-4111-8111-111111111112"
PRIM = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"
OWNER = "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
MATERIAL = "12345678-1234-1234-1234-123456789abc"
CROSSING_ROOT = "c7cb523a-f9ab-4a56-9f8d-44f239b8b00e"
CROSSING_CHILD = "82ad9a50-b8b4-460a-aa2b-bec87f1d45b2"
CROSSING_SCRIPT_ASSET = "7b1a5a42-fc36-4936-aa3a-a13535841e59"
CROSSING_SCRIPT_ITEM = "bc9108f0-5f03-462f-82a9-ad21af9df594"
CHILD_SCRIPT_ASSET = "50d03426-b0be-4d7c-aaf3-90d789a5c9dd"
CHILD_SCRIPT_ITEM = "ea6c1ef2-20d6-4d0e-b57d-f014a8fbb760"
RESTART_SCRIPT_ASSET = "01ca4585-74ac-4825-a8c8-328e73680c02"
RESTART_SCRIPT_ITEM = "50133e33-a07d-4fe7-8921-7ba47e8946a0"
ATTACHMENT_PRIM = "549252f2-dba5-4a1e-858e-58ba607bc100"
CROSSING_ANIMATION = b"\x01\x00" + UUID("03c6d03c-7464-4036-a9df-1b338a342ad2").bytes + b"\x03run"
CROSSING_TEXTURE_ANIMATION = base64.b64encode(struct.pack("<4B3f", 3, 255, 4, 8, 1.25, 2.5, -3)).decode()
CROSSING_SCRIPT = b"""
integer counter;
default {
    state_entry() {
        if (llGetRegionName() != "Legacy Material Restart") return;
        integer increment = 2;
        counter = 40;
        llSetPhysicsMaterial(GRAVITY_MULTIPLIER | RESTITUTION | FRICTION | DENSITY,
                             -0.5, 0.25, 1.5, 1234);
        llSetPhysicsMaterial(FRICTION | DENSITY, 5, 0.75, 2.5, 0);
        llSitTarget(<0,0,1>, ZERO_ROTATION);
        llSetRegionPos(<120,122,30>);
        llSleep(10.0);
        list material = llGetPhysicsMaterial();
        if (llList2Float(material, 0) != -0.5 || llList2Float(material, 1) != 0.25 ||
            llList2Float(material, 2) != 2.5 || llList2Float(material, 3) != 1234)
            return;
        if (counter + increment == 42 && llGetRegionName() == "Crossing Destination") {
            llMessageLinked(LINK_ALL_CHILDREN, 42, "crossed", NULL_KEY);
        }
    }
    link_message(integer sender, integer number, string message, key id) {
        if (sender == 2 && number == 43 && message == "received" && id == NULL_KEY) {
            llSetTextureAnim(0, ALL_SIDES, 0, 0, 0, 0, 0);
            llSetRegionPos(<5,121,31>);
        }
    }
}
"""
CHILD_SCRIPT = b"""
default {
    changed(integer mask) {
        if ((mask & CHANGED_LINK) && llGetRegionName() == "Legacy Material Restart") {
            key rider = llAvatarOnLinkSitTarget(LINK_ROOT);
            if (rider == llGetOwner() && !(llGetPermissions() & PERMISSION_TAKE_CONTROLS))
                llRequestPermissions(rider, PERMISSION_TAKE_CONTROLS | PERMISSION_TRIGGER_ANIMATION
                    | PERMISSION_OVERRIDE_ANIMATIONS);
        }
    }
    run_time_permissions(integer granted) {
        if (granted & PERMISSION_TAKE_CONTROLS)
            llTakeControls(CONTROL_FWD | CONTROL_LBUTTON, TRUE, FALSE);
        if (granted & PERMISSION_TRIGGER_ANIMATION)
            llStartAnimation("hello");
        if (granted & PERMISSION_OVERRIDE_ANIMATIONS)
            llSetAnimationOverride("Standing", "salute");
    }
    control(key rider, integer held, integer changed) {
        if (rider != llGetOwner())
            return;
        if (llGetRegionName() == "Legacy Material Restart" && held == CONTROL_LBUTTON)
            llSay(0, "Rider pressed the mouse button");
        if (llGetRegionName() == "Crossing Destination" && held == CONTROL_FWD
            && changed == (CONTROL_FWD | CONTROL_LBUTTON))
            llSay(0, "Rider controls survived the crossing");
    }
    link_message(integer sender, integer number, string message, key id) {
        if (sender == 1 && number == 42 && message == "crossed" && id == NULL_KEY)
            llMessageLinked(LINK_ROOT, 43, "received", id);
    }
}
"""
EMBEDDED = (
    "<llsd><map><key>OpenSim</key><map>"
    "<key>OtherStore</key><map><key>note</key><string>keep</string></map>"
    "<key>Materials</key><map><key>Materials</key><array><map>"
    f"<key>ID</key><uuid>{MATERIAL}</uuid>"
    "<key>Material</key><map><key>DiffuseAlphaMode</key><integer>2</integer>"
    "<key>NormRepeatX</key><integer>10000</integer>"
    "</map></map></array></map></map></map></llsd>"
)


def create_region_fixture(path, seed=True):
    fields = {
        "UUID": PRIM, "SceneGroupID": PRIM, "RegionUUID": REGION,
        "Name": "Embedded legacy material", "Description": "",
        "OwnerID": OWNER, "GroupID": "00000000-0000-0000-0000-000000000000",
        "LastOwnerID": OWNER, "CreatorID": OWNER,
        "CreationDate": 1700000000, "ObjectFlags": 0, "DieAtEdge": 0,
        "BaseMask": 0x9E000, "OwnerMask": 0x9E000, "GroupMask": 0,
        "EveryoneMask": 0, "NextOwnerMask": 0x82000,
        "SaleType": 0, "SalePrice": 0, "Material": 3, "ClickAction": 0,
        "GroupPositionX": 128, "GroupPositionY": 128, "GroupPositionZ": 25,
        "PositionX": 0, "PositionY": 0, "PositionZ": 0,
        "RotationX": 0, "RotationY": 0, "RotationZ": 0, "RotationW": 1,
        "AttachedPosX": 0, "AttachedPosY": 0, "AttachedPosZ": 0,
        "DynAttrs": EMBEDDED, "sopanims": None, "TextureAnimation": "",
        "Density": "1000", "Friction": "0.6", "Restitution": "0.5", "GravityModifier": "1",
    }
    with closing(sqlite3.connect(path)) as database, database:
        database.execute("CREATE TABLE prims (" + ", ".join(fields) + ")")
        if seed:
            placeholders = ", ".join("?" for _ in fields)
            database.execute(f"INSERT INTO prims VALUES ({placeholders})", tuple(fields.values()))
        database.execute(
            "CREATE TABLE primshapes (UUID PRIMARY KEY, PCode, State, ScaleX, ScaleY, ScaleZ, "
            "PathBegin, PathEnd, ProfileBegin, ProfileEnd, ProfileHollow, PathScaleX, PathScaleY, "
            "PathShearX, PathShearY, PathCurve, PathRevolutions, ProfileCurve, PathSkew, "
            "PathRadiusOffset, PathTaperX, PathTaperY, PathTwist, PathTwistBegin, Texture, ExtraParams, LastAttachPoint)"
        )
        database.execute(
            "CREATE TABLE primitems (itemID PRIMARY KEY, primID, assetID, parentFolderID, invType, assetType, "
            "name, description, creationDate, creatorID, ownerID, lastOwnerID, groupID, nextPermissions, "
            "currentPermissions, basePermissions, everyonePermissions, groupPermissions, flags)"
        )


def read_attributes(path):
    with closing(sqlite3.connect(path)) as database:
        return database.execute("SELECT DynAttrs FROM prims WHERE UUID=?", (PRIM,)).fetchone()[0]


def read_asset(path):
    with closing(sqlite3.connect(path)) as database:
        return database.execute("SELECT Type, Data FROM assets WHERE UUID=?", (MATERIAL,)).fetchone()


def install_crossing_scripts(path):
    with closing(sqlite3.connect(path)) as database, database:
        database.executemany(
            "INSERT INTO assets (UUID,Name,Description,Type,Local,Temporary,CreatorID,Data) "
            "VALUES (?, 'Crossing script', '', 10, 0, 0, ?, ?)",
            [(CROSSING_SCRIPT_ASSET, OWNER, CROSSING_SCRIPT), (CHILD_SCRIPT_ASSET, OWNER, CHILD_SCRIPT)],
        )

def verify_script_restart(executable, config, region_db, asset_db):
    script = b"""
integer listener;
integer region_starts;
key granter;
default {
    state_entry() {
        if (llGetObjectDesc() == "Restart checkpoint") {
            llSetObjectDesc("Unexpected state_entry");
            return;
        }
        listener = llListen(8123, "", NULL_KEY, "restart listener");
        llListenControl(llListen(8124, "", NULL_KEY, "restart listener"), FALSE);
        llListen(8125, "", NULL_KEY, "request grant");
    }
    listen(integer channel, string name, key sender, string message) {
        if (channel == 8124) {
            llSetObjectDesc("Disabled listener fired");
            return;
        }
        if (channel == 8125) {
            granter = sender;
            llRequestPermissions(sender, PERMISSION_TRIGGER_ANIMATION);
            return;
        }
        if (llGetObjectDesc() == "Restart startup event") {
            llListenRemove(listener);
            llSetObjectDesc("Restart listener received");
        }
    }
    run_time_permissions(integer granted) {
        if (granted != PERMISSION_TRIGGER_ANIMATION) return;
        llSetObjectDesc("Restart checkpoint");
        llSleep(6);
        if (llGetPermissions() == granted && llGetPermissionsKey() == granter)
            llSetObjectDesc("Restart resumed");
        else
            llSetObjectDesc("Restart lost permissions");
    }
    changed(integer mask) {
        if (mask == CHANGED_REGION_START) {
            ++region_starts;
            if (region_starts == 2 && llGetObjectDesc() == "Restart resumed")
                llSetObjectDesc("Restart startup event");
        }
    }
}
"""
    with closing(sqlite3.connect(asset_db)) as database, database:
        database.execute(
            "INSERT INTO assets (UUID,Name,Description,Type,Local,Temporary,CreatorID,Data) "
            "VALUES (?, 'Restart script', '', 10, 0, 0, ?, ?)", (RESTART_SCRIPT_ASSET, OWNER, script),
        )
    with closing(sqlite3.connect(region_db)) as database, database:
        database.execute(
            "INSERT INTO primitems VALUES (?, ?, ?, ?, 10, 10, 'Restart script', '', 1700000000, ?, ?, ?, "
            "'00000000-0000-0000-0000-000000000000', 647168, 647168, 647168, 0, 0, 0)",
            (RESTART_SCRIPT_ITEM, PRIM, RESTART_SCRIPT_ASSET, PRIM, OWNER, OWNER, OWNER),
        )

    def read_state():
        with closing(sqlite3.connect(region_db)) as database:
            return database.execute(
                "SELECT p.Description,s.checkpoint FROM prims p LEFT JOIN native_script_states s "
                "ON s.region_id=p.RegionUUID AND s.part_id=p.UUID AND s.item_id=? WHERE p.UUID=?",
                (RESTART_SCRIPT_ITEM, PRIM),
            ).fetchone()

    subprocess.run([executable, "--config", str(config), "--create-user", "Restart", "Viewer"],
                   input="fixture-password\n", text=True, check=True, timeout=20)
    def connect_viewer(url, endpoint, viewer):
        with ServerProxy(url + "/") as login:
            session = login.login_to_simulator({
                "first": "Restart", "last": "Viewer", "passwd": "fixture-password",
                "start": "uri:Legacy Material Restart&128&128&25", "version": "fixture",
            })
        assert session.get("login") == "true", session
        agent = UUID(session["agent_id"]).bytes
        session_id = UUID(session["session_id"]).bytes
        code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
        sequence = 0

        def send(packet_id, data):
            nonlocal sequence
            sequence += 1
            viewer.sendto(struct.pack(">BIB", 0, sequence, 0) + bytes.fromhex(packet_id) + data, endpoint)

        def receive(packet_id):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                packet, _ = viewer.recvfrom(8192)
                payload = packet[6 + packet[5]:]
                if payload.startswith(bytes.fromhex(packet_id)):
                    return payload
            raise AssertionError(f"Viewer did not receive packet {packet_id}")

        def send_chat(channel, text):
            wire_text = text + b"\0"
            send("ffff0050", agent + session_id + struct.pack("<H", len(wire_text)) + wire_text +
                 struct.pack("<Bi", 1, channel))

        viewer.settimeout(5)
        send("ffff0003", code + session_id + agent)
        send("ffff00f9", agent + session_id + code)
        receive("ffff00fa")
        return agent + session_id, send, receive, send_chat

    with running_server(executable, config, region=True) as (url, endpoint):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
            identity, send, receive, send_chat = connect_viewer(url, endpoint, viewer)
            send_chat(8125, b"request grant")
            question = receive("ffff00bc")
            assert question[4:36] == UUID(PRIM).bytes + UUID(RESTART_SCRIPT_ITEM).bytes
            send("ffff0084", identity + question[4:36] + struct.pack("<i", 0x10))
            wait_until(lambda: read_state()[0] == "Restart checkpoint", "Script did not save its granted checkpoint")
            assert read_state()[1].startswith(b"LSC\x03"), "permission grant must be in the same transaction"
    with running_server(executable, config, region=True) as (url, endpoint):
        wait_until(lambda: read_state()[0] == "Restart startup event",
                   "Script did not resume with its permission grant and receive CHANGED_REGION_START")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
            _, _, _, send_chat = connect_viewer(url, endpoint, viewer)
            for channel in (8124, 8123):
                send_chat(channel, b"restart listener")
            wait_until(lambda: read_state()[0] == "Restart listener received",
                       "Restored listener did not receive authenticated viewer chat")


def cross_linkset(url, already_present=False):
    animation = base64.b64encode(CROSSING_ANIMATION).decode()
    shape = "<Shape><PCode>9</PCode><State>0</State></Shape>"

    def part(part_id, content):
        return (f"<SceneObjectPart><UUID><UUID>{part_id}</UUID></UUID>"
                f"<OwnerID><UUID>{OWNER}</UUID></OwnerID>{shape}{content}</SceneObjectPart>")

    root = part(
        CROSSING_ROOT, f"<SOPAnims>{animation}</SOPAnims>"
        f"<TextureAnimation>{CROSSING_TEXTURE_ANIMATION}</TextureAnimation>",
    )
    child = part(CROSSING_CHILD, "")
    xml = f"<SceneObjectGroup>{root}<OtherParts>{child}</OtherParts></SceneObjectGroup>"
    body = json.dumps({"destination_uuid": REGION, "sog": xml, "new_position": [120, 121, 30]}).encode()
    request = Request(f"{url}/object/{CROSSING_ROOT}/", body, {"Content-Type": "application/json"})
    if not already_present:
        with urlopen(request, timeout=5) as response:
            assert response.read() == b"True"
    with urlopen(request, timeout=5) as response:
        assert response.read() == b"False"


def crossing_rows(path):
    with closing(sqlite3.connect(path)) as database:
        root = database.execute(
            "SELECT SceneGroupID, GroupPositionX, GroupPositionY, GroupPositionZ, sopanims, "
            "TextureAnimation FROM prims WHERE UUID=?",
            (CROSSING_ROOT,),
        ).fetchone()
        child = database.execute("SELECT SceneGroupID FROM prims WHERE UUID=?", (CROSSING_CHILD,)).fetchone()
    return root, child


def check_crossing(path, position=("120", "121", "30"), texture_animation=CROSSING_TEXTURE_ANIMATION):
    root, child = crossing_rows(path)
    assert root == (CROSSING_ROOT, *position, CROSSING_ANIMATION, texture_animation), root
    assert child == (CROSSING_ROOT,), child


def viewer_channel(viewer, endpoint):
    sequence = 0
    viewer.settimeout(5)

    def send(packet_id, data, reliable=True, target=endpoint):
        nonlocal sequence
        sequence += 1
        header = struct.pack(">BIB", 0x40 if reliable else 0, sequence, 0)
        viewer.sendto(header + bytes.fromhex(packet_id) + data, target)

    def receive():
        datagram, peer = viewer.recvfrom(8192)
        if datagram[0] & 0x40:
            reply_sequence = struct.unpack_from(">I", datagram, 1)[0]
            send("fffffffb", struct.pack("<BI", 1, reply_sequence), reliable=False, target=peer)
        payload = datagram[6 + datagram[5]:]
        if not datagram[0] & 0x80:
            return payload
        decoded = bytearray()
        offset = 0
        while offset < len(payload):
            value = payload[offset]
            offset += 1
            if value:
                decoded.append(value)
            elif offset < len(payload):
                decoded.extend(bytes(payload[offset]))
                offset += 1
        return decoded

    return send, receive

def verify_attachment_restart(executable, config, region_db):
    with closing(sqlite3.connect(config.parent / "users.db")) as accounts:
        owner = accounts.execute(
            "SELECT PrincipalID FROM UserAccounts WHERE FirstName='Restart' AND LastName='Viewer'"
        ).fetchone()[0]
    with closing(sqlite3.connect(region_db)) as database, database:
        database.row_factory = sqlite3.Row
        part = dict(database.execute("SELECT * FROM prims WHERE UUID=?", (CROSSING_ROOT,)).fetchone())
        part.update(UUID=ATTACHMENT_PRIM, SceneGroupID=ATTACHMENT_PRIM, OwnerID=owner,
                    Name="Restart attachment", AttachedPosX=0.25, AttachedPosY=-0.5, AttachedPosZ=0.75,
                    DieAtEdge=1)
        database.execute("INSERT INTO prims (" + ", ".join(part) + ") VALUES (" +
                         ", ".join("?" for _ in part) + ")", tuple(part.values()))
        shape = dict(database.execute("SELECT * FROM primshapes WHERE UUID=?", (CROSSING_ROOT,)).fetchone())
        shape.update(UUID=ATTACHMENT_PRIM, LastAttachPoint=6)
        database.execute("INSERT INTO primshapes (" + ", ".join(shape) + ") VALUES (" +
                         ", ".join("?" for _ in shape) + ")", tuple(shape.values()))

    def saved_attachment():
        with closing(sqlite3.connect(region_db)) as database:
            row = database.execute(
                "SELECT s.LastAttachPoint,p.AttachedPosX,p.AttachedPosY,p.AttachedPosZ,p.DieAtEdge "
                "FROM prims p JOIN primshapes s ON p.UUID=s.UUID WHERE p.UUID=?", (ATTACHMENT_PRIM,),
            ).fetchone()
        return tuple(float(value) for value in row) if row else None

    for cycle in range(2):
        with (
            running_server(executable, config, region=True) as (url, endpoint),
            ServerProxy(url + "/") as login,
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer,
        ):
            session = login.login_to_simulator({
                "first": "Restart", "last": "Viewer", "passwd": "fixture-password",
                "start": "uri:Legacy Material Restart&128&128&25", "version": "fixture",
            })
            assert session.get("login") == "true", session
            agent = UUID(session["agent_id"]).bytes
            session_id = UUID(session["session_id"]).bytes
            code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
            send, receive = viewer_channel(viewer, endpoint)
            send("ffff0003", code + session_id + agent)
            send("ffff00f9", agent + session_id + code)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                update = receive()
                if update[:1] == b"\x0c" and update[17:33] == UUID(ATTACHMENT_PRIM).bytes:
                    local_id = struct.unpack_from("<I", update, 12)[0]
                    break
            else:
                raise AssertionError("Viewer did not receive the saved attachment prim")
            identity = agent + session_id
            send("ffff0070", identity + struct.pack("<BBI4f", 128, 1, local_id, 0, 0, 0, 1))
            wait_until(lambda: saved_attachment() is None, "Worn object remained in the ground backup")
            send("ffff0072", identity + struct.pack("<BI", 1, local_id))
            wait_until(lambda: saved_attachment() == (6, 0.25, -0.5, 0.75, 1),
                       "Dropped object did not retain its attachment metadata and edge flag")
            if cycle == 1:
                send("ff04", identity + struct.pack("<BI3f", 1, local_id, 257, 170, 30))
                wait_until(lambda: saved_attachment() is None, "Restored die-at-edge object was not removed")


def move_linkset_as_viewer(url, endpoint, destination_endpoint):
    with (ServerProxy(url + "/") as login,
          socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer,
          socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as passenger_viewer,
          socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rejected_viewer):
        def login_rider(first, send):
            session = login.login_to_simulator({
                "first": first, "last": "Viewer", "passwd": "fixture-password",
                "start": "uri:Legacy Material Restart&120&122&30", "version": "fixture",
            })
            assert session.get("login") == "true", session
            agent = UUID(session["agent_id"]).bytes
            session_id = UUID(session["session_id"]).bytes
            code = struct.pack("<I", int(session["circuit_code"]) & 0xffffffff)
            send("ffff0003", code + session_id + agent)
            send("ffff00f9", agent + session_id + code)
            return session, agent, session_id, code

        send, receive = viewer_channel(viewer, endpoint)
        send_passenger, receive_passenger = viewer_channel(passenger_viewer, endpoint)
        send_rejected, receive_rejected = viewer_channel(rejected_viewer, endpoint)
        session, agent, session_id, code = login_rider("Crossing", send)
        passenger, passenger_agent, passenger_session, passenger_code = login_rider("Passenger", send_passenger)
        _, rejected_agent, rejected_session, _ = login_rider("Rejected", send_rejected)
        target = UUID(CROSSING_ROOT).bytes
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            update = receive()
            if update[:1] == b"\x0c" and update[17:33] == target:
                local_id = struct.unpack_from("<I", update, 12)[0]
                break
        else:
            raise AssertionError("Viewer did not receive the source linkset")
        def post_capability(url, payload):
            request = Request(url, payload, {"Content-Type": "application/llsd+xml"})
            with urlopen(request, timeout=15) as response:
                return ElementTree.fromstring(response.read())

        def crossing_queue(login_response):
            seed = post_capability(login_response["seed_capability"],
                                   b"<llsd><array><string>EventQueueGet</string></array></llsd>")
            event_queue = seed.findtext("map/string")
            assert event_queue
            return event_queue

        event_queue = crossing_queue(session)
        passenger_queue = crossing_queue(passenger)

        def sit(send_packet, receive_packet, rider, rider_session, part, offset):
            send_packet("06", rider + rider_session + part + struct.pack("<3f", *offset))
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                update = receive_packet()
                if update[:1] == b"\x0c" and update[17:33] == rider:
                    if struct.unpack_from("<I", update, 53 + update[52])[0] == local_id:
                        return struct.unpack_from("<3f", update, 69)
            raise AssertionError("Viewer did not sit on the source linkset")

        seat_offset = sit(send, receive, agent, session_id, target, (0, 0, 0))

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            question = receive()
            if question[:4] == bytes.fromhex("ffff00bc"):
                requested_permissions = 0x8014
                send("ffff0084", agent + session_id + question[4:36] + struct.pack("<i", requested_permissions))
                break
        else:
            raise AssertionError("The seat script did not request animation override permission")

        def is_control_capture(packet):
            return packet[:11] == bytes.fromhex("ffff00bd01010100001000")

        def wait_for_control_capture(receive_packet=receive):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                packet = receive_packet()
                if is_control_capture(packet):
                    return
            raise AssertionError("Viewer did not receive the script control capture")

        wait_for_control_capture()

        hello_animation = UUID("9b29cd61-c45b-5689-ded2-91756b8d76a9").bytes
        sit_animation = UUID("1a5fe8ac-a804-8a5d-7cbd-56bd83184568").bytes

        def animation_sequence(packet, animation):
            if packet[:17] != b"\x14" + agent:
                return None
            for index in range(packet[17]):
                offset = 18 + index * 20
                if packet[offset:offset + 16] == animation:
                    return struct.unpack_from("<i", packet, offset + 16)[0]
            return None

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            packet = receive()
            source_animation_sequence = animation_sequence(packet, hello_animation)
            if source_animation_sequence is not None:
                source_sit_sequence = animation_sequence(packet, sit_animation)
                assert source_sit_sequence is not None, "The source did not publish a default sitting animation"
                break
        else:
            raise AssertionError("The seat script did not start the rider's animation")

        def send_rider_input(flags, target=endpoint):
            send("04", agent + session_id + bytes(25) +
                 struct.pack("<13fIB", 4, 121, 30, 1, 0, 0, 0, 1, 0, 0, 0, 1, 64, flags, 0),
                 target=target)

        send_rider_input(0x10000000)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if b"Rider pressed the mouse button" in receive():
                break
        else:
            raise AssertionError("Source script did not observe the mouse press")

        passenger_offset = sit(send_passenger, receive_passenger, passenger_agent, passenger_session,
                               UUID(CROSSING_CHILD).bytes, (1, 0, 0))
        assert passenger_offset != seat_offset
        sit(send_rejected, receive_rejected, rejected_agent, rejected_session, target, (-1, 0, 0))
        send("ff04", agent + session_id + struct.pack("<BI3f", 1, local_id, 260, 121, 30))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            update = receive()
            if update[:1] == b"\x10" and len(update) >= 6:
                if local_id in struct.unpack_from(f"<{update[1]}I", update, 2):
                    break
        else:
            raise AssertionError("Source did not remove the crossed linkset")

        rejected = standing = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not (rejected and standing):
            update = receive_rejected()
            if update[:4] == bytes.fromhex("ffff0087"):
                rejected |= b"Unknown local account" in update
            elif update[:1] == b"\x0c" and update[17:33] == rejected_agent:
                standing |= struct.unpack_from("<I", update, 53 + update[52])[0] == 0
        assert rejected and standing, "The rejected rider did not stand in the source region and receive the reason"

        def cross(event_queue, send_packet, receive_packet, rider, rider_session, circuit, expected_offset):
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                events = post_capability(event_queue,
                    b"<llsd><map><key>ack</key><undef/><key>done</key><boolean>0</boolean></map></llsd>")
                if any(event.findtext("string") == "CrossedRegion" for event in events.iter("map")):
                    break
            else:
                raise AssertionError("Seated viewer did not receive a crossing event")
            send_packet("ffff0003", circuit + rider_session + rider, target=destination_endpoint)
            send_packet("ffff00f9", rider + rider_session + circuit, target=destination_endpoint)
            parent_id = None
            destination_root = None
            captured = False
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                update = receive_packet()
                captured |= is_control_capture(update)
                if update[:1] != b"\x0c":
                    continue
                if update[17:33] == target:
                    destination_root = struct.unpack_from("<I", update, 12)[0]
                elif update[17:33] == rider:
                    parent_id = struct.unpack_from("<I", update, 53 + update[52])[0]
                    offset = struct.unpack_from("<3f", update, 69)
                if destination_root and parent_id == destination_root:
                    assert offset == expected_offset, (offset, expected_offset)
                    return captured
            raise AssertionError(f"Rider was not parented to the destination linkset: {parent_id}, {destination_root}")

        destination_animation_sequences = set()
        destination_sit_sequences = set()

        def receive_destination():
            packet = receive()
            sequence = animation_sequence(packet, hello_animation)
            if sequence is not None:
                destination_animation_sequences.add(sequence)
            sequence = animation_sequence(packet, sit_animation)
            if sequence is not None:
                destination_sit_sequences.add(sequence)
            return packet

        if not cross(event_queue, send, receive_destination, agent, session_id, code, seat_offset):
            wait_for_control_capture(receive_destination)
        send_rider_input(0x20000000 | 1, destination_endpoint)
        received_control = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            received_control |= b"Rider controls survived the crossing" in receive_destination()
            if (received_control and source_animation_sequence in destination_animation_sequences
                    and source_sit_sequence in destination_sit_sequences):
                break
        assert received_control, "Destination script did not observe forward input and the transferred mouse release"
        assert source_animation_sequence in destination_animation_sequences, "The rider's animation restarted or disappeared"
        assert source_sit_sequence in destination_sit_sequences, "The default sitting animation restarted at the crossing"
        cross(passenger_queue, send_passenger, receive_passenger, passenger_agent, passenger_session,
              passenger_code, passenger_offset)

        destination_animation_sequences.clear()
        send_rider_input(0x10000, destination_endpoint)
        release_packets = set()
        standing_override = UUID("cd7668a6-7011-d7e2-ead8-fc69eff1a104").bytes
        received_standing_override = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            packet = receive_destination()
            if packet[:10] == bytes.fromhex("ffff00bd010001000010"):
                release_packets.add(packet[10])
            received_standing_override |= animation_sequence(packet, standing_override) is not None
            if (release_packets == {0, 1} and received_standing_override
                    and source_animation_sequence in destination_animation_sequences):
                break
        assert release_packets == {0, 1}, "Standing did not release both viewer control registrations"
        assert source_animation_sequence in destination_animation_sequences, "Standing interrupted the transferred animation"
        assert received_standing_override, "Standing did not use the override transferred from the source region"


def cross_between_regions(executable, root, config, source_db):
    destination = root / "destination"
    destination.mkdir()
    destination_db = destination / "terrain.db"
    destination_config = destination / "region.ini"
    source_assets = root / "assets.db"
    create_region_fixture(destination_db, seed=False)
    destination_config.write_text(
        "[NativeServer]\nEnableLocalRegion=true\nEnableRegionAssets=true\n"
        "EnableSimulationEndpoint=true\nEnableGridEndpoint=true\nEnablePresenceEndpoint=true\n"
        "ListenAddress=127.0.0.1\nUserAccountDatabase=users.db\nAuthenticationDatabase=users.db\n"
        "InventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db\n"
        "[Network]\nhttp_listener_port=0\n"
        f"[NativeRegion]\nID={DESTINATION}\nName=Crossing Destination\nLocationX=1001\nUDPPort=0\n"
        "BindAddress=127.0.0.1\nExternalHost=127.0.0.1\nTerrainDatabase=terrain.db\n"
        "[PresenceService]\nConnectionString=presence.db\n"
        "[Terrain]\nInitialTerrain=flat\n"
        "[Modules]\nAssetServices=RegionAssetConnector\n"
        "[AssetService]\nLocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector\n"
        "AuthType=BasicHttpAuthentication\nHttpAuthUsername=grid\nHttpAuthPassword=asset-fixture\n",
        encoding="utf-8",
    )
    subprocess.run([executable, "--config", str(destination_config), "--init-db"], check=True, timeout=20)
    settings = destination_config.read_text(encoding="utf-8")
    settings = settings.replace("ConnectionString=presence.db", f"ConnectionString={root / 'presence.db'}")
    destination_config.write_text(settings, encoding="utf-8")
    command = [executable, "--config", str(config), "--create-user", "Crossing", "Viewer"]
    subprocess.run(command, input="fixture-password\n", text=True, check=True, timeout=20)
    subprocess.run([executable, "--config", str(config), "--create-user", "Passenger", "Viewer"],
                   input="fixture-password\n", text=True, check=True, timeout=20)
    with closing(sqlite3.connect(config.parent / "users.db")) as accounts, \
            closing(sqlite3.connect(destination / "users.db")) as destination_accounts:
        accounts.backup(destination_accounts)
    subprocess.run([executable, "--config", str(config), "--create-user", "Rejected", "Viewer"],
                   input="fixture-password\n", text=True, check=True, timeout=20)
    with closing(sqlite3.connect(config.parent / "users.db")) as accounts:
        owner = accounts.execute(
            "SELECT PrincipalID FROM UserAccounts WHERE FirstName='Crossing' AND LastName='Viewer'"
        ).fetchone()[0]
    with closing(sqlite3.connect(source_db)) as database, database:
        database.execute("UPDATE prims SET OwnerID=?, OwnerMask=?, BaseMask=? WHERE SceneGroupID=?",
                         (owner, 0x9E000, 0x9E000, CROSSING_ROOT))
        database.executemany(
            "INSERT INTO primitems VALUES (?, ?, ?, ?, 10, 10, 'Crossing script', '', 1700000000, ?, ?, ?, "
            "'00000000-0000-0000-0000-000000000000', 647168, 647168, 647168, 0, 0, 0)",
            [
                (CROSSING_SCRIPT_ITEM, CROSSING_ROOT, CROSSING_SCRIPT_ASSET, CROSSING_ROOT, owner, owner, owner),
                (CHILD_SCRIPT_ITEM, CROSSING_CHILD, CHILD_SCRIPT_ASSET, CROSSING_CHILD, owner, owner, owner),
            ],
        )
    install_crossing_scripts(source_assets)
    asset_reads = set()

    class AssetHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.headers.get("Authorization") != "Basic " + base64.b64encode(b"grid:asset-fixture").decode():
                self.send_error(401)
                return
            asset = self.path.removeprefix("/assets/")
            if self.path != f"/assets/{asset}" or asset not in (CROSSING_SCRIPT_ASSET, CHILD_SCRIPT_ASSET):
                self.send_error(404)
                return
            with closing(sqlite3.connect(source_assets)) as database:
                data, asset_type = database.execute("SELECT Data, Type FROM assets WHERE UUID=?",
                                                    (asset,)).fetchone()
            asset_reads.add(asset)
            body = (f"<AssetBase><Data>{base64.b64encode(data).decode()}</Data>"
                    f"<FullID><Guid>{asset}</Guid></FullID><Type>{asset_type}</Type>"
                    f"<ID>{asset}</ID><Name>Crossing script</Name></AssetBase>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/xml")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    with ThreadingHTTPServer(("127.0.0.1", 0), AssetHandler) as asset_server:
        Thread(target=asset_server.serve_forever, daemon=True).start()
        with destination_config.open("a", encoding="utf-8") as settings:
            settings.write(f"AssetServerURI=http://127.0.0.1:{asset_server.server_port}\n")
        try:
            run_crossing_pair(executable, config, source_db, destination_config, destination_db)
        finally:
            asset_server.shutdown()
    assert asset_reads == {CROSSING_SCRIPT_ASSET, CHILD_SCRIPT_ASSET}, asset_reads
    with closing(sqlite3.connect(destination_db)) as database:
        physics = database.execute(
            "SELECT GravityModifier, Restitution, Friction, Density FROM prims WHERE UUID=?",
            (CROSSING_ROOT,),
        ).fetchone()
        assert physics == ("-0.5", "0.25", "2.5", "1234"), physics
        owners = database.execute("SELECT OwnerID FROM prims WHERE SceneGroupID=?", (CROSSING_ROOT,)).fetchall()
        assert owners == [(owner,), (owner,)], owners
        task = database.execute("SELECT primID, assetID FROM primitems WHERE itemID=?",
                                (CROSSING_SCRIPT_ITEM,)).fetchone()
        assert task == (CROSSING_ROOT, CROSSING_SCRIPT_ASSET), task
        child_task = database.execute("SELECT primID, assetID FROM primitems WHERE itemID=?",
                                      (CHILD_SCRIPT_ITEM,)).fetchone()
        assert child_task == (CROSSING_CHILD, CHILD_SCRIPT_ASSET), child_task


def run_crossing_pair(executable, config, source_db, destination_config, destination_db):
    with running_server(executable, destination_config, region=True) as (destination_url, destination_udp):
        with config.open("a", encoding="utf-8") as source:
            source.write(f"\n[GridService]\nGridServerURI={destination_url}\n"
                         f"\n[PresenceService]\nConnectionString={config.parent / 'presence.db'}\n")
        with running_server(executable, config, region=True) as (source_url, source_udp):
            wait_until(lambda: (crossing_rows(source_db)[0] or ("", "", ""))[2] == "122",
                       "Source script did not reach its sleep checkpoint")
            move_linkset_as_viewer(source_url, source_udp, destination_udp)
            wait_until(lambda: crossing_rows(destination_db)[1] is not None,
                       "Destination did not persist the crossed linkset")
            check_crossing(destination_db, ("4", "121", "30"))
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                destination_root, _ = crossing_rows(destination_db)
                if destination_root and destination_root[3] == "31":
                    break
                time.sleep(0.05)
            else:
                raise AssertionError("Destination did not resume the sleeping script")
            check_crossing(destination_db, ("5", "121", "31"), "")
        assert crossing_rows(source_db) == (None, None)
        with closing(sqlite3.connect(source_db)) as database:
            assert database.execute("SELECT itemID FROM primitems WHERE itemID IN (?, ?)",
                                    (CROSSING_SCRIPT_ITEM, CHILD_SCRIPT_ITEM)).fetchone() is None
    check_crossing(destination_db, ("5", "121", "31"), "")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    assets = Path(__file__).resolve().parents[2] / "bin" / "assets" / "AssetSets.xml"
    with tempfile.TemporaryDirectory(dir=args.work.resolve()) as directory:
        root = Path(directory)
        region_db = root / "terrain.db"
        asset_db = root / "assets.db"
        config = root / "region.ini"
        create_region_fixture(region_db)
        config.write_text(
            "[NativeServer]\nEnableLocalRegion=true\nEnableRegionAssets=true\n"
            "EnableAvatarService=true\nAvatarDatabase=avatars.db\n"
            "EnableSimulationEndpoint=true\nEnablePresenceEndpoint=true\nListenAddress=127.0.0.1\n"
            "UserAccountDatabase=users.db\nAuthenticationDatabase=users.db\n"
            "InventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db\n"
            "[Network]\nhttp_listener_port=0\n"
            "[NativeRegion]\nName=Legacy Material Restart\nUDPPort=0\nBindAddress=127.0.0.1\n"
            f"ExternalHost=127.0.0.1\nTerrainDatabase=terrain.db\nID={REGION}\n"
            "[Terrain]\nInitialTerrain=flat\n"
            "[Modules]\nAssetServices=LocalAssetServicesConnector\n"
            "[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\n"
            "StorageProvider=OpenSim.Data.SQLite.dll\n"
            f"ConnectionString=URI=file:{asset_db};version=3;Pooling=false\n"
            "DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll\n"
            f"AssetLoaderArgs={assets}\n",
            encoding="utf-8",
        )
        subprocess.run([args.native, "--config", str(config), "--init-db"], check=True, timeout=20)
        with running_server(args.native, config, region=True) as (url, _):
            assert read_attributes(region_db) == EMBEDDED
            assert read_asset(asset_db) is None
            cross_linkset(url)

        attributes = read_attributes(region_db)
        keys = [key.text for key in ElementTree.fromstring(attributes).iter("key")]
        assert keys == ["OpenSim", "OtherStore", "note"], attributes
        assert ElementTree.fromstring(attributes).find(".//string").text == "keep"
        asset = read_asset(asset_db)
        assert asset and int(asset[0]) == -2, asset
        fields = list(ElementTree.fromstring(asset[1]).find("map"))
        material = {key.text: value.text for key, value in zip(fields[::2], fields[1::2])}
        assert material["DiffuseAlphaMode"] == "2" and material["NormRepeatX"] == "10000"
        check_crossing(region_db)

        with running_server(args.native, config, region=True) as (url, _):
            assert read_attributes(region_db) == attributes
            assert read_asset(asset_db) == asset
            cross_linkset(url, already_present=True)
        assert read_attributes(region_db) == attributes
        assert read_asset(asset_db) == asset
        check_crossing(region_db)
        verify_script_restart(args.native, config, region_db, asset_db)
        verify_attachment_restart(args.native, config, region_db)
        cross_between_regions(args.native, root, config, region_db)
    print("PASS: embedded material restart and live script resumption across two native regions")


if __name__ == "__main__":
    main()
