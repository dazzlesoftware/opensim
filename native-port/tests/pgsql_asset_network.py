"""Exercise native asset routes and original schema migration on a private PostgreSQL cluster."""

import argparse
import base64
import gzip
import hashlib
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile
from xml.etree import ElementTree

from asset_endpoint_network import asset_xml, request, server

ROOT = Path(__file__).resolve().parents[2]
FIRST_ASSET = "12121212-1212-4212-8212-121212121212"
LEGACY_ASSET = "34343434-3434-4434-8434-343434343434"


def check_database(native, probe, cluster, psql, port, database):
    config = cluster / "native.ini"
    config.write_text(f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase={database}-accounts.db
AuthenticationDatabase={database}-auth.db
EnableAssetEndpoint=true
[AssetService]
StorageProvider=OpenSim.Data.PGSQL.dll:PGSQLAssetData
ConnectionString="Server=127.0.0.1;Port={port};Database={database};User ID=postgres"
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
AllowRemoteDeleteAllTypes=true
""", encoding="utf-8")
    subprocess.run([native, "--config", str(config), "--init-db"], cwd=cluster, check=True, timeout=30)
    if database == "legacy_assets":
        psql(database, "INSERT INTO assets VALUES ('invalid-id','Bad ID','Invalid UUID',7,0,0,decode('00','hex'))")
        failed = subprocess.run([native, "--config", str(config), "--console"], cwd=cluster,
                                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=15)
        assert failed.returncode != 0 and "invalid input syntax for type uuid" in failed.stderr, failed
        assert psql(database, "SELECT version FROM migrations WHERE name='AssetStore'") == "1"
        assert psql(database, "SELECT count(*) FROM assets") == "2"
        psql(database, "DELETE FROM assets WHERE id='invalid-id'")
    payload = b"\x00\xff\xc3\xa9" + bytes(range(256))
    wire = asset_xml(FIRST_ASSET, "Maptile Collectable", asset_type=7, data=payload)
    with server(native, config) as http_port:
        assert request(http_port, "POST", "/assets/", wire, authorized=False)[0] == 401
        status, _, result = request(http_port, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(result).text == FIRST_ASSET
        status, _, result = request(http_port, "GET", f"/assets/{FIRST_ASSET}")
        asset = ElementTree.fromstring(result)
        assert status == 200 and asset.findtext("Flags") == "Maptile Collectable"
        assert asset.findtext("Type") == "7" and base64.b64decode(asset.findtext("Data")) == payload
        assert request(http_port, "GET", f"/assets/{FIRST_ASSET}/data")[2] == payload
        status, _, result = request(http_port, "GET", f"/assets/{FIRST_ASSET}/metadata")
        assert status == 200 and ElementTree.fromstring(result).findtext("ID") == FIRST_ASSET
        status, _, result = request(http_port, "DELETE", f"/assets/{FIRST_ASSET}")
        assert status == 200 and ElementTree.fromstring(result).text == "false"
        ids = (f"<ArrayOfString><string>{LEGACY_ASSET}</string>"
               f"<string>{FIRST_ASSET}</string><string>{FIRST_ASSET}</string></ArrayOfString>")
        status, _, result = request(http_port, "POST", "/get_assets_exist", ids)
        expected = ["true" if database == "legacy_assets" else "false", "true", "true"]
        assert status == 200 and [item.text for item in ElementTree.fromstring(result)] == expected
        if database == "legacy_assets":
            status, _, result = request(http_port, "GET", f"/assets/{LEGACY_ASSET}")
            legacy = ElementTree.fromstring(result)
            assert status == 200 and legacy.findtext("Name") == "Legacy asset"
            assert legacy.findtext("Local") == "true" and legacy.findtext("Temporary") == "false"
            assert legacy.findtext("Flags") == "Normal" and base64.b64decode(legacy.findtext("Data")) == b"\xa5"
    with server(native, config) as http_port:
        assert request(http_port, "GET", f"/assets/{FIRST_ASSET}/data")[2] == payload
    subprocess.run([probe, "--pgsql-provider", str(config), FIRST_ASSET], cwd=cluster, check=True, timeout=25)
    assert psql(database, "SELECT version FROM migrations WHERE name='AssetStore'") == "9"


def check_filesystem(native, work, psql, port, dotnet):
    config = work / "fs.ini"
    base = work / "fs-store"
    spool = work / "fs-spool"
    config.write_text(f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=fs-accounts.db
AuthenticationDatabase=fs-auth.db
EnableAssetEndpoint=true
[AssetService]
LocalServiceModule=OpenSim.Services.FSAssetService.dll:FSAssetConnector
StorageProvider=OpenSim.Data.PGSQL.dll:PGSQLFSAssetData
ConnectionString="Server=127.0.0.1;Port={port};Database=fs_assets;User ID=postgres"
BaseDirectory={base}
SpoolDirectory={spool}
UseOsgridFormat=true
SecondaryInstance=true
DaysBetweenAccessTimeUpdates=0
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
""", encoding="utf-8")
    subprocess.run([native, "--config", str(config), "--init-db"], cwd=work, check=True, timeout=30)
    payload = b"existing\x00\xff"
    digest = hashlib.sha256(payload).hexdigest().upper()
    archive = base / digest[:3] / digest[3:6] / (digest + ".gz")
    archive.parent.mkdir(parents=True)
    archive.write_bytes(gzip.compress(payload))

    def batch_exists(http, ids):
        document = ElementTree.Element("ArrayOfString")
        for asset_id in ids:
            ElementTree.SubElement(document, "string").text = asset_id
        status, _, body = request(http, "POST", "/get_assets_exist", ElementTree.tostring(document))
        return status, [flag.text for flag in ElementTree.fromstring(body)] if status == 200 else []

    def check_id_forms(http):
        assert batch_exists(http, [FIRST_ASSET, FIRST_ASSET.replace("-", ""), " " + FIRST_ASSET + "tail",
                                   "ffffffff-ffff-ffff-ffff-ffffffffffff"]) == (
            200, ["true", "true", "true", "false"],
        )
        # The SDK's SSE4.2 UUID parser reads the low byte of a UTF-16 digit.
        return batch_exists(http, ["\u0131" + FIRST_ASSET[1:]])

    def check_duplicate_writes(http):
        wire = asset_xml(FIRST_ASSET, "Maptile", asset_type=7, data=b"new payload")
        psql("fs_assets", """
            CREATE TABLE fs_update_log (id uuid);
            CREATE FUNCTION audit_fs_update() RETURNS trigger LANGUAGE plpgsql AS $$
            BEGIN
                INSERT INTO fs_update_log VALUES (NEW.id);
                RETURN NEW;
            END;
            $$;
            CREATE TRIGGER audit_fs_update AFTER UPDATE ON fsassets
            FOR EACH ROW EXECUTE FUNCTION audit_fs_update();
        """)
        try:
            status, _, body = request(http, "POST", "/assets/", wire)
            assert status == 200 and ElementTree.fromstring(body).text == "00000000-0000-0000-0000-000000000000"
            assert psql("fs_assets", f"SELECT COUNT(*) FROM fs_update_log WHERE id='{FIRST_ASSET}'") == "2"
            assert request(http, "GET", f"/assets/{FIRST_ASSET}/data")[2] == b"new payload"
        finally:
            psql("fs_assets", """
                DROP TRIGGER audit_fs_update ON fsassets;
                DROP FUNCTION audit_fs_update();
                DROP TABLE fs_update_log;
            """)

    with server(native, config, with_console=True) as (http, console):
        psql("fs_assets", f"""
            INSERT INTO fsassets (id,type,hash,asset_flags,create_time,access_time)
            VALUES ('{LEGACY_ASSET}',7,'{digest}',1,100,1)
        """)
        status, _, body = request(http, "GET", f"/assets/{LEGACY_ASSET}/metadata")
        meta = ElementTree.fromstring(body)
        assert status == 200 and meta.findtext("Name") == "" and meta.findtext("Description") == ""
        assert meta.findtext("CreationDate") == "1970-01-01T00:01:40Z"
        assert request(http, "GET", f"/assets/{LEGACY_ASSET}/data")[2] == payload
        assert int(psql("fs_assets", f"SELECT access_time FROM fsassets WHERE id='{LEGACY_ASSET}'")) > 1
        wire = asset_xml(FIRST_ASSET, "Maptile", asset_type=7, data=b"new payload")
        status, _, body = request(http, "POST", "/assets/", wire)
        assert status == 200 and ElementTree.fromstring(body).text == FIRST_ASSET
        native_unicode_id = check_id_forms(http)
        check_duplicate_writes(http)
        status, _, body = request(http, "DELETE", f"/assets/{LEGACY_ASSET}")
        assert status == 200 and ElementTree.fromstring(body).text == "true" and archive.exists()
        assert request(http, "GET", f"/assets/{LEGACY_ASSET}")[0] == 404
        psql("fs_assets", f"""
            CREATE TABLE legacy_import (id uuid, "assetType" smallint, create_time integer, data bytea);
            INSERT INTO legacy_import VALUES ('{LEGACY_ASSET}',7,100,decode('00ffaa','hex'));
        """)
        connection = f"Server=127.0.0.1;Port={port};Database=fs_assets;User ID=postgres"
        assert "Import done, 0 assets imported" in console(f'import "{connection}" legacy_import 0 1')
        assert "Import done, 1 assets imported" in console(f'import "{connection}" legacy_import')
        assert request(http, "GET", f"/assets/{LEGACY_ASSET}/data")[2] == b"\x00\xff\xaa"
        assert "Asset deleted" in console(f"delete asset {LEGACY_ASSET}")
        psql("fs_assets", """
            TRUNCATE legacy_import;
            INSERT INTO legacy_import
            SELECT ('f0000000-0000-4000-8000-' || lpad(i::text,12,'0'))::uuid,
                   7,100,decode(repeat('00ff',32768),'hex')
            FROM generate_series(0,100) AS i;
        """)
        output = console(f'import "{connection}" legacy_import')
        assert "100 assets imported so far" in output and "Import done, 101 assets imported" in output, output
        assert request(http, "GET", "/assets/f0000000-0000-4000-8000-000000000100/data")[2] == b"\x00\xff" * 32768
        psql("fs_assets", """
            CREATE VIEW interrupted_import AS
            SELECT ('e0000000-0000-4000-8000-' || lpad(i::text,12,'0'))::uuid AS id,
                   7/(2-i) AS "assetType", 100 AS create_time, decode('00ff','hex') AS data
            FROM generate_series(1,2) AS i;
        """)
        output = console(f'import "{connection}" interrupted_import')
        assert "PostgreSQL streaming result failed" in output and "Import done" not in output, output
        assert request(http, "GET", "/assets/e0000000-0000-4000-8000-000000000001/data")[2] == b"\x00\xff"
    assert psql("fs_assets", "SELECT version FROM migrations WHERE name='FSAssetStore'") == "1"
    print("PASS: PostgreSQL filesystem asset index, OSGrid archive reads, and duplicate handling")

    if dotnet:
        from managed_asset_wire_network import check_shared_filesystem, managed_asset_server
        plugins = ("OpenSim.Services.FSAssetService.dll", "OpenSim.Data.PGSQL.dll")
        settings = config.read_text(encoding="utf-8").replace("SecondaryInstance=true\n", "")
        managed_settings = "[AssetService]" + settings.split("[AssetService]", 1)[1]
        with managed_asset_server(dotnet, work, managed_settings + "SecondaryInstance=true\n", plugins) as http:
            assert check_id_forms(http) == native_unicode_id
            check_duplicate_writes(http)
        check_shared_filesystem(native, dotnet, config, settings, plugins, spool)

    settings = config.read_text(encoding="utf-8").replace(
        "EnableAssetEndpoint=true", "EnableAssetEndpoint=false\nEnableHGAssetEndpoint=true",
    )
    config.write_text(settings + """
[HGAssetService]
LocalServiceModule=OpenSim.Services.HypergridService.dll:HGFSAssetService
UserAccountsService=OpenSim.Services.UserAccountService.dll:UserAccountService
HomeURI=http://home.example
AuthType=None
DisallowExport=LSLText
DisallowImport=LSLBytecode
AllowRemoteDelete=true
AllowRemoteDeleteAllTypes=true
""", encoding="utf-8")
    with server(native, config) as http:
        assert request(http, "GET", f"/assets/{FIRST_ASSET}/data", authorized=False)[2] == b"new payload"
        status, _, body = request(http, "DELETE", f"/assets/{FIRST_ASSET}", authorized=False)
        assert status == 200 and ElementTree.fromstring(body).text == "false"
        script = asset_xml(LEGACY_ASSET, "Normal", asset_type=11)
        status, _, body = request(http, "POST", "/assets/", script, authorized=False)
        assert status == 200 and not ElementTree.fromstring(body).text
        source = asset_xml(LEGACY_ASSET, "Normal", asset_type=10)
        status, _, body = request(http, "POST", "/assets/", source, authorized=False)
        assert status == 200 and ElementTree.fromstring(body).text == LEGACY_ASSET
        assert request(http, "GET", f"/assets/{LEGACY_ASSET}", authorized=False)[0] == 404
        assert request(http, "GET", f"/assets/{LEGACY_ASSET}/metadata", authorized=False)[0] == 200
    print("PASS: Hypergrid filesystem service reuses the PostgreSQL index and enforces exchange rules")


def run(native, probe, binaries, work, dotnet):
    suffix = ".exe" if (binaries / "initdb.exe").exists() else ""
    cluster = work / "database"
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    def command(tool, *args, **kwargs):
        return subprocess.run([str(binaries / (tool + suffix)), *args], cwd=work,
                              check=True, timeout=40, **kwargs)

    def psql(database, sql):
        return command("psql", "-X", "-qAt", "-h", "127.0.0.1", "-p", str(port),
                       "-U", "postgres", "-d", database, "-v", "ON_ERROR_STOP=1",
                       "-c", sql, capture_output=True, text=True).stdout.strip()

    command("initdb", "-D", str(cluster), "-A", "trust", "-U", "postgres", "--locale=C", "-E", "UTF8")
    options = f"-h 127.0.0.1 -p {port}"
    if os.name != "nt":
        options += f" -k {shlex.quote(str(work))}"
    command("pg_ctl", "-D", str(cluster), "-l", str(work / "postgres.log"), "-o", options, "-w", "start")
    try:
        psql("postgres", "CREATE DATABASE fresh_assets")
        psql("postgres", "CREATE DATABASE legacy_assets")
        migrations = ROOT / "OpenSim/Data/PGSQL/Resources/AssetStore.migrations"
        first_version = migrations.read_text(encoding="utf-8").split(":VERSION 2", 1)[0].split("\n", 1)[1]
        psql("legacy_assets", first_version + f"""
CREATE TABLE migrations (name varchar(100), version int);
INSERT INTO migrations VALUES ('migrations',1),('AssetStore',1);
INSERT INTO assets VALUES ('{LEGACY_ASSET}','Legacy asset','From version one',7,1,0,decode('a5','hex'));
""")
        for database in ("fresh_assets", "legacy_assets"):
            check_database(native, probe, work, psql, port, database)
        psql("postgres", "CREATE DATABASE fs_assets")
        check_filesystem(native, work, psql, port, dotnet)
    finally:
        command("pg_ctl", "-D", str(cluster), "-m", "fast", "-w", "stop")
    print("PASS: native PostgreSQL assets, fresh and legacy migrations, binary payloads, and restart")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--postgres-bin", type=Path, required=True)
    parser.add_argument("--dotnet")
    args = parser.parse_args()
    # The default macOS temporary path can exceed the Unix socket address limit.
    temp_root = "/tmp" if sys.platform == "darwin" else None
    with tempfile.TemporaryDirectory(prefix="native-pgsql-assets-", dir=temp_root) as directory:
        run(str(args.native.resolve()), str(args.probe.resolve()), args.postgres_bin.resolve(),
            Path(directory), args.dotnet)
