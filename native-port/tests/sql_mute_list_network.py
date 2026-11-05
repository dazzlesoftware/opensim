"""Exercise mute-list persistence against an isolated SQL database."""
import argparse
import base64
import os
from pathlib import Path
import subprocess
import tempfile
import uuid
import zlib

from sql_offline_im_network import running_server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--provider", required=True, choices=("SQLite", "MySQL", "PGSQL"))
    parser.add_argument("--connection", default=os.environ.get("OPENSIM_MUTE_LIST_TEST_CONNECTION"))
    args = parser.parse_args()
    connection = args.connection
    if connection is None:
        if args.provider != "SQLite":
            parser.error("Set OPENSIM_MUTE_LIST_TEST_CONNECTION to an isolated SQL database")
        connection = "Data Source=mutes.db"
    if "\n" in connection or "\r" in connection:
        parser.error("Connection string must fit on one line")

    with tempfile.TemporaryDirectory() as temporary:
        config = Path(temporary) / "mute-list.ini"
        config.write_text(
            "[NativeServer]\nEnableMuteListEndpoint=true\n"
            "[Network]\nhttp_listener_port=0\n"
            "[MuteListService]\nLocalServiceModule=OpenSim.Services.MuteListService.dll:MuteListService\n"
            f'[MuteListStore]\nStorageProvider=OpenSim.Data.{args.provider}.dll\nConnectionString="{connection}"\n',
            encoding="utf-8",
        )
        command = [str(args.native.resolve()), "--config", str(config)]
        subprocess.run(command + ["--init-db"], check=True, timeout=30)
        agent_id, first_id, second_id = (str(uuid.uuid4()) for _ in range(3))
        first = {"agentid": agent_id, "muteid": first_id, "mutename": "Café & O'Brien"}
        second = {"agentid": agent_id, "muteid": second_id, "mutename": "Second"}

        with running_server(command, "/mutelist") as post:
            assert post(METHOD="update", **first, muteflags=1).findtext("result") == "Success"
            assert post(METHOD="update", **first, muteflags=7).findtext("result") == "Success"
            first_reply = post(METHOD="get", agentid=agent_id, mutecrc=0).findtext("result")
            assert base64.b64decode(first_reply, validate=True).decode() == f"0 {first_id} Café & O'Brien|7\n"
            update = post(METHOD="update", **second, mutetype=2, mutestamp=123).findtext("result")
            assert update == ("Failure" if args.provider == "PGSQL" else "Success")

        with running_server(command, "/mutelist") as post:
            try:
                reply = post(METHOD="get", agentid=agent_id, mutecrc=0).findtext("result")
                data = base64.b64decode(reply, validate=True)
                expected = set()
                if args.provider != "PGSQL":
                    expected.add(f"2 {second_id} Second|0")
                if args.provider != "SQLite":
                    expected.add(f"0 {first_id} Café & O'Brien|7")
                assert sorted(data.decode().splitlines()) == sorted(expected)
                assert post(METHOD="get", agentid=agent_id, mutecrc=zlib.crc32(data)).findtext("result") == "AQ=="
            finally:
                post(METHOD="delete", **first)
                assert post(METHOD="delete", **second).findtext("result") == "Success"
            assert post(METHOD="get", agentid=agent_id, mutecrc=0).findtext("result") == ""
            missing_result = "Success" if args.provider == "PGSQL" else "Failure"
            assert post(METHOD="delete", **second).findtext("result") == missing_result

    print(f"PASS: {args.provider} mute-list writes, restart, CRC cache and deletion")


if __name__ == "__main__":
    main()
