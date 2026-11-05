"""Verify SQLite persistence for generated parcel edits and large terrain uploads."""

import argparse
from contextlib import closing
import gzip
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import uuid
from xmlrpc.client import ServerProxy

from remote_presence_network import running_server


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
        config.write_text(settings.replace("[NativeRegion]", f"[NativeRegion]\nOwnerID={owner}"),
                          encoding="utf-8")

        def login(proxy, last):
            return proxy.login_to_simulator({
                "first": "Parcel", "last": last, "passwd": "fixture-password",
                "start": "uri:Default Parcel Fixture&128&128&25", "version": "fixture",
            })

        with running_server(args.native, config, region=True) as (url, endpoint):
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
                if args.large_terrain:
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
