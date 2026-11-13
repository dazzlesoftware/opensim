"""Exercise the native OfflineIM endpoint against an isolated SQL database."""
import argparse
import concurrent.futures
from contextlib import contextmanager
import os
import queue
import subprocess
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path


@contextmanager
def running_server(command, endpoint="/offlineim"):
    with subprocess.Popen(command + ["--console"], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True, encoding="utf-8") as server:
        try:
            lines = queue.Queue()

            def read_output():
                for line in server.stdout:
                    lines.put(line)
                lines.put(None)

            threading.Thread(target=read_output, daemon=True).start()
            deadline = time.monotonic() + 15
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Native server did not begin listening")
                line = lines.get(timeout=remaining)
                if line is None:
                    raise RuntimeError("Native server exited before listening")
                if line.startswith("LISTENING "):
                    url = line.split(" ", 1)[1].strip().rstrip("/") + endpoint
                    break

            def post(**fields):
                data = urllib.parse.urlencode(fields).encode()
                with urllib.request.urlopen(url, data, timeout=5) as response:
                    return ET.fromstring(response.read())

            yield post
        finally:
            if server.poll() is None:
                try:
                    server.stdin.write('"quit"\n')
                    server.stdin.flush()
                except OSError:
                    # The server can close its pipe before poll observes exit.
                    try:
                        server.stdin.close()
                    except OSError:
                        pass
                try:
                    server.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
                    raise
        assert server.returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--connection", default=os.environ.get("OPENSIM_OFFLINE_IM_TEST_CONNECTION"))
    parser.add_argument("--provider", choices=("PGSQL", "MySQL", "SQLite"), default="PGSQL")
    parser.add_argument("--realm", help="Use an existing table in the isolated test database")
    args = parser.parse_args()
    if args.provider != "SQLite" and not args.connection:
        parser.error("Set --connection or OPENSIM_OFFLINE_IM_TEST_CONNECTION")
    if args.connection and ("\n" in args.connection or "\r" in args.connection):
        raise ValueError("Connection string must fit on one line")

    with tempfile.TemporaryDirectory() as temporary:
        realm = args.realm or f"offline_fixture_{uuid.uuid4().hex}"
        config = Path(temporary) / "offline.ini"
        database = (
            f'[DatabaseService]\nStorageProvider=unused.dll\nConnectionString="{args.connection}"\n'
            if args.provider != "SQLite" else ""
        )
        config.write_text(
            "[NativeServer]\nEnableOfflineIMEndpoint=true\nOfflineIMDatabase=offline-fixture.db\n"
            "[Network]\nhttp_listener_port=0\n"
            f"{database}"
            f"[Messaging]\nStorageProvider=OpenSim.Data.{args.provider}.dll\n"
            f"MaxOfflineIMs=1\nRealm={realm}\n",
            encoding="utf-8",
        )
        command = [str(args.native.resolve()), "--config", str(config)]
        subprocess.run(command + ["--init-db"], check=True, timeout=30)
        recipient = "92929292-9292-4292-8292-929292929292"
        message = {
            "METHOD": "STORE", "ToAgentID": recipient, "Message": "SQL & café",
            "Dialog": "9", "Offline": "0", "SessionID": "41414141-4141-4141-8141-414141414141",
            "BinaryBucket": "00 FF",
        }

        for restart in (False, True):
            with running_server(command) as post, running_server(command) as peer:
                if not restart:
                    post(METHOD="DELETE", UserID=recipient)
                    ready = threading.Barrier(4, timeout=5)

                    def store(index):
                        ready.wait()
                        return (post, peer)[index % 2](**message)

                    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                        replies = list(pool.map(store, range(4)))
                    assert sum(reply.findtext("RESULT") == "True" for reply in replies) == 1
                    assert post(**message).findtext("REASON") == "Number of offline IMs has maxed out"
                else:
                    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                        replies = list(pool.map(lambda i: (post, peer)[i % 2](METHOD="GET", PrincipalID=recipient), range(4)))
                    delivered = [reply for reply in replies if reply.find("RESULT/im-0") is not None]
                    assert len(delivered) == 1
                    assert all(reply.find("RESULT").get("type") == "List" for reply in replies)
                    result = delivered[0]
                    for field in ("Message", "BinaryBucket", "Dialog", "Offline", "SessionID"):
                        assert result.findtext(f"RESULT/im-0/{field}") == message[field], field
                    assert len(post(METHOD="GET", PrincipalID=recipient).find("RESULT")) == 0
                    assert post(**(message | {"Message": " \t "})).findtext("RESULT") == "True"
                    assert peer(METHOD="GET", PrincipalID=recipient).findtext("RESULT/im-0/Message") == " \t "
                    assert post(**message).findtext("RESULT") == "True"
                    assert post(METHOD="DELETE", UserID=recipient).findtext("RESULT") == "True"
    print(f"PASS: {args.provider} OfflineIM limit, restart, retrieval and deletion")


if __name__ == "__main__":
    main()
