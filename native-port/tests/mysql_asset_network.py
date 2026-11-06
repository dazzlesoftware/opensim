"""Exercise native assets against a disposable MariaDB server."""

import argparse
import base64
import gzip
import hashlib
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
from time import monotonic, sleep
from xml.etree import ElementTree

from asset_endpoint_network import asset_xml, request, server

ROOT = Path(__file__).resolve().parents[2]
ASSET = "12121212-1212-4212-8212-121212121212"
LEGACY = "34343434-3434-4434-8434-343434343434"
EMPTY = "56565656-5656-4656-8656-565656565656"


def check_database(native, probe, work, sql, port, database):
    config = work / "native.ini"
    config.write_text(f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase={database}-accounts.db
AuthenticationDatabase={database}-auth.db
EnableAssetEndpoint=true
[AssetService]
StorageProvider=OpenSim.Data.MySQL.dll:MySQLAssetData
ConnectionString="Server=127.0.0.1;Port={port};Database={database};User ID=root;SslMode=None"
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
""", encoding="utf-8")
    subprocess.run([native, "--config", str(config), "--init-db"], cwd=work, check=True, timeout=30)
    payload = b"\x00\xff\xc3\xa9" + bytes(range(256))
    wire = asset_xml(ASSET, "Maptile Collectable", asset_type=7, data=payload)
    with server(native, config) as http:
        assert request(http, "POST", "/assets/", wire, authorized=False)[0] == 401
        status, _, result = request(http, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(result).text == ASSET
        status, _, result = request(http, "GET", f"/assets/{ASSET}")
        asset = ElementTree.fromstring(result)
        assert status == 200 and asset.findtext("Flags") == "Maptile Collectable"
        assert asset.findtext("Type") == "7" and base64.b64decode(asset.findtext("Data")) == payload
        assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        status, _, result = request(http, "GET", f"/assets/{ASSET}/metadata")
        assert status == 200 and ElementTree.fromstring(result).findtext("ID") == ASSET
        ids = f"<ArrayOfString><string>{LEGACY}</string><string>{ASSET}</string><string>{ASSET}</string></ArrayOfString>"
        status, _, result = request(http, "POST", "/get_assets_exist", ids)
        assert status == 200
        assert [flag.text for flag in ElementTree.fromstring(result)] == [
            "true" if database == "legacy_assets" else "false", "true", "true",
        ]
        empty = asset_xml(EMPTY, "Maptile", data=b"")
        status, _, result = request(http, "POST", "/assets/", empty)
        assert status == 200 and ElementTree.fromstring(result).text == EMPTY
        status, _, result = request(http, "GET", f"/assets/{EMPTY}/data")
        assert status == 200 and result == b""
        assert ElementTree.fromstring(request(http, "DELETE", f"/assets/{EMPTY}")[2]).text == "true"
        if database == "legacy_assets":
            status, _, result = request(http, "GET", f"/assets/{LEGACY}")
            assert status == 200 and ElementTree.fromstring(result).findtext("Name") == "Legacy asset"
    with server(native, config) as http:
        assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        status, _, result = request(http, "DELETE", f"/assets/{ASSET}")
        assert status == 200 and ElementTree.fromstring(result).text == "true"
        assert request(http, "GET", f"/assets/{ASSET}")[0] == 404
        assert request(http, "POST", "/assets/", wire)[0] == 200
    subprocess.run([probe, "--mysql-provider", str(config), ASSET], cwd=work, check=True, timeout=25)
    assert sql(database, "SELECT version FROM migrations WHERE name='AssetStore'") == "10"


def check_filesystem(native, work, sql, port, verify_stats, dotnet):
    config = work / "fs.ini"
    store = work / "asset-files"
    spool = work / "asset-spool"
    settings = f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=fs-accounts.db
AuthenticationDatabase=fs-auth.db
EnableAssetEndpoint=true
[AssetService]
LocalServiceModule=OpenSim.Services.FSAssetService.dll:FSAssetConnector
StorageProvider=OpenSim.Data.MySQL.dll
ConnectionString="Server=127.0.0.1;Port={port};Database=fs_assets;User ID=root;SslMode=None"
BaseDirectory={store}
SpoolDirectory={spool}
FallbackService=OpenSim.Services.AssetService.dll:AssetService
DaysBetweenAccessTimeUpdates=1
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
"""
    config.write_text(settings + "SecondaryInstance=true\n", encoding="utf-8")
    subprocess.run([native, "--config", str(config), "--init-db"], cwd=work, check=True, timeout=30)
    payload = b"\x00\xff" + bytes(range(256))
    digest = hashlib.sha256(payload).hexdigest().upper()
    staged = spool / (digest + ".asset")
    archived = store.joinpath(digest[:2], digest[2:4], digest[4:6], digest[6:10], digest + ".gz")
    with server(native, config, with_console=True) as (http, console):
        wire = asset_xml(ASSET, "Maptile", asset_type=7, data=payload)
        status, _, result = request(http, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(result).text == ASSET
        assert staged.read_bytes() == payload
        assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        assert sql("fs_assets", f"SELECT hash FROM fsassets WHERE id='{ASSET}'") == digest
        sql("fs_assets", f"UPDATE fsassets SET access_time=UNIX_TIMESTAMP()-60 WHERE id='{ASSET}'")
        recent = sql("fs_assets", f"SELECT access_time FROM fsassets WHERE id='{ASSET}'")
        assert request(http, "GET", f"/assets/{ASSET}/metadata")[0] == 200
        assert sql("fs_assets", f"SELECT access_time FROM fsassets WHERE id='{ASSET}'") == recent
        sql("fs_assets", f"UPDATE fsassets SET access_time=1 WHERE id='{ASSET}'")
        assert request(http, "GET", f"/assets/{ASSET}/metadata")[0] == 200
        assert int(sql("fs_assets", f"SELECT access_time FROM fsassets WHERE id='{ASSET}'")) > 1
        duplicate = asset_xml(ASSET, "Normal", data=b"replacement")
        assert request(http, "POST", "/assets/", duplicate)[0] == 200
        assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        sql("fs_assets", f"""
INSERT INTO assets (id,name,description,assetType,local,temporary,data)
VALUES ('{LEGACY}','Fallback asset','Classic storage',7,0,0,X'A501');
""")
        assert request(http, "GET", f"/assets/{LEGACY}/metadata")[0] == 404
        status, _, result = request(http, "GET", f"/assets/{LEGACY}")
        assert status == 200 and base64.b64decode(ElementTree.fromstring(result).findtext("Data")) == b"\xa5\x01"
        assert request(http, "GET", f"/assets/{LEGACY}/metadata")[0] == 200
        fallback_digest = hashlib.sha256(b"\xa5\x01").hexdigest().upper()
        missing_digest = "0" * 64
        sql("fs_assets", f"UPDATE fsassets SET hash='{missing_digest}' WHERE id='{LEGACY}'")
        assert request(http, "GET", f"/assets/{LEGACY}/data")[2] == b""
        output = console(f"show digest {LEGACY}")
        assert f"[FSASSETS]: Added asset {LEGACY} from fallback to local store" in output
        assert fallback_digest in output and "0000: A5-01" in output
        assert sql("fs_assets", f"SELECT hash FROM fsassets WHERE id='{LEGACY}'") == missing_digest
        sql("fs_assets", f"UPDATE fsassets SET hash='{fallback_digest}' WHERE id='{LEGACY}'")
        assert "Total asset count: 2" in console("show assets")
        digest_output = console(f"show digest {ASSET}")
        assert "FS file:" in digest_output and digest in digest_output and "0000: 00-FF" in digest_output
        assert "Syntax: import" in console("import")
        sql("fs_assets", f"""
CREATE TABLE legacy_import LIKE assets;
INSERT INTO legacy_import (id,name,description,assetType,local,temporary,data)
VALUES ('{EMPTY}','Console import','From SQL',7,0,0,X'A509');
""")
        connection = f"Server=127.0.0.1;Port={port};Database=fs_assets;User ID=root;SslMode=None"
        assert "Invalid filesystem import table name" in console(f'import "{connection}" ../legacy_import')
        assert "Import done, 1 assets imported" in console(f'import "{connection}" legacy_import 0 1')
        assert request(http, "GET", f"/assets/{EMPTY}/data")[2] == b"\xa5\x09"
        sql("fs_assets", f"UPDATE legacy_import SET data=X'FF' WHERE id='{EMPTY}'")
        assert "Import done, 1 assets imported" in console(f'force import "{connection}" legacy_import')
        assert request(http, "GET", f"/assets/{EMPTY}/data")[2] == b"\xa5\x09"
        assert "Asset deleted" in console(f"delete asset {EMPTY}")
        assert request(http, "GET", f"/assets/{EMPTY}")[0] == 404
        rows = ",".join(
            f"('f0000000-0000-4000-8000-{index:012d}','Streamed asset','',7,0,0,"
            "REPEAT(X'00FF',32768))"
            for index in range(101)
        )
        sql("fs_assets", "TRUNCATE TABLE legacy_import")
        sql("fs_assets",
            "INSERT INTO legacy_import (id,name,description,assetType,local,temporary,data) VALUES " + rows)
        output = console(f'import "{connection}" legacy_import')
        assert "100 assets imported so far" in output and "Import done, 101 assets imported" in output, output
        assert request(http, "GET", "/assets/f0000000-0000-4000-8000-000000000100/data")[2] == b"\x00\xff" * 32768
    assert staged.exists() and not archived.exists()

    store.mkdir(exist_ok=True)
    blocked_shard = store / digest[:2]
    blocked_shard.write_bytes(b"legacy shard obstruction")
    config.write_text(settings, encoding="utf-8")
    with server(native, config, with_console=True) as (http, console):
        deadline = monotonic() + 10
        while (not archived.exists() or staged.exists()) and monotonic() < deadline:
            sleep(0.1)
        assert gzip.decompress(archived.read_bytes()) == payload
        assert not staged.exists()
        saved = list(store.glob(blocked_shard.name + ".legacy-*"))
        assert len(saved) == 1 and saved[0].read_bytes() == b"legacy shard obstruction"
        assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        if verify_stats:
            missing_file = "92929292-9292-4292-8292-929292929292"
            missing_metadata = "93939393-9393-4393-8393-939393939393"
            sql("fs_assets", f"""
INSERT INTO fsassets (id,type,hash) VALUES ('{missing_file}',7,REPEAT('0',64));
""")
            assert request(http, "GET", f"/assets/{ASSET}")[0] == 200
            assert request(http, "GET", f"/assets/{missing_file}")[0] == 200
            assert request(http, "GET", f"/assets/{missing_metadata}")[0] == 404
            sleep(61)
            output = console("show assets")
            assert "[FSASSETS]: Read stats: 2 files," in output and ", missing 1, FS 1" in output, output
            print("PASS: primary filesystem service reports read statistics after 60 seconds")
        sql("fs_assets", """
CREATE TRIGGER reject_fs_delete BEFORE DELETE ON fsassets FOR EACH ROW
SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'fixture rejects delete';
""")
        try:
            status, _, result = request(http, "DELETE", f"/assets/{ASSET}")
            assert status == 200 and ElementTree.fromstring(result).text == "true"
            assert request(http, "GET", f"/assets/{ASSET}/data")[2] == payload
        finally:
            sql("fs_assets", "DROP TRIGGER reject_fs_delete")
        status, _, result = request(http, "DELETE", f"/assets/{ASSET}")
        assert status == 200 and ElementTree.fromstring(result).text == "true"
        assert request(http, "GET", f"/assets/{ASSET}")[0] == 404
        assert archived.exists()
    assert sql("fs_assets", "SELECT version FROM migrations WHERE name='FSAssetStore'") == "1"
    sql("fs_assets", """
CREATE USER 'fs_fixture'@'127.0.0.1';
GRANT ALL ON fs_assets.* TO 'fs_fixture'@'127.0.0.1';
""")
    config.write_text(settings.replace("User ID=root", "User ID=fs_fixture") + "SecondaryInstance=true\n",
                      encoding="utf-8")
    ids = f"<ArrayOfString><string>{LEGACY}</string><string>{ASSET}</string></ArrayOfString>"

    def check_rejected_insert(http, asset_id, payload):
        wire = asset_xml(asset_id, "Maptile", asset_type=7, data=payload)
        digest = hashlib.sha256(payload).hexdigest().upper()
        sql("fs_assets", """
CREATE TRIGGER reject_fs_insert BEFORE INSERT ON fsassets FOR EACH ROW
SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'fixture rejects insert';
""")
        try:
            status, _, result = request(http, "POST", "/assets/", wire)
            assert status == 200 and ElementTree.fromstring(result).text == asset_id
            assert request(http, "GET", f"/assets/{asset_id}/metadata")[0] == 404
            assert (spool / (digest + ".asset")).read_bytes() == payload
            duplicate = asset_xml(LEGACY, "Maptile", asset_type=7, data=b"ignored duplicate")
            status, _, result = request(http, "POST", "/assets/", duplicate)
            assert status == 200 and ElementTree.fromstring(result).text == LEGACY
            assert request(http, "GET", f"/assets/{LEGACY}/data")[2] == b"\xa5\x01"
        finally:
            sql("fs_assets", "DROP TRIGGER reject_fs_insert")
        status, _, result = request(http, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(result).text == asset_id
        assert request(http, "GET", f"/assets/{asset_id}/data")[2] == payload

    with server(native, config, with_console=True) as (http, console):
        status, _, result = request(http, "POST", "/get_assets_exist", ids)
        assert status == 200 and [flag.text for flag in ElementTree.fromstring(result)] == ["true", "false"]
        sql("fs_assets", "ALTER USER 'fs_fixture'@'127.0.0.1' ACCOUNT LOCK")
        try:
            status, _, result = request(http, "POST", "/get_assets_exist", ids)
            assert status == 200 and [flag.text for flag in ElementTree.fromstring(result)] == ["false", "false"]
            assert "Total asset count: 0" in console("show assets")
        finally:
            sql("fs_assets", "ALTER USER 'fs_fixture'@'127.0.0.1' ACCOUNT UNLOCK")
        status, _, result = request(http, "POST", "/get_assets_exist", ids)
        assert status == 200 and [flag.text for flag in ElementTree.fromstring(result)] == ["true", "false"]
        check_rejected_insert(http, "81818181-8181-4181-8181-818181818181", b"native retry\0\xa5")
    print("PASS: native filesystem service, MariaDB index, fallback import, and spool archive")
    if dotnet:
        from managed_asset_wire_network import check_shared_filesystem, managed_asset_server
        plugins = ("OpenSim.Services.FSAssetService.dll", "OpenSim.Services.AssetService.dll", "OpenSim.Data.MySQL.dll")
        managed_settings = "[AssetService]" + settings.split("[AssetService]", 1)[1]
        with managed_asset_server(dotnet, work, managed_settings + "SecondaryInstance=true\n", plugins) as http:
            check_rejected_insert(http, "82828282-8282-4282-8282-828282828282", b"managed retry\0\xff")
        check_shared_filesystem(native, dotnet, config, settings, plugins, spool)


def run(native, probe, binaries, work, verify_stats, dotnet):
    suffix = ".exe" if os.name == "nt" else ""
    data = work / "database"
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    def tool(name):
        candidate = binaries / (name + suffix)
        if candidate.is_file():
            return str(candidate)
        if os.name != "nt":
            installed = shutil.which(name)
            if installed:
                return installed
        raise FileNotFoundError(candidate)

    def sql(database, statement):
        args = [tool("mariadb"), "--no-defaults", "--protocol=tcp", "--host=127.0.0.1",
                f"--port={port}", "--user=root", "--batch", "--skip-column-names", "--connect-timeout=2"]
        if database:
            args.append(database)
        return subprocess.run([*args, "-e", statement], cwd=work, check=True, capture_output=True,
                              text=True, timeout=10).stdout.strip()

    install = [tool("mariadb-install-db"), f"--datadir={data}"]
    daemon = [tool("mariadbd"), "--no-defaults", f"--datadir={data}",
              "--bind-address=127.0.0.1", f"--port={port}"]
    if os.name != "nt":
        install.insert(1, "--no-defaults")
        install += ["--auth-root-authentication-method=normal", "--skip-test-db"]
        daemon += [f"--socket={work / 'mariadb.sock'}", f"--pid-file={work / 'mariadb.pid'}"]
    subprocess.run(install, cwd=work, check=True, timeout=60)
    log = work / "mariadb.log"
    with log.open("wb") as output, subprocess.Popen(
        daemon,
        cwd=work, stdout=output, stderr=subprocess.STDOUT,
    ) as process:
        try:
            deadline = monotonic() + 25
            while process.poll() is None and monotonic() < deadline:
                try:
                    if sql(None, "SELECT 1") == "1":
                        break
                except subprocess.CalledProcessError:
                    sleep(0.2)
            else:
                raise RuntimeError("MariaDB did not start")
            sql(None, "CREATE DATABASE fresh_assets")
            sql(None, "CREATE DATABASE legacy_assets")
            migrations = ROOT / "OpenSim/Data/MySQL/Resources/AssetStore.migrations"
            schema = migrations.read_text(encoding="utf-8").split(":VERSION 10", 1)[1]
            sql("legacy_assets", schema + f"""
CREATE TABLE migrations (name varchar(100), version int);
INSERT INTO migrations VALUES ('migrations',1),('AssetStore',9);
INSERT INTO assets (id,name,description,assetType,local,temporary,data)
VALUES ('{LEGACY}','Legacy asset','Previously stored',7,1,0,X'A5');
""")
            for database in ("fresh_assets", "legacy_assets"):
                check_database(native, probe, work, sql, port, database)
            sql(None, "CREATE DATABASE fs_assets")
            check_filesystem(native, work, sql, port, verify_stats, dotnet)
        except Exception:
            print(log.read_text(encoding="utf-8", errors="replace"))
            raise
        finally:
            if process.poll() is None:
                try:
                    sql(None, "SHUTDOWN")
                    process.wait(timeout=15)
                except (subprocess.SubprocessError, OSError):
                    process.kill()
                    process.wait()
    print("PASS: native MySQL asset provider on MariaDB, migrations, replacement, pagination, and deletion")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--mariadb-bin", type=Path, required=True)
    parser.add_argument("--verify-stats", action="store_true")
    parser.add_argument("--dotnet")
    args = parser.parse_args()
    # The default macOS temporary path can exceed the Unix socket address limit.
    temp_root = "/tmp" if sys.platform == "darwin" else None
    with tempfile.TemporaryDirectory(prefix="native-mysql-assets-", dir=temp_root) as directory:
        run(str(args.native.resolve()), str(args.probe.resolve()), args.mariadb_bin.resolve(),
            Path(directory), args.verify_stats, args.dotnet)
