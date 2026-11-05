"""Run native region presence and grid-user flows against managed Robust and test MySQL."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import urllib.error
import uuid

from remote_presence_network import exercise_region, request_status, wait_until


def ports():
    with socket.socket() as public, socket.socket() as private:
        for reservation in (public, private):
            reservation.bind(("127.0.0.1", 0))
        return public.getsockname()[1], private.getsockname()[1]


@contextmanager
def managed_robust(dotnet, source, work, config, url):
    binaries = work / "managed"
    binaries.mkdir()
    for path in source.iterdir():
        if path.is_file() and (".dll" in path.name or path.name in
                               ("Robust.runtimeconfig.json", "Robust.exe.config")):
            shutil.copyfile(path, binaries / path.name)

    log_path = work / "managed-console.log"
    with log_path.open("wb") as log, subprocess.Popen(
            [dotnet, binaries / "Robust.dll", "-inifile", config, "-console", "basic"],
            cwd=binaries, stdin=subprocess.PIPE, stdout=log, stderr=log,
            text=True) as process:
        try:
            def ready():
                if process.poll() is not None:
                    raise RuntimeError("Managed Robust exited before starting presence")
                try:
                    return request_status(url + "/presence", b"METHOD=getagent") == 401
                except urllib.error.URLError:
                    return False

            wait_until(ready, "Managed Robust presence did not start")
            yield
        except Exception as error:
            output = log_path.read_text(encoding="utf-8", errors="replace")
            if password := os.environ.get("MYSQL_PWD"):
                output = output.replace(password, "[redacted]")
            raise RuntimeError(output[-8000:]) from error
        finally:
            if process.poll() is None:
                try:
                    process.stdin.write("quit\n")
                    process.stdin.flush()
                    process.wait(timeout=8)
                except (OSError, subprocess.TimeoutExpired):
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--managed-bin", required=True, type=Path)
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--mysql-client", default="mariadb")
    parser.add_argument("--mysql-port", type=int, required=True)
    parser.add_argument("--mysql-user", default="root")
    parser.add_argument("--database", required=True, help="An isolated MySQL test database")
    args = parser.parse_args()

    password = os.environ.get("MYSQL_PWD", "")
    for value in (args.mysql_user, args.database):
        if not re.fullmatch(r"[A-Za-z0-9_]+", value):
            parser.error("The test database and user must be plain SQL identifiers")
    if any(character in password for character in "\r\n\";"):
        parser.error("Use a fixture password without newlines, semicolons or quotes")

    mysql = [args.mysql_client, "--protocol=tcp", "--host=127.0.0.1",
             f"--port={args.mysql_port}", f"--user={args.mysql_user}", "-N", args.database]
    sessions = []

    def query(statement):
        return subprocess.check_output(mysql + ["-e", statement], text=True, timeout=10).strip()

    def seed(session, user, region):
        sessions.append(session)
        query("INSERT INTO Presence (SessionID,UserID,RegionID,SecureSessionID) "
              f"VALUES ('{session}','{user}','{region}','{uuid.uuid4()}')")

    def location(session):
        return query(f"SELECT RegionID FROM Presence WHERE SessionID='{session}'") or None

    with tempfile.TemporaryDirectory(prefix="opensim-managed-presence-") as temporary:
        work = Path(temporary)
        public, private = ports()
        url = f"http://127.0.0.1:{private}"
        config = work / "robust.ini"
        config.write_text(
            "[Startup]\nConsoleHistoryFileEnabled=false\n"
            f"[Network]\nport={public}\nhttp_listener_port={public}\n"
            f"[ServiceList]\nPresenceServiceConnector={private}/OpenSim.Server.Handlers.dll:PresenceServiceConnector\n"
            f"GridUserServiceConnector={private}/OpenSim.Server.Handlers.dll:GridUserServiceConnector\n"
            "[DatabaseService]\nStorageProvider=OpenSim.Data.MySQL.dll\n"
            f'ConnectionString="Server=127.0.0.1;Port={args.mysql_port};Database={args.database};'
            f'User ID={args.mysql_user};Password={password};SslMode=None;Old Guids=true;"\n'
            "[PresenceService]\nLocalServiceModule=OpenSim.Services.PresenceService.dll:PresenceService\n"
            "AuthType=BasicHttpAuthentication\nHttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n"
            "[GridUserService]\nLocalServiceModule=OpenSim.Services.UserAccountService.dll:GridUserService\n"
            "AuthType=BasicHttpAuthentication\nHttpAuthUsername=presence\nHttpAuthPassword=fixture-password\n",
            encoding="utf-8")
        try:
            with managed_robust(args.dotnet, args.managed_bin.resolve(), work, config, url):
                if query("SELECT COUNT(*) FROM Presence") != "0":
                    raise RuntimeError("Use an isolated test database without existing presence sessions")
                agent = exercise_region(args.native.resolve(), work, url, seed, location, grid_user_url=url)
                query(f"DELETE FROM GridUser WHERE UserID='{agent}'")
        finally:
            if sessions:
                identifiers = ",".join(f"'{session}'" for session in sessions)
                query(f"DELETE FROM Presence WHERE SessionID IN ({identifiers})")
    print("PASS: native region presence, viewer home-setting and grid-user lifecycle with managed Robust")


if __name__ == "__main__":
    main()
