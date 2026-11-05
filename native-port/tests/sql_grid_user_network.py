"""Check grid-user HTTP behavior against an isolated MariaDB or PostgreSQL database."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import uuid

from sql_offline_im_network import running_server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--provider", required=True, choices=("MySQL", "PGSQL"))
    args = parser.parse_args()
    connection = os.environ.get("OPENSIM_GRID_USER_TEST_CONNECTION", "")
    if not connection or any(character in connection for character in "\r\n\""):
        parser.error("Set OPENSIM_GRID_USER_TEST_CONNECTION to an isolated test database")

    with tempfile.TemporaryDirectory() as temporary:
        config = Path(temporary) / "robust.ini"
        config.write_text(
            "[NativeServer]\nListenAddress=127.0.0.1\nEnableGridUserEndpoint=true\n"
            "[Network]\nhttp_listener_port=0\n"
            f'[DatabaseService]\nStorageProvider=OpenSim.Data.{args.provider}.dll\nConnectionString="{connection}"\n'
            "[GridUserService]\nRealm=GridUser\n",
            encoding="utf-8",
        )
        command = [str(args.native.resolve()), "--config", str(config)]
        subprocess.run(command + ["--init-db"], check=True, timeout=20)
        principal = str(uuid.uuid4())
        user = principal + ";http://grid.example/;Café O'Brien"
        home, last, session = (str(uuid.uuid4()) for _ in range(3))

        with running_server(command, "/griduser") as post:
            assert post(METHOD="getgriduserinfo", UserID=principal).findtext("result") == "Failure"
            assert post(METHOD="loggedin", UserID=user).findtext("result/Online") == "True"
            assert post(METHOD="sethome", UserID=user, RegionID=home,
                        Position="<10,20,30>", LookAt="<0,1,0>").findtext("result") == "Success"
            assert post(METHOD="setposition", UserID=user, SessionID=session, RegionID=last,
                        Position="<40,50,60>", LookAt="<1,0,0>").findtext("result") == "Success"
            assert post(METHOD="loggedout", UserID=user, SessionID=session, RegionID=last,
                        Position="<70,80,90>", LookAt="<0,0,1>").findtext("result") == "Success"

        with running_server(command, "/griduser") as post:
            record = post(METHOD="getgriduserinfo", UserID=principal).find("result")
            assert record.findtext("UserID") == user
            assert record.findtext("HomeRegionID") == home and record.findtext("HomePosition") == "<10, 20, 30>"
            assert record.findtext("LastRegionID") == last and record.findtext("LastPosition") == "<70, 80, 90>"
            assert record.findtext("Online") == "False"
            assert record.findtext("Login") not in (None, "01/01/1970 00:00:00")
            assert record.findtext("Logout") not in (None, "01/01/1970 00:00:00")
            batch = post(METHOD="getgriduserinfos", **{"AgentIDs[]": principal})
            assert batch.findtext("griduser0/UserID") == user

    print(f"PASS: {args.provider} grid-user SQL login, home, position, logout and restart")


if __name__ == "__main__":
    main()
