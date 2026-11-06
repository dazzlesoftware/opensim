"""Build the original C# connectors and exercise them against native Robust."""

import argparse
import base64
from contextlib import contextmanager
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import tempfile
from threading import Event, Lock, Thread
from time import monotonic, sleep
from xml.etree import ElementTree

from asset_endpoint_network import asset_xml, check_batch_xml, check_upload_xml, request, server

ROOT = Path(__file__).resolve().parents[2]
ASSEMBLIES = (
    "Nini", "log4net", "OpenMetaverseTypes", "OpenMetaverse",
    "OpenMetaverse.StructuredData", "OpenSim.Framework", "OpenSim.Framework.Monitoring",
    "OpenSim.Services.Interfaces",
)


def build_client(dotnet, work):
    project = ElementTree.Element("Project", Sdk="Microsoft.NET.Sdk")
    settings = ElementTree.SubElement(project, "PropertyGroup")
    ElementTree.SubElement(settings, "TargetFramework").text = "net8.0"
    ElementTree.SubElement(settings, "OutputType").text = "Exe"
    items = ElementTree.SubElement(project, "ItemGroup")
    for source in (
        "OpenSim/Services/Connectors/BaseServiceConnector.cs",
        "OpenSim/Services/Connectors/Asset/AssetServicesConnector.cs",
        "OpenSim/Services/Connectors/Asset/HGAssetServiceConnector.cs",
        "native-port/tests/managed_asset_wire_reference.cs",
    ):
        ElementTree.SubElement(items, "Compile", Include=str(ROOT / source))
    for name in ASSEMBLIES:
        reference = ElementTree.SubElement(items, "Reference", Include=name)
        ElementTree.SubElement(reference, "HintPath").text = str(ROOT / "bin" / f"{name}.dll")
    project_path = work / "ManagedAssetWire.csproj"
    ElementTree.ElementTree(project).write(project_path, encoding="utf-8")
    subprocess.run([dotnet, "build", str(project_path), "--nologo"], check=True, timeout=100)
    return work / "bin" / "Debug" / "net8.0" / "ManagedAssetWire.dll"


def check_managed_wire_replies(native_probe, dotnet, client, work):
    asset_id = "70707070-7070-4070-8070-707070707070"
    missing = "81818181-8181-4181-8181-818181818181"
    remote_prefix = "84848484-8484-4484-8484-84848484848"
    queue_fixture = None

    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, body):
            self.send_response(status)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if queue_fixture and self.path == f"/assets/{asset_id}":
                busy, release, _, _ = queue_fixture
                if not busy.wait(5):
                    self.reply(503, b"")
                    return
                self.reply(200, asset_xml(asset_id, "Normal").encode())
                release.set()
                return
            if queue_fixture and self.path.startswith(f"/assets/{remote_prefix}"):
                if self.headers.get("Authorization"):
                    self.reply(404, b"")
                    return
                busy, release, lock, requests = queue_fixture
                with lock:
                    requests.append(self.path)
                    if len(requests) == 2:
                        busy.set()
                if not release.wait(6):
                    self.reply(503, b"")
                    return
                self.reply(200, asset_xml(self.path.rsplit("/", 1)[1], "Normal").encode())
                return
            if self.path == f"/assets/{asset_id}":
                if self.server.foreign_reads is not None:
                    auth = self.headers.get("Authorization")
                    self.server.foreign_reads.append(auth)
                    if auth:
                        self.reply(404, b"")
                    else:
                        self.reply(200, asset_xml(asset_id, "Normal").encode())
                    return
                self.server.async_requests += 1
                if self.server.async_requests == 1:
                    self.reply(503, b"")
                else:
                    self.reply(200, asset_xml(asset_id, "Normal").encode())
                return
            if self.path != f"/assets/{asset_id}/metadata":
                self.send_error(404)
                return
            self.reply(200, self.server.metadata)

        def do_POST(self):
            if self.path == f"/assets/{asset_id}":
                self.rfile.read(int(self.headers["Content-Length"]))
                self.reply(200, self.server.mutation_reply)
                return
            if self.path == "/assets/":
                payload = self.rfile.read(int(self.headers["Content-Length"]))
                self.server.last_store = ElementTree.fromstring(payload).findtext("ID")
                self.server.last_store_auth = self.headers.get("Authorization")
                self.reply(200, self.server.store_reply)
                return
            if self.path != "/get_assets_exist":
                self.send_error(404)
                return
            body = self.rfile.read(int(self.headers["Content-Length"]))
            ids = [node.text for node in ElementTree.fromstring(body)]
            if self.server.foreign_batches is not None:
                self.server.foreign_batches.append((ids, self.headers.get("Authorization")))
                flags = "".join(f"<boolean>{str(value == asset_id).lower()}</boolean>" for value in ids)
                self.reply(200, f"<ArrayOfBoolean>{flags}</ArrayOfBoolean>".encode())
                return
            assert ids == [asset_id, missing], ids
            self.server.last_authorization = self.headers.get("Authorization")
            self.reply(*self.server.exists_reply)

        def do_DELETE(self):
            if self.path != f"/assets/{asset_id}":
                self.send_error(404)
                return
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            self.reply(200, self.server.mutation_reply)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as endpoint:
        thread = Thread(target=endpoint.serve_forever, daemon=True)
        thread.start()
        try:
            config = work / "region.ini"
            config.write_text(f"""[Modules]
AssetServices=RegionAssetConnector
[AssetService]
LocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector
AssetServerURI=http://127.0.0.1:{endpoint.server_port}
""", encoding="utf-8")
            endpoint.async_requests = 0
            endpoint.foreign_batches = None
            endpoint.foreign_reads = None
            subprocess.run(
                [native_probe, "--grid-async-retry", str(config), asset_id], check=True, timeout=15,
            )
            assert endpoint.async_requests == 2
            for kind in ("Utc", "Unspecified", "Local"):
                metadata = subprocess.check_output(
                    [dotnet, str(client), "--metadata-xml", kind], timeout=15
                )
                expected, endpoint.metadata = metadata.split(b"\n", 1)
                native_reply = subprocess.check_output(
                    [native_probe, "--grid-metadata", str(config), asset_id], timeout=15
                )
                received, native_xml = native_reply.split(b"\n", 1)
                assert received.strip() == expected.strip(), (kind, received, expected)
                read_back = subprocess.check_output(
                    [dotnet, str(client), "--read-metadata"], input=native_xml, timeout=15
                )
                assert read_back.strip() == expected.strip(), (kind, read_back, expected)
            cases = (
                (200, b"<ArrayOfBoolean><boolean>1</boolean></ArrayOfBoolean>", b"1"),
                (200, b"<ArrayOfBoolean/>", b""),
                (200, b'<ArrayOfBoolean xmlns="urn:other"><boolean>true</boolean></ArrayOfBoolean>', b"00"),
                (200, b'<ArrayOfBoolean><boolean xmlns="urn:other">false</boolean>'
                      b'<boolean>tr<![CDATA[ue]]></boolean></ArrayOfBoolean>', b"1"),
                (200, b"<ArrayOfBoolean><boolean>true<nested/></boolean></ArrayOfBoolean>", b"00"),
                (200, b"<ArrayOfBoolean><boolean>true</boolean><boolean>invalid</boolean></ArrayOfBoolean>", b"00"),
                (200, b'<ArrayOfBoolean xmlns:i="http://www.w3.org/2001/XMLSchema-instance" i:nil="true"/>', b"00"),
                (503, b"", b"00"),
            )
            for status, body, expected in cases:
                endpoint.exists_reply = status, body
                managed = subprocess.check_output(
                    [dotnet, str(client), "--exists", f"http://127.0.0.1:{endpoint.server_port}", asset_id, missing],
                    timeout=15,
                ).strip()
                native = subprocess.check_output(
                    [native_probe, "--grid-exists", str(config), asset_id, missing], timeout=15
                ).strip()
                assert native == managed == expected, (status, body, native, managed)
            stored = b"91919191-9191-4191-8191-919191919191"
            for body, expected in (
                (b"<string>" + stored[:8] + b"<![CDATA[" + stored[8:] + b"]]></string>", stored),
                (b'<string xmlns="urn:other">' + stored + b"</string>", b""),
                (b"<string>" + stored + b"<nested/></string>", b""),
                (b'<string xmlns:i="http://www.w3.org/2001/XMLSchema-instance" i:nil="true"/>', b""),
            ):
                endpoint.store_reply = body
                managed = subprocess.check_output(
                    [dotnet, str(client), "--store-reply", f"http://127.0.0.1:{endpoint.server_port}"],
                    timeout=15,
                ).strip()
                native = subprocess.check_output(
                    [native_probe, "--grid-store-reply", str(config)], timeout=15,
                ).strip()
                assert native == managed == expected, (body, native, managed)
            endpoint.exists_reply = 200, b"<ArrayOfBoolean><boolean>true</boolean></ArrayOfBoolean>"
            foreign_config = work / "asset-foreign.ini"
            foreign_settings = config.read_text(encoding="utf-8") + (
                "HypergridAssetService=OpenSim.Services.Connectors.dll:HGAssetServiceConnector\n"
                "AuthType=BasicHttpAuthentication\nHttpAuthUsername=local\nHttpAuthPassword=local\n"
            )
            foreign_id = f"http://127.0.0.1:{endpoint.server_port}/{stored.decode()}"
            foreign_command = [native_probe, "--foreign-store", str(foreign_config), foreign_id]
            endpoint.store_reply = b"<string>" + stored + b"</string>"
            endpoint.last_store = None
            foreign_config.write_text(foreign_settings, encoding="utf-8")
            assert subprocess.check_output(foreign_command, timeout=15).strip() == stored
            assert endpoint.last_store == stored.decode() and endpoint.last_store_auth is None
            endpoint.last_store = None
            foreign_config.write_text(foreign_settings + "[HGAssetService]\nDisallowExport=LSLText\n",
                                      encoding="utf-8")
            assert subprocess.check_output(foreign_command, timeout=15).strip() == b""
            assert endpoint.last_store is None
            read_id = f"http://127.0.0.1:{endpoint.server_port}/{asset_id}"
            local_auth = "Basic " + base64.b64encode(b"local:local").decode()
            endpoint.store_reply = f"<string>{asset_id}</string>".encode()
            for mode in ("--foreign-read", "--foreign-read-async"):
                read_command = [native_probe, mode, str(foreign_config), read_id]
                endpoint.foreign_reads = []
                foreign_config.write_text(foreign_settings, encoding="utf-8")
                assert subprocess.check_output(read_command, timeout=15).strip() == asset_id.encode()
                assert endpoint.foreign_reads == [local_auth, None]
                assert endpoint.last_store == asset_id and endpoint.last_store_auth == local_auth
                endpoint.foreign_reads = []
                endpoint.last_store = None
                foreign_config.write_text(foreign_settings + "[HGAssetService]\nDisallowImport=LSLText\n",
                                          encoding="utf-8")
                assert subprocess.check_output(read_command, timeout=15).strip() == b""
                assert endpoint.foreign_reads == [local_auth, None] and endpoint.last_store is None
            endpoint.foreign_reads = None
            foreign_ids = [
                f"http://127.0.0.1:{endpoint.server_port}/{asset_id}", missing,
                f"http://localhost:{endpoint.server_port}/{missing}",
                f"http://127.0.0.1:{endpoint.server_port}/{asset_id}", "http://127.0.0.1/invalid",
            ]
            for command in (
                [native_probe, "--foreign-exists", str(foreign_config)],
                [dotnet, str(client), "--foreign-exists", str(foreign_config)],
            ):
                endpoint.foreign_batches = []
                assert subprocess.check_output([*command, *foreign_ids], timeout=15).strip() == b"10010"
                assert endpoint.foreign_batches == [([asset_id, asset_id], None), ([missing], None)]
            endpoint.foreign_batches = None
            foreign_config.write_text(foreign_settings, encoding="utf-8")
            queue_fixture = Event(), Event(), Lock(), []
            try:
                subprocess.run([native_probe, "--asset-read-queues", str(foreign_config),
                                f"http://127.0.0.1:{endpoint.server_port}", asset_id, remote_prefix],
                               check=True, timeout=18)
                assert len(queue_fixture[3]) == 3
            finally:
                queue_fixture[1].set()
                queue_fixture = None
            auth_config = work / "asset-auth.ini"
            for overrides, credentials in (
                ("", "network:secret"),
                ("HttpAuthUsername=gríd\nHttpAuthPassword=\n", "gríd:"),
                ("AuthType=None\n", None),
            ):
                auth_config.write_text(f"""[Network]
asset_server_url=http://127.0.0.1:{endpoint.server_port}
AuthType=BasicHttpAuthentication
HttpAuthUsername=network
HttpAuthPassword=secret
[AssetService]
{overrides}""", encoding="utf-8")
                expected = ("Basic " + base64.b64encode(credentials.encode()).decode()
                            if credentials is not None else None)
                for command in (
                    [dotnet, str(client), "--exists-config", str(auth_config), asset_id, missing],
                    [native_probe, "--grid-exists", str(auth_config), asset_id, missing],
                ):
                    assert subprocess.check_output(command, timeout=15).strip() == b"1"
                    assert endpoint.last_authorization == expected
            for body, expected in (
                (b"<boolean>tr<![CDATA[ue]]></boolean>", b"11"),
                (b'<boolean xmlns="urn:other">true</boolean>', b"00"),
                (b"<boolean>true<nested/></boolean>", b"00"),
            ):
                endpoint.mutation_reply = body
                managed = subprocess.check_output(
                    [dotnet, str(client), "--mutation-reply", f"http://127.0.0.1:{endpoint.server_port}", asset_id],
                    timeout=15,
                ).strip()
                native = subprocess.check_output(
                    [native_probe, "--grid-mutation-reply", str(config), asset_id], timeout=15,
                ).strip()
                assert native == managed == expected, (body, native, managed)
        finally:
            endpoint.shutdown()
            thread.join()
    print("PASS: native connector preserves managed metadata dates, MIME type, and SHA1")
    print("PASS: native and managed connectors agree on unusual batch existence replies")


@contextmanager
def managed_asset_server(dotnet, work, settings, plugins):
    binaries = ROOT / "bin"
    for name in ("OpenSim.Server.Handlers.dll", *plugins):
        shutil.copy2(binaries / name, work / name)
    env = os.environ.copy()
    if "OpenSim.Data.SQLite.dll" in plugins:
        arm = platform.machine().lower() in ("arm64", "aarch64")
        source, target, search_path = {
            "Windows": ("e_sqlite3.dll", "e_sqlite3.dll", "PATH"),
            "Linux": ("libe_sqlite3-arm64.so" if arm else "libe_sqlite3.so",
                      "libe_sqlite3.so", "LD_LIBRARY_PATH"),
            "Darwin": ("libe_sqlite3_OSX_arm64.dylib" if arm else "libe_sqlite3_OSX_x64.dylib",
                       "libe_sqlite3.dylib", "DYLD_LIBRARY_PATH"),
        }[platform.system()]
        native_libraries = work / "sqlite-native"
        native_libraries.mkdir(exist_ok=True)
        shutil.copy2(binaries / "lib64" / source, native_libraries / target)
        previous = env.get(search_path)
        env[search_path] = os.pathsep.join((str(native_libraries), previous)) if previous else str(native_libraries)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    host_config = work / "Robust.ini"
    host_config.write_text(f"""[Startup]
console=basic
ConsoleHistoryFileEnabled=false
[Network]
port={port}
[ServiceList]
AssetServiceConnector=OpenSim.Server.Handlers.dll:AssetServiceConnector
{settings}
""", encoding="utf-8")
    log_config = work / "log4net.xml"
    log_config.write_text("""<log4net>
  <appender name="Fixture" type="log4net.Appender.ConsoleAppender">
    <layout type="log4net.Layout.PatternLayout"><conversionPattern value="%level %message%newline"/></layout>
  </appender>
  <root><level value="DEBUG"/><appender-ref ref="Fixture"/></root>
</log4net>
""", encoding="utf-8")
    log = work / "robust-stdout.log"
    with log.open("wb") as output, subprocess.Popen(
        [dotnet, str(binaries / "Robust.dll"), "-inifile", str(host_config),
         "-logconfig", str(log_config)],
        cwd=work, env=env, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT,
    ) as process:
        try:
            deadline = monotonic() + 25
            while process.poll() is None and monotonic() < deadline:
                try:
                    status, _, body = request(port, "POST", "/get_assets_exist", "<ArrayOfString/>")
                    if status == 200 and b"<ArrayOfBoolean" in body:
                        break
                except OSError:
                    pass
                sleep(0.1)
            else:
                raise RuntimeError("Managed Robust did not register the asset service")
            yield port
        except Exception:
            print(log.read_text(encoding="utf-8", errors="replace"))
            raise
        finally:
            if process.poll() is None:
                try:
                    process.communicate(b"quit\n", timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def check_managed_robust(native_probe, dotnet, work):
    settings = f"""[AssetService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString="URI=file:{work / 'managed-assets.db'};version=3;Pooling=false"
AllowRemoteDelete=true
AllowRemoteDeleteAllTypes=true
"""
    plugins = ("OpenSim.Services.AssetService.dll", "OpenSim.Data.SQLite.dll")
    with managed_asset_server(dotnet, work, settings, plugins) as port:
        client_config = work / "region.ini"
        client_config.write_text(f"""[Modules]
AssetServices=RegionAssetConnector
[AssetService]
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
LocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector
AssetServerURI=http://127.0.0.1:{port}
""", encoding="utf-8")
        stored = subprocess.check_output(
            [native_probe, "--grid-store", str(client_config)], cwd=work, text=True, timeout=15
        ).strip()
        assert stored == "stored|91919191-9191-4191-8191-919191919191", stored
        check_batch_xml(port, "91919191-9191-4191-8191-919191919191")
        check_upload_xml(port)
        subprocess.run([native_probe, "--managed-grid", str(client_config)], cwd=work, check=True, timeout=25)
    redirect = "http://example.test"
    with managed_asset_server(dotnet, work, settings + f"RedirectURL={redirect}\n", plugins) as port:
        missing = "99999999-9999-4999-8999-999999999999"
        for suffix in ("/", "/data/", "/metadata/"):
            path = f"/assets/{missing}{suffix}"
            status, headers, _ = request(port, "GET", path + "?probe=1")
            assert status == 302 and headers["Location"] == redirect + path[:-1]


def check_shared_filesystem(native, dotnet, config, settings, plugins, spool):
    native_id = "66666666-6666-4666-8666-666666666666"
    managed_id = "77777777-7777-4777-8777-777777777777"
    native_payload = b"native FS asset\0\xff"
    managed_payload = b"managed FS asset\0\xa5"
    config.write_text(settings, encoding="utf-8")

    def write(port, asset_id, payload):
        wire = asset_xml(asset_id, "Maptile", asset_type=7, data=payload)
        status, _, body = request(port, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(body).text == asset_id, (status, body)

    with server(native, config) as port:
        write(port, native_id, native_payload)
        digest = hashlib.sha256(native_payload).hexdigest().upper()
        staged = spool / (digest + ".asset")
        deadline = monotonic() + 10
        while staged.exists() and monotonic() < deadline:
            sleep(0.1)
        assert not staged.exists(), "Native filesystem writer did not archive its asset"

    managed_work = config.parent / "managed-fs"
    managed_work.mkdir()
    asset_settings = "[AssetService]" + settings.split("[AssetService]", 1)[1]
    asset_settings += "\nShowConsoleStats=false\n"
    with managed_asset_server(dotnet, managed_work, asset_settings + "SecondaryInstance=true\n", plugins) as port:
        status, _, body = request(port, "GET", f"/assets/{native_id}/data")
        assert status == 200 and body == native_payload, (status, body[:160])
        write(port, managed_id, managed_payload)
        digest = hashlib.sha256(managed_payload).hexdigest().upper()
        managed_staged = spool / (digest + ".asset")
        assert managed_staged.read_bytes() == managed_payload
    config.write_text(settings + "\nSecondaryInstance=true\n", encoding="utf-8")
    with server(native, config) as port:
        status, _, body = request(port, "GET", f"/assets/{managed_id}/metadata")
        assert status == 200 and ElementTree.fromstring(body).findtext("ID") == managed_id
        assert request(port, "GET", f"/assets/{managed_id}/data")[2] == managed_payload
    with managed_asset_server(dotnet, managed_work, asset_settings, plugins):
        deadline = monotonic() + 10
        while managed_staged.exists() and monotonic() < deadline:
            sleep(0.1)
        assert not managed_staged.exists(), "Managed filesystem writer did not archive its asset"
    with server(native, config) as port:
        assert request(port, "GET", f"/assets/{managed_id}/data")[2] == managed_payload
    print("PASS: native and original Robust share the filesystem metadata index, spool, and archives")


def run(native, native_probe, dotnet, work):
    client = build_client(dotnet, work)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        private_port = reservation.getsockname()[1]
    config = work / "assets.ini"
    config.write_text(f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=accounts.db
AuthenticationDatabase=auth.db
[ServiceList]
AssetServiceConnector={private_port}/OpenSim.Server.Handlers.dll:AssetServiceConnector
HGAssetServiceConnector=HGAssetService@0/OpenSim.Server.Handlers.dll:AssetServiceConnector
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{work / 'assets.db'};version=3;Pooling=false
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
[HGAssetService]
LocalServiceModule=OpenSim.Services.HypergridService.dll:HGAssetService
UserAccountsService=OpenSim.Services.UserAccountService.dll:UserAccountService
HomeURI=http://home.example
AuthType=None
""", encoding="utf-8")
    subprocess.run([native, "--config", str(config), "--init-db"], cwd=work, check=True, timeout=30)
    with server(native, config) as public_port:
        subprocess.run(
            [dotnet, str(client), f"http://127.0.0.1:{private_port}", f"http://127.0.0.1:{public_port}"],
            cwd=work, check=True, timeout=45,
        )
    check_managed_wire_replies(native_probe, dotnet, client, work)
    robust_work = work / "managed-robust"
    robust_work.mkdir()
    check_managed_robust(native_probe, dotnet, robust_work)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--native-probe", type=Path, required=True)
    parser.add_argument("--dotnet", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="managed-asset-wire-") as directory:
        run(str(args.native.resolve()), str(args.native_probe.resolve()), args.dotnet, Path(directory))
