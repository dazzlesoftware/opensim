"""Exercise a native grid region against a separate native Robust presence service."""
import argparse
import base64
from contextlib import closing, contextmanager
import json
from pathlib import Path
import queue
import socket
import sqlite3
import struct
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import xml.etree.ElementTree as ET


@contextmanager
def running_server(executable, config, region=False):
    with subprocess.Popen([executable, "--config", config, "--console"], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace") as process:
        lines = queue.Queue()
        output = []

        def read():
            for line in process.stdout:
                output.append(line)
                lines.put(line.strip())
            lines.put(None)

        threading.Thread(target=read, daemon=True).start()
        url, udp = None, None
        try:
            while not url or (region and udp is None):
                line = lines.get(timeout=15)
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
        assert process.returncode == 0, "".join(output)


def wait_until(predicate, description):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError(description)


def request_status(url, data):
    try:
        with urllib.request.urlopen(url, data, timeout=5) as response:
            return response.status
    except urllib.error.HTTPError as error:
        with error:
            return error.code


def exercise_region(executable, root, private_url, seed, location, grid_user_url=None, local_grid_user=False):
    region = uuid.UUID("11111111-1111-4111-8111-111111111111")
    region_config = root / "region.ini"
    settings = (
        "[NativeServer]\nListenAddress=127.0.0.1\nEnableLocalRegion=true\n"
        "EnableSimulationEndpoint=true\nUserAccountDatabase=users.db\nAuthenticationDatabase=users.db\n"
        "InventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db\n"
        "EnableFriendsService=true\nFriendsDatabase=friends.db\n"
        "[Network]\nhttp_listener_port=0\n"
        f"[NativeRegion]\nID={region}\nName=Remote Presence Fixture\nUDPPort=0\n"
        "BindAddress=127.0.0.1\nExternalHost=127.0.0.1\n"
        "[Modules]\nPresenceServices=RemotePresenceServicesConnector\n"
        f"[PresenceService]\nPresenceServerURI={private_url}/\nAuthType=BasicHttpAuthentication\n"
        "HttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n")
    if grid_user_url:
        settings = settings.replace("[Modules]", "[Modules]\nGridUserServices=RemoteGridUserServicesConnector")
        settings += (
            f"[GridUserService]\nGridUserServerURI={grid_user_url}/\nAuthType=BasicHttpAuthentication\n"
            "HttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n")
    elif local_grid_user:
        settings = settings.replace("[NativeServer]", "[NativeServer]\nEnableGridUserEndpoint=true")
    region_config.write_text(settings, encoding="utf-8")
    command = [executable, "--config", region_config]
    subprocess.run(command + ["--init-db"], check=True, timeout=20)
    subprocess.run(command + ["--create-user", "Remote", "Resident"], input="fixture-password\n",
                   text=True, check=True, timeout=20)
    with closing(sqlite3.connect(root / "users.db")) as connection:
        agent = uuid.UUID(connection.execute("SELECT PrincipalID FROM UserAccounts WHERE FirstName='Remote'")
                          .fetchone()[0])
    if grid_user_url or local_grid_user:
        # The default parcel belongs to this region's estate owner.
        region_config.write_text(settings.replace("[NativeRegion]", f"[NativeRegion]\nOwnerID={agent}"),
                                 encoding="utf-8")

    def grid_user_request(endpoint, method):
        body = urllib.parse.urlencode({"METHOD": method, "UserID": str(agent)}).encode()
        auth = base64.b64encode(b"presence:fixture-password").decode()
        request = urllib.request.Request(endpoint + "/griduser", body, {"Authorization": f"Basic {auth}"})
        with urllib.request.urlopen(request, timeout=5) as response:
            return ET.fromstring(response.read())

    if grid_user_url:
        assert grid_user_request(grid_user_url, "loggedin").findtext("result/Online") == "True"
    stale, session, wrong_owner_session, shutdown_session = (uuid.uuid4() for _ in range(4))
    friends = [uuid.uuid4(), uuid.uuid4()]
    with closing(sqlite3.connect(root / "friends.db")) as connection, connection:
        for friend in friends:
            connection.executemany("INSERT INTO Friends (PrincipalID,Friend,Flags) VALUES (?,?,1)",
                                   ((str(agent), str(friend)), (str(friend), str(agent))))
            seed(uuid.uuid4(), friend, uuid.uuid4())
    seed(stale, agent, region)
    seed(session, agent, uuid.UUID(int=0))
    seed(wrong_owner_session, uuid.uuid4(), uuid.UUID(int=0))

    with running_server(executable, region_config, region=True) as (region_url, udp):
        assert location(stale) is None, "Region startup retained stale presence"
        home_service = region_url if local_grid_user else grid_user_url
        if local_grid_user:
            assert grid_user_request(home_service, "loggedin").findtext("result/Online") == "True"
        code = 93817
        circuit = {"agent_id": str(agent), "session_id": str(session),
                   "secure_session_id": str(uuid.uuid4()), "circuit_code": str(code),
                   "destination_uuid": str(region), "first_name": "Remote", "last_name": "Resident",
                   "caps_path": "presence-fixture", "start_pos": "<128,128,45>", "teleport_flags": 128}

        def admit(fields):
            request = urllib.request.Request(region_url + f"/agent/{agent}/", json.dumps(fields).encode(),
                                             {"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=5) as response:
                return json.load(response)

        unknown = admit(circuit | {"session_id": str(uuid.uuid4())})
        assert not unknown.get("success", False) and unknown.get("reason") == "Invalid agent session", unknown
        wrong_owner = admit(circuit | {"session_id": str(wrong_owner_session)})
        assert not wrong_owner.get("success", False) and wrong_owner.get("reason") == "Invalid agent session"
        accepted = admit(circuit)
        assert accepted["success"], accepted
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as viewer:
            viewer.settimeout(8)
            circuit_bytes = struct.pack("<I", code)

            def packet(sequence, packet_id, payload):
                viewer.sendto(struct.pack(">BIB", 0x40, sequence, 0) +
                              bytes.fromhex(packet_id) + payload, udp)

            packet(1, "ffff0003", circuit_bytes + session.bytes + agent.bytes)
            packet(2, "ffff00f9", agent.bytes + session.bytes + circuit_bytes)
            moved_position, online = None, False
            for _ in range(64):
                data = viewer.recv(4096)
                if data[6:10] == bytes.fromhex("ffff00fa"):
                    moved_position = struct.unpack_from("<3f", data, 42)
                if data[6:10] == bytes.fromhex("ffff0142"):
                    online = all(friend.bytes in data for friend in friends)
                if moved_position and online:
                    break
            else:
                raise AssertionError("Viewer did not enter with both remote friends online")
            wait_until(lambda: location(session) == str(region), "Root presence was not reported")
            sequence = 3
            if home_service:
                wait_until(lambda: grid_user_request(home_service, "getgriduserinfo")
                           .findtext("result/LastRegionID") == str(region),
                           "Root location was not reported to GridUser")
                packet(sequence, "ffff0041", agent.bytes + session.bytes + bytes(16))
                sequence += 1
                for _ in range(64):
                    data = viewer.recv(4096)
                    if data[6:10] == bytes.fromhex("ffff004a"):
                        assert b"Home set not" in data, data
                        break
                else:
                    raise AssertionError("Viewer did not receive the remote home lookup result")
                def set_home(x, y, expected_alert):
                    nonlocal sequence
                    location = struct.pack("<BI6f", 0, 1, x, y, 60, 0, 1, 0)
                    packet(sequence, "ffff0144", agent.bytes + session.bytes + location)
                    sequence += 1
                    for _ in range(64):
                        reply = viewer.recv(4096)
                        if reply[6:10] != bytes.fromhex("ffff0087") or reply[10:26] != agent.bytes:
                            continue
                        message = reply[28:28 + reply[27]].removesuffix(b"\0")
                        if message == expected_alert and reply[26] == 0:
                            return
                    raise AssertionError(f"Viewer did not receive {expected_alert!r}")

                set_home(512, 128, b"/Set Home request failed.")
                set_home(255.5, 255.5, b"Home position set.")
                home = grid_user_request(home_service, "getgriduserinfo").find("result")
                assert home.findtext("HomeRegionID") == str(region)
                assert home.findtext("HomePosition") == f"<{moved_position[0]:g}, {moved_position[1]:g}, 60>"
                assert home.findtext("HomeLookAt") == "<0, 1, 0>"
            packet(sequence, "ffff00fc", agent.bytes + session.bytes)
            wait_until(lambda: location(session) is None, "Viewer logout retained remote presence")
            if home_service:
                wait_until(lambda: grid_user_request(home_service, "getgriduserinfo")
                           .findtext("result/Online") == "False",
                           "Viewer logout retained GridUser online status")
        seed(shutdown_session, agent, region)
    assert location(shutdown_session) is None, "Region shutdown retained remote presence"
    return agent



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--local-grid-user", action="store_true")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    executable = args.native.resolve()

    with tempfile.TemporaryDirectory(dir=args.work) as directory:
        root = Path(directory)
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            private_port = reservation.getsockname()[1]
        private_url = f"http://127.0.0.1:{private_port}"
        robust = root / "robust.ini"
        robust.write_text(
            "[NativeServer]\nListenAddress=127.0.0.1\n"
            "PresenceDatabase=presence.db\nGridUserDatabase=robust-gridusers.db\n[Network]\nhttp_listener_port=0\n"
            f"[Const]\nPrivatePort={private_port}\n[ServiceList]\n"
            'PresenceServiceConnector="${Const|PrivatePort}/OpenSim.Server.Handlers.dll:PresenceServiceConnector"\n'
            'GridUserServiceConnector="${Const|PrivatePort}/OpenSim.Server.Handlers.dll:GridUserServiceConnector"\n'
            'MuteListConnector="${Const|PrivatePort}/OpenSim.Server.Handlers.dll:MuteListServiceConnector"\n'
            "[PresenceService]\nLocalServiceModule=OpenSim.Services.PresenceService.dll:PresenceService\n"
            "AuthType=BasicHttpAuthentication\nHttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n"
            "[GridUserService]\nLocalServiceModule=OpenSim.Services.UserAccountService.dll:GridUserService\n"
            "AuthType=BasicHttpAuthentication\nHttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n"
            "[MuteListService]\nLocalServiceModule=OpenSim.Services.MuteListService.dll:MuteListService\n"
            "[MuteListStore]\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=mutes.db\n",
            encoding="utf-8")
        database = root / "presence.db"

        def seed(session, user, location):
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute("INSERT INTO Presence VALUES (?,?,?,?)",
                                   (str(session), str(user), str(location), str(uuid.uuid4())))

        def location(session):
            with closing(sqlite3.connect(database)) as connection:
                row = connection.execute("SELECT RegionID FROM Presence WHERE SessionID=?",
                                         (str(session),)).fetchone()
                return row[0] if row else None

        with running_server(executable, robust) as (robust_url, _):
            assert request_status(robust_url + "/presence", b"METHOD=getagent") == 404
            assert request_status(private_url + "/presence", b"METHOD=getagent") == 401
            assert request_status(private_url + "/mutelist", b"METHOD=get") == 200
            exercise_region(executable, root, private_url, seed, location,
                            grid_user_url=None if args.local_grid_user else private_url,
                            local_grid_user=args.local_grid_user)

    provider = "local" if args.local_grid_user else "remote"
    print(f"PASS: remote Robust presence and {provider} GridUser viewer home-setting, root reporting and logout")


if __name__ == "__main__":
    main()
