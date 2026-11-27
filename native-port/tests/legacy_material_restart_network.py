"""Verify embedded legacy material migration across a SQLite-backed region restart."""

import argparse
import uuid
import base64
from contextlib import closing, contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import itertools
import queue
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
import shutil
import traceback
import time
from threading import Thread
from urllib.request import Request, urlopen
from uuid import UUID
from xml.etree import ElementTree
from xmlrpc.client import ServerProxy

from remote_presence_network import wait_until


_evidence_root = None
_server_sequence = itertools.count()
_viewer_sequence = itertools.count()
_viewer_traces = []


def close_viewer_traces():
    while _viewer_traces:
        trace = _viewer_traces.pop()
        try:
            trace.flush()
        finally:
            trace.close()


@contextmanager
def running_server(executable, config, region=False):
    """Keep each host's output so a failed final crossing remains diagnosable."""
    log_path = Path(config).parent / f"server-{next(_server_sequence)}.log"
    with log_path.open("w", encoding="utf-8") as log, subprocess.Popen(
        [executable, "--config", str(config), "--console"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace",
    ) as process:
        lines = queue.Queue()
        output = []

        def read_output():
            for line in process.stdout:
                output.append(line)
                log.write(line)
                log.flush()
                lines.put(line.strip())
            lines.put(None)

        reader = Thread(target=read_output, daemon=True)
        reader.start()
        url, udp = None, None
        startup_deadline = time.monotonic() + 40
        try:
            while not url or (region and udp is None):
                remaining = startup_deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("Native host timed out during startup:\n" + "".join(output))
                try:
                    line = lines.get(timeout=remaining)
                except queue.Empty as error:
                    raise RuntimeError("Native host timed out during startup:\n" + "".join(output)) from error
                if line is None:
                    raise RuntimeError("Native host exited during startup:\n" + "".join(output))
                if line.startswith("LISTENING "):
                    url = line.removeprefix("LISTENING ").rstrip("/")
                elif line.startswith("REGION_UDP "):
                    host, port = line.removeprefix("REGION_UDP ").rsplit(":", 1)
                    udp = host, int(port)
            yield url, udp
        finally:
            if process.poll() is None:
                try:
                    process.stdin.write('"quit"\n')
                    process.stdin.flush()
                    process.wait(timeout=10)
                except (OSError, subprocess.TimeoutExpired):
                    if process.poll() is None:
                        process.kill()
                    process.wait()
            reader.join(timeout=5)
        assert process.returncode == 0, "".join(output)


@contextmanager
def retained_workspace(parent):
    global _evidence_root
    parent = parent.resolve()
    root = Path(tempfile.mkdtemp(prefix="legacy-material-", dir=parent)).resolve()
    _evidence_root = root
    try:
        yield root
    except BaseException as error:
        close_viewer_traces()
        report = dict(workspace=str(root), error=str(error), traceback=traceback.format_exc())
        (parent / "failure.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"FAILED WORKSPACE RETAINED: {root}", flush=True)
        raise
    else:
        close_viewer_traces()
        # Remove only this fixture's verified, newly allocated workspace.
        assert root.parent == parent and root.name.startswith("legacy-material-")
        shutil.rmtree(root)
    finally:
        close_viewer_traces()
        _evidence_root = None


REGION = "11111111-1111-4111-8111-111111111111"
DESTINATION = "11111111-1111-4111-8111-111111111112"
PRIM = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"
OWNER = "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
MATERIAL = "12345678-1234-1234-1234-123456789abc"
CROSSING_ROOT = "c7cb523a-f9ab-4a56-9f8d-44f239b8b00e"
CROSSING_CHILD = "82ad9a50-b8b4-460a-aa2b-bec87f1d45b2"
CROSSING_CYPRESS = "c7cb523a-f9ab-4a56-9f8d-44f239b8b00f"
PHANTOM_FLAG = 0x400
VOLUME_DETECT_FLAG = 1 << 14
CROSSING_CAMERA_OFFSETS = (1.25, -2.5, 3.75, -0.5, 0.25, 1.5)
CROSSING_ROTATION_LOCKS = 5
CROSSING_LINKSET_DATA = (
    b"\x01\x24\x04\x08crossing\x05ready\x00\x06secret\x04kept\x04pass"
    b"\x06legacy\x01\xff\x00\x08overflow\x05value\x00"
)
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
        osSetInertiaAsBox(18.0, <2.0, 3.0, 4.0>, <0.25, -0.5, 0.75>,
                          <0.0, 0.0, 0.6, 0.8>);
        llSitTarget(<0,0,1>, ZERO_ROTATION);
        llSetRegionPos(<120,122,30>);
        llSleep(10.0);
        list material = llGetPhysicsMaterial();
        if (llList2Float(material, 0) != -0.5 || llList2Float(material, 1) != 0.25 ||
            llList2Float(material, 2) != 2.5 || llList2Float(material, 3) != 1234)
            return;
        if (counter + increment == 42 && llGetRegionName() == "Crossing Destination") {
            if (llLinksetDataRead("crossing") != "ready" ||
                llLinksetDataReadProtected("secret", "pass") != "kept" ||
                llLinksetDataReadProtected("secret", "wrong") != "" ||
                llLinksetDataRead("legacy") != llChar(65533) ||
                llLinksetDataRead("overflow") != "" ||
                llLinksetDataAvailable() != 0 ||
                llLinksetDataWrite("crossing", "read") != LINKSETDATA_EMEMORY ||
                llLinksetDataCountFound("oss") != 1 ||
                llList2CSV(llLinksetDataDeleteFound("ecre", "wrong")) != "0, 1" ||
                llLinksetDataReadProtected("secret", "pass") != "kept")
                return;
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
            && changed == (CONTROL_FWD | CONTROL_LBUTTON)) {
            list details = llGetObjectDetails(rider, [OBJECT_POS, OBJECT_ROOT]);
            vector offset = llList2Vector(details, 0) - llGetRootPosition();
            if (llList2String(details, 1) == (string)llGetLinkKey(LINK_ROOT) &&
                llVecDist(offset, <0, 0, 1.35>) < 0.001)
                llSay(0, "Rider controls survived the crossing");
        }
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


CREATOR_HOME = "http://creator.example.test:8002"
CREATOR_NAME = "Outside Resident"


def check_creator_identification(path, part_id):
    with closing(sqlite3.connect(path)) as database:
        row = database.execute("SELECT CreatorID FROM prims WHERE UUID=?", (part_id,)).fetchone()
        assert row == (f"{OWNER};{CREATOR_HOME}/;{CREATOR_NAME}",), row


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
        "CameraEyeOffsetX": 0, "CameraEyeOffsetY": 0, "CameraEyeOffsetZ": 0,
        "CameraAtOffsetX": 0, "CameraAtOffsetY": 0, "CameraAtOffsetZ": 0,
        "RotationAxisLocks": 0,
        "lnkstBinData": b"",
        "DynAttrs": EMBEDDED, "sopanims": None, "TextureAnimation": "", "PhysInertia": None,
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
        integer removed_seed = llLinksetDataDelete("s");
        integer wrote_short = llLinksetDataWrite("k", "v");
        string read_short = llLinksetDataRead("k");
        integer key_count = llLinksetDataCountKeys();
        integer removed_short = llLinksetDataDelete("k");
        if (removed_seed != LINKSETDATA_OK || wrote_short != LINKSETDATA_OK ||
            read_short != "v" || key_count != 1 || removed_short != LINKSETDATA_OK) {
            llSetObjectDesc("Incorrect live linkset capacity: minimum-length record " +
                llList2CSV([removed_seed, wrote_short, read_short, key_count, removed_short]));
            return;
        }
        llLinksetDataReset();
        integer write_result = llLinksetDataWrite("live", "value");
        integer available = llLinksetDataAvailable();
        if (write_result != LINKSETDATA_OK || available != 299991) {
            llSetObjectDesc("Incorrect live linkset capacity: " + (string)write_result + "/" + (string)available);
            return;
        }
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
        if (llGetPermissions() == granted && llGetPermissionsKey() == granter &&
            llLinksetDataRead("live") == "value" && llLinksetDataAvailable() == 262135)
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
        database.execute("UPDATE prims SET lnkstBinData=? WHERE UUID=?",
                         (b"\x01\x40\x01\x01s\x02ok\x00", PRIM))
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
            assert not read_state()[0].startswith("Incorrect live linkset capacity"), read_state()[0]
            send_chat(8125, b"request grant")
            question = receive("ffff00bc")
            assert question[4:36] == UUID(PRIM).bytes + UUID(RESTART_SCRIPT_ITEM).bytes
            send("ffff0084", identity + question[4:36] + struct.pack("<i", 0x10))
            wait_until(lambda: read_state()[0] == "Restart checkpoint", "Script did not save its granted checkpoint")
            checkpoint = read_state()[1]
            assert len(checkpoint) >= 4 and checkpoint[:3] == b"LSC" and checkpoint[3] >= 3, \
                "permission callback must persist a permission-capable checkpoint"
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
        CROSSING_ROOT, f"<CreatorID><UUID>{OWNER}</UUID></CreatorID>"
        f"<CreatorData>{CREATOR_HOME}/;{CREATOR_NAME}</CreatorData>"
        f"<SOPAnims>{animation}</SOPAnims>"
        f"<TextureAnimation>{CROSSING_TEXTURE_ANIMATION}</TextureAnimation>"
        "<Flags>Phantom</Flags><VolumeDetectActive>1</VolumeDetectActive>"
        "<CameraEyeOffset><X>1.25</X><Y>-2.5</Y><Z>3.75</Z></CameraEyeOffset>"
        "<CameraAtOffset><X>-0.5</X><Y>0.25</Y><Z>1.5</Z></CameraAtOffset>"
        f"<RotationAxisLocks> +{CROSSING_ROTATION_LOCKS} </RotationAxisLocks>"
        "<PhysicsInertia><MASS>12</MASS>"
        "<CM><X>0.75</X><Y>0</Y><Z>0</Z></CM>"
        "<INERTIA><X>4</X><Y>5</Y><Z>6</Z></INERTIA>"
        "<IROT><X>0</X><Y>0</Y><Z>0</Z><W>1</W></IROT></PhysicsInertia>",
    )
    child = part(CROSSING_CHILD, "")
    encoded = base64.b64encode(CROSSING_LINKSET_DATA).decode()
    xml = (f"<SceneObjectGroup>{root}<OtherParts>{child}</OtherParts>"
           f"<lnkstdt>{encoded}</lnkstdt></SceneObjectGroup>")
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
            "TextureAnimation, ObjectFlags, CameraEyeOffsetX, CameraEyeOffsetY, CameraEyeOffsetZ, "
            "CameraAtOffsetX, CameraAtOffsetY, CameraAtOffsetZ, RotationAxisLocks FROM prims WHERE UUID=?",
            (CROSSING_ROOT,),
        ).fetchone()
        child = database.execute("SELECT SceneGroupID FROM prims WHERE UUID=?", (CROSSING_CHILD,)).fetchone()
    return root, child


def check_crossing(path, position=("120", "121", "30"), texture_animation=CROSSING_TEXTURE_ANIMATION):
    root, child = crossing_rows(path)
    assert root and root[:6] == (CROSSING_ROOT, *position, CROSSING_ANIMATION, texture_animation), root
    collision_flags = PHANTOM_FLAG | VOLUME_DETECT_FLAG
    assert int(root[6]) & collision_flags == collision_flags, root
    assert tuple(map(float, root[7:-1])) == CROSSING_CAMERA_OFFSETS, root
    assert int(root[-1]) == CROSSING_ROTATION_LOCKS, root
    assert child == (CROSSING_ROOT,), child
    with closing(sqlite3.connect(path)) as database:
        data = database.execute("SELECT lnkstBinData FROM prims WHERE UUID=?", (CROSSING_ROOT,)).fetchone()
    assert data == (CROSSING_LINKSET_DATA,), data


def check_crossing_inertia(path, expected_mass, expected_center, expected_axes, expected_rotation):
    with closing(sqlite3.connect(path)) as database:
        rows = dict(database.execute(
            "SELECT UUID, PhysInertia FROM prims WHERE UUID IN (?, ?)",
            (CROSSING_ROOT, CROSSING_CHILD),
        ))
    assert not rows[CROSSING_CHILD], "The root override must not be copied onto the child"
    inertia = ElementTree.fromstring(rows[CROSSING_ROOT])
    assert inertia.tag == "PhysicsInertia", rows[CROSSING_ROOT]

    def vector(field, components="XYZ"):
        return tuple(float(inertia.findtext(f"{field}/{component}")) for component in components)

    assert float(inertia.findtext("MASS")) == expected_mass
    assert vector("CM") == expected_center
    assert vector("INERTIA") == expected_axes
    assert all(abs(a - b) < 1e-6 for a, b in zip(vector("IROT", "XYZW"), expected_rotation))


def viewer_channel(viewer, endpoint, *, receive_peer=False):
    sequence = 0
    viewer.settimeout(5)
    trace = (_evidence_root / f"viewer-{next(_viewer_sequence)}.jsonl").open("w", encoding="utf-8") if _evidence_root else None
    if trace:
        _viewer_traces.append(trace)
    last_peer = {}

    def record(direction, peer, payload, **fields):
        if trace:
            trace.write(json.dumps(dict(time=time.monotonic(), direction=direction, peer=peer,
                                        payload_hex=bytes(payload).hex(), **fields)) + "\n")
            trace.flush()

    def send(packet_id, data, reliable=True, target=endpoint):
        nonlocal sequence
        sequence += 1
        header = struct.pack(">BIB", 0x40 if reliable else 0, sequence, 0)
        viewer.sendto(header + bytes.fromhex(packet_id) + data, target)
        record("send", target, bytes.fromhex(packet_id) + data, sequence=sequence, reliable=reliable)

    def receive():
        datagram, peer = viewer.recvfrom(8192)
        last_peer["endpoint"] = peer
        if datagram[0] & 0x40:
            reply_sequence = struct.unpack_from(">I", datagram, 1)[0]
            send("fffffffb", struct.pack("<BI", 1, reply_sequence), reliable=False, target=peer)
        payload = datagram[6 + datagram[5]:]
        if not datagram[0] & 0x80:
            record("receive", peer, payload, sequence=struct.unpack_from(">I", datagram, 1)[0])
            return (payload, peer) if receive_peer else payload
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
        record("receive", peer, decoded, sequence=struct.unpack_from(">I", datagram, 1)[0])
        return (decoded, peer) if receive_peer else decoded

    receive.last_peer = last_peer
    return send, receive

def verify_inventory_creator_attribution(executable, config, region_db):
    with closing(sqlite3.connect(config.parent / "users.db")) as accounts:
        owner = accounts.execute(
            "SELECT PrincipalID FROM UserAccounts WHERE FirstName='Restart' AND LastName='Viewer'"
        ).fetchone()[0]
    first_creator = f"{OWNER};{CREATOR_HOME}/;{CREATOR_NAME}"
    second_creator = f"{OWNER};{CREATOR_HOME}/;Different Resident"
    with closing(sqlite3.connect(region_db)) as database, database:
        original_rows = database.execute(
            "SELECT OwnerID, CreatorID, UUID FROM prims WHERE UUID IN (?, ?)", (PRIM, CROSSING_ROOT)
        ).fetchall()
        for part_id, creator in ((PRIM, first_creator), (CROSSING_ROOT, second_creator)):
            database.execute("UPDATE prims SET OwnerID=?, CreatorID=? WHERE UUID=?", (owner, creator, part_id))

    with running_server(executable, config, region=True) as (url, endpoint):
        with ServerProxy(url + "/") as login, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
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
            local_ids = {}
            targets = {UUID(PRIM).bytes, UUID(CROSSING_ROOT).bytes}
            deadline = time.monotonic() + 10
            while targets - local_ids.keys() and time.monotonic() < deadline:
                update = receive()
                object_id = bytes(update[17:33])
                if update[:1] == b"\x0c" and object_id in targets:
                    local_ids[object_id] = struct.unpack_from("<I", update, 12)[0]
            assert len(local_ids) == 2, "Viewer did not see both creator fixtures"

            def take_copy(parts):
                ids = b"".join(struct.pack("<I", local_ids[UUID(part).bytes]) for part in parts)
                send("ffff0123", agent + session_id + bytes(16) + bytes([1]) + bytes(16)
                     + uuid.uuid4().bytes + bytes([1, 0, len(parts)]) + ids)

            def creators():
                with closing(sqlite3.connect(config.parent / "inventory.db")) as database:
                    return {row[0] for row in database.execute(
                        "SELECT creatorID FROM inventoryitems WHERE avatarID=? AND invType=6", (owner,)
                    )}

            take_copy([PRIM])
            wait_until(lambda: first_creator in creators(), "Take Copy lost the object creator details")
            take_copy([PRIM, CROSSING_ROOT])
            wait_until(lambda: str(UUID(int=0)) in creators(),
                       "Combined Take Copy retained attribution for conflicting creator details")
    with closing(sqlite3.connect(region_db)) as database, database:
        database.executemany("UPDATE prims SET OwnerID=?, CreatorID=? WHERE UUID=?", original_rows)



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


def verify_empty_inventory_serial(url, send, receive, agent, session):
    object_id = UUID("b740227c-d69b-4d22-9ad1-f2702183ae1e")
    serial = 37
    owner = UUID(bytes=agent)
    xml = (
        "<SceneObjectGroup><SceneObjectPart>"
        f"<UUID><UUID>{object_id}</UUID></UUID><OwnerID><UUID>{owner}</UUID></OwnerID>"
        f"<InventorySerial>{serial}</InventorySerial><Shape><PCode>9</PCode></Shape>"
        "</SceneObjectPart><OtherParts /></SceneObjectGroup>"
    )
    body = json.dumps({"destination_uuid": REGION, "sog": xml, "new_position": [122, 122, 30]}).encode()
    request = Request(f"{url}/object/{object_id}/", body, {"Content-Type": "application/json"})
    with urlopen(request, timeout=5) as response:
        assert response.read() == b"True"

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        packet = receive()
        if packet[:1] == b"\x0c" and packet[17:33] == object_id.bytes:
            local_id = struct.unpack_from("<I", packet, 12)[0]
            break
    else:
        raise AssertionError("Viewer did not receive the transferred object with empty inventory")

    send("ffff006e", agent + session + struct.pack("<BI", 1, local_id))
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        packet = receive()
        if packet[:3] == b"\xff\x09\x01" and packet[3:19] == object_id.bytes:
            # Skip the packet ID, block count, and fields through Category.
            serial_offset = 3 + struct.calcsize("<64sQ5IiBi3BI")
            received_serial = struct.unpack_from("<h", packet, serial_offset)[0]
            assert received_serial == serial, received_serial
            return
    raise AssertionError("Viewer did not receive the transferred object's inventory serial")


def move_linkset_as_viewer(url, endpoint, destination_endpoint, source_db, destination_db):
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
        tree_target = UUID(CROSSING_CYPRESS).bytes
        local_id = tree_local_id = None
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            update = receive()
            if update[:1] == b"\x0c" and update[17:33] == target:
                local_id = struct.unpack_from("<I", update, 12)[0]
            if update[:1] == b"\x0c" and update[17:33] == tree_target:
                tree_local_id = struct.unpack_from("<I", update, 12)[0]
            if local_id is not None and tree_local_id is not None:
                break
        else:
            raise AssertionError("Viewer did not receive the source linkset and Cypress")
        verify_empty_inventory_serial(url, send, receive, agent, session_id)
        send("ff04", agent + session_id + struct.pack("<BI3f", 1, tree_local_id, 261, 125, 31))
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

        def decode_llsd(value):
            if value.tag == "llsd":
                return decode_llsd(value[0])
            if value.tag == "map":
                entries = list(value)
                return {key.text: decode_llsd(item) for key, item in zip(entries[::2], entries[1::2])}
            if value.tag == "array":
                return [decode_llsd(item) for item in value]
            if value.tag == "integer":
                return int(value.text or 0)
            if value.tag == "binary":
                return base64.b64decode(value.text or "")
            return value.text or ""

        event_ack = {}
        pending_crossings = {}

        def poll_events(capability):
            ack = f"<integer>{event_ack[capability]}</integer>" if capability in event_ack else "<undef/>"
            result = decode_llsd(post_capability(capability,
                ("<llsd><map><key>ack</key>" + ack +
                 "<key>done</key><boolean>0</boolean></map></llsd>").encode()))
            event_ack[capability] = result["id"]
            return result["events"]

        def establish_child(capability, send_packet, receive_packet, rider, rider_session, circuit):
            enabled = established = False
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not (enabled and established):
                for event in poll_events(capability):
                    if event["message"] == "EnableSimulator":
                        for simulator in event["body"]["SimulatorInfo"]:
                            announced = socket.inet_ntoa(simulator["IP"]), int(simulator["Port"])
                            if announced == destination_endpoint:
                                send_packet("ffff0003", circuit + rider_session + rider, target=announced)
                                enabled = True
                    elif event["message"] == "EstablishAgentCommunication":
                        established |= event["body"]["sim-ip-and-port"] == ":".join(map(str, destination_endpoint))
                    elif event["message"] == "CrossedRegion":
                        pending_crossings.setdefault(capability, []).append(event)
            assert enabled and established, "Viewer was not told to connect to the destination child simulator"
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                update = receive_packet()
                if receive_packet.last_peer["endpoint"] == destination_endpoint and update[:4] == bytes.fromhex("ffff0094"):
                    send_packet("ffff0095", rider + rider_session + struct.pack("<I", 0), target=destination_endpoint)
                    return
            raise AssertionError("Destination child circuit did not send its region handshake")

        establish_child(event_queue, send, receive, agent, session_id, code)
        establish_child(passenger_queue, send_passenger, receive_passenger,
                        passenger_agent, passenger_session, passenger_code)

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
        # Regular seated edge crossings delete the source silently in original
        # SceneObjectGroup.CrossAsync / EntityTransferModule. A rider must not
        # wait for KillObject before acknowledging its crossing event.
        def crossed_parts(path):
            with closing(sqlite3.connect(f"file:{path.resolve().as_posix()}?mode=ro", uri=True)) as database:
                return {row[0] for row in database.execute(
                    "SELECT UUID FROM prims WHERE UUID IN (?, ?)", (CROSSING_ROOT, CROSSING_CHILD))}

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if not crossed_parts(source_db) and crossed_parts(destination_db) == {CROSSING_ROOT, CROSSING_CHILD}:
                break
            time.sleep(0.025)
        else:
            raise AssertionError("Crossed root/child were not removed from source and persisted at destination")

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
                events = pending_crossings.pop(event_queue, []) or poll_events(event_queue)
                if any(event["message"] == "CrossedRegion" for event in events):
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

        send("06", agent + session_id + target + struct.pack("<3f", 0, 0, 0),
             target=destination_endpoint)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            packet = receive_destination()
            if packet[:1] == b"\x15" and packet[1:17] == target:
                # High-frequency ID, target UUID, autopilot byte, position and packed rotation.
                camera_offset = struct.calcsize("<B16sB6f")
                offsets = struct.unpack_from("<6f", packet, camera_offset)
                assert offsets == CROSSING_CAMERA_OFFSETS, offsets
                break
        else:
            raise AssertionError("Viewer did not receive destination sit camera offsets")


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
        database.row_factory = sqlite3.Row
        tree = dict(database.execute("SELECT * FROM prims WHERE UUID=?", (CROSSING_ROOT,)).fetchone())
        tree.update(UUID=CROSSING_CYPRESS, SceneGroupID=CROSSING_CYPRESS, Name="Crossing Cypress",
                    ObjectFlags=0x400, GroupPositionX=119, GroupPositionY=125, GroupPositionZ=31,
                    AttachedPosX=0.25, AttachedPosY=-0.5, AttachedPosZ=1.5)
        database.execute("INSERT INTO prims (" + ", ".join(tree) + ") VALUES (" +
                         ", ".join("?" for _ in tree) + ")", tuple(tree.values()))
        shape = dict(database.execute("SELECT * FROM primshapes WHERE UUID=?", (CROSSING_ROOT,)).fetchone())
        shape.update(UUID=CROSSING_CYPRESS, PCode=111, State=7, LastAttachPoint=7,
                     ScaleX=4, ScaleY=4, ScaleZ=10)
        database.execute("INSERT INTO primshapes (" + ", ".join(shape) + ") VALUES (" +
                         ", ".join("?" for _ in shape) + ")", tuple(shape.values()))
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
    check_crossing_inertia(destination_db, 18.0, (0.25, -0.5, 0.75), (37.5, 30.0, 19.5), (0, 0, 0.6, 0.8))
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
    def cypress_row(path):
        with closing(sqlite3.connect(path)) as database:
            return database.execute(
                "SELECT s.PCode,s.State,s.LastAttachPoint,s.ScaleX,s.ScaleY,s.ScaleZ,"
                "p.GroupPositionX,p.GroupPositionY,p.GroupPositionZ,"
                "p.AttachedPosX,p.AttachedPosY,p.AttachedPosZ,p.ObjectFlags "
                "FROM prims p JOIN primshapes s ON p.UUID=s.UUID WHERE p.UUID=?", (CROSSING_CYPRESS,),
            ).fetchone()

    with running_server(executable, destination_config, region=True) as (destination_url, destination_udp):
        with config.open("a", encoding="utf-8") as source:
            source.write(f"\n[GridService]\nGridServerURI={destination_url}\n"
                         f"\n[PresenceService]\nConnectionString={config.parent / 'presence.db'}\n")
        with running_server(executable, config, region=True) as (source_url, source_udp):
            wait_until(lambda: (crossing_rows(source_db)[0] or ("", "", ""))[2] == "122",
                       "Source script did not reach its sleep checkpoint")
            move_linkset_as_viewer(source_url, source_udp, destination_udp, source_db, destination_db)
            wait_until(lambda: cypress_row(destination_db) is not None,
                       "Destination did not persist the crossed Cypress")
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
                raise AssertionError("Destination did not resume the sleeping script with its linkset data")
            check_crossing(destination_db, ("5", "121", "31"), "")
        assert crossing_rows(source_db) == (None, None)
        with closing(sqlite3.connect(source_db)) as database:
            assert database.execute("SELECT UUID FROM prims WHERE UUID=?", (CROSSING_CYPRESS,)).fetchone() is None
            assert database.execute("SELECT itemID FROM primitems WHERE itemID IN (?, ?)",
                                    (CROSSING_SCRIPT_ITEM, CHILD_SCRIPT_ITEM)).fetchone() is None
    check_crossing(destination_db, ("5", "121", "31"), "")
    check_creator_identification(destination_db, CROSSING_ROOT)
    tree = cypress_row(destination_db)
    assert tree and tuple(map(float, tree[:-1])) == (
        111, 7, 7, 4, 4, 10, 5, 125, 31, 0.25, -0.5, 1.5
    ), tree
    assert int(tree[-1]) & PHANTOM_FLAG, tree


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    bundled_assets = Path(args.native).resolve().parent / "share" / "opensim-native" / "assets" / "AssetSets.xml"
    source_assets = Path(__file__).resolve().parents[2] / "bin" / "assets" / "AssetSets.xml"
    assets = bundled_assets if bundled_assets.is_file() else source_assets
    with retained_workspace(args.work.resolve()) as directory:
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
            "[YEngine]\nLinksetDataLimit=300000\n"
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
        check_creator_identification(region_db, CROSSING_ROOT)
        check_crossing(region_db)
        check_crossing_inertia(region_db, 12.0, (0.75, 0, 0), (4, 5, 6), (0, 0, 0, 1))

        with running_server(args.native, config, region=True) as (url, _):
            assert read_attributes(region_db) == attributes
            assert read_asset(asset_db) == asset
            cross_linkset(url, already_present=True)
        assert read_attributes(region_db) == attributes
        assert read_asset(asset_db) == asset
        check_creator_identification(region_db, CROSSING_ROOT)
        check_crossing(region_db)
        check_crossing_inertia(region_db, 12.0, (0.75, 0, 0), (4, 5, 6), (0, 0, 0, 1))
        verify_script_restart(args.native, config, region_db, asset_db)
        verify_attachment_restart(args.native, config, region_db)
        verify_inventory_creator_attribution(args.native, config, region_db)
        cross_between_regions(args.native, root, config, region_db)
    print("PASS: embedded material restart and live script resumption across two native regions")


if __name__ == "__main__":
    main()
