"""Exercise the native asset service over HTTP with a real SQLite database."""

import base64
from contextlib import closing, contextmanager
import http.client
from pathlib import Path
from queue import Queue
import socket
import sqlite3
import subprocess
import sys
import tempfile
from threading import Thread
from urllib.parse import urlsplit
from xml.etree import ElementTree


@contextmanager
def server(executable, config, with_console=False):
    with subprocess.Popen(
        [executable, "--config", str(config), "--console"],
        cwd=config.parent,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
    ) as process:
        lines = Queue()

        def read_output():
            for line in process.stdout:
                lines.put(line.strip())
            lines.put(None)

        Thread(target=read_output, daemon=True).start()
        log = []

        def console(command):
            marker = "ERROR: asset-test-marker is not a valid ID format"
            process.stdin.write(f"{command}\ndump asset asset-test-marker\n")
            process.stdin.flush()
            output = []
            while True:
                line = lines.get(timeout=15)
                if line is None:
                    raise RuntimeError("\n".join(log + output))
                if marker in line:
                    return "\n".join(output)
                output.append(line)

        try:
            while True:
                line = lines.get(timeout=25)
                if line is None:
                    raise RuntimeError("\n".join(log))
                if line.startswith("LISTENING "):
                    port = urlsplit(line.split(" ", 1)[1]).port
                    yield (port, console) if with_console else port
                    return
                log.append(line)
        finally:
            if process.poll() is None:
                try:
                    process.stdin.write("quit\n")
                    process.stdin.flush()
                    process.wait(timeout=10)
                except BrokenPipeError:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def request(port, method, path, body=None, authorized=True):
    headers = {"Content-Type": "text/xml"}
    if authorized:
        credentials = base64.b64encode(b"assets:secret").decode("ascii")
        headers["Authorization"] = f"Basic {credentials}"
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=15)
    try:
        connection.request(method, path, body, headers)
        response = connection.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        connection.close()


def check_batch_xml(port, asset_id):
    for body in (
        '<ArrayOfString xmlns="urn:other"/>',
        '<other:ArrayOfString xmlns:other="urn:other"/>',
        '<ArrayOfString><string><nested/></string></ArrayOfString>',
    ):
        assert request(port, "POST", "/get_assets_exist", body, authorized=False)[0] == 400, body
    body = f"""<ArrayOfString xmlns:other="urn:other">
        <string xmlns="urn:other">{asset_id}</string>
        <other:string>{asset_id}</other:string>
        <string>{asset_id[:8]}<![CDATA[{asset_id[8:]}]]></string>
    </ArrayOfString>"""
    status, _, result = request(port, "POST", "/get_assets_exist", body, authorized=False)
    assert status == 200 and [flag.text for flag in ElementTree.fromstring(result)] == ["true"]


def asset_xml(asset_id, flags, asset_type=10, data=b"\x00\x01\xff", creator=None):
    payload = base64.b64encode(data).decode("ascii")
    creator_element = "" if creator is None else f"<CreatorID>{creator}</CreatorID>"
    return (
        f"<AssetBase><Data>{payload}</Data><FullID><Guid>{asset_id}</Guid></FullID>"
        f"<ID>{asset_id}</ID><Name>Wire &amp; SQLite</Name><Type>{asset_type}</Type>"
        f"{creator_element}<Flags>{flags}</Flags></AssetBase>"
    )


def check_upload_xml(port):
    asset_id = "83838383-8383-4383-8383-838383838383"
    wire = asset_xml(asset_id, "Normal")
    for invalid in (
        wire.replace("<AssetBase>", '<AssetBase xmlns="urn:other">'),
        wire.replace("</AssetBase>", "<Local>TRUE</Local></AssetBase>"),
        wire.replace("</AssetBase>", "<Temporary>yes</Temporary></AssetBase>"),
        wire.replace("<Data>AAH/</Data>", "<Data>AA?=</Data>"),
        wire.replace("<Data>AAH/</Data>", "<Data>AAH/<nested/></Data>"),
        wire.replace("<Name>Wire &amp; SQLite</Name>", "<Name>Wire<nested/></Name>"),
        wire.replace("<Type>10</Type>", "<Type>10<nested/></Type>"),
        wire.replace(f"<Guid>{asset_id}</Guid>", "<Guid/>"),
    ):
        assert request(port, "POST", "/assets/", invalid)[0] == 400
    valid = wire.replace("<Type>10</Type>", '<Type xmlns="urn:other">bad</Type><Type>+1<![CDATA[0]]></Type>')
    valid = valid.replace("</AssetBase>", "<Local>0</Local><Temporary>fal<![CDATA[se]]></Temporary></AssetBase>")
    valid = valid.replace("<Flags>Normal</Flags>", "<Flags>Map<![CDATA[tile]]></Flags>")
    valid = valid.replace(f"<Guid>{asset_id}</Guid>", f"<Guid>{asset_id[:8]}<![CDATA[{asset_id[8:]}]]></Guid>")
    valid = valid.replace("<Data>AAH/</Data>", "<Data>AA<!-- split --><![CDATA[H/]]></Data>")
    valid = valid.replace("<Name>Wire &amp; SQLite</Name>",
                          "<Name>Wire <!-- split --><![CDATA[& SQLite]]></Name>"
                          "<Description>First<![CDATA[ & second]]> &amp; third</Description>")
    valid = valid.replace("</AssetBase>",
                          "<Name>Ignored</Name><Type>bad</Type><Data>??</Data></AssetBase>")
    status, _, body = request(port, "POST", "/assets/", valid)
    assert status == 200 and ElementTree.fromstring(body).text == asset_id
    status, _, body = request(port, "GET", f"/assets/{asset_id}")
    asset = ElementTree.fromstring(body)
    assert status == 200 and asset.findtext("Type") == "10"
    assert asset.findtext("Flags") == "Maptile"
    assert asset.findtext("Name") == "Wire & SQLite"
    assert asset.findtext("Description") == "First & second & third"
    assert request(port, "GET", f"/assets/{asset_id}/data")[2] == b"\x00\x01\xff"

    for index, full_id in enumerate(("", "<FullID/>",
                                     '<FullID><Guid xmlns="urn:other">ignored</Guid></FullID>'), 1):
        string_id = f"83838383-8383-4383-8383-{index:012d}"
        request_xml = asset_xml(string_id, "Normal").replace(
            f"<FullID><Guid>{string_id}</Guid></FullID>", full_id,
        )
        status, _, body = request(port, "POST", "/assets/", request_xml)
        assert status == 200 and ElementTree.fromstring(body).text == string_id, full_id
        assert request(port, "GET", f"/assets/{string_id}/data")[2] == b"\x00\x01\xff"

    for index, (prefix, suffix) in enumerate((("{", "}"), ("(", ")"), ("\t", "\n")), 4):
        guid_id = f"83838383-8383-4383-8383-{index:012d}"
        request_xml = asset_xml(guid_id, "Normal").replace(f"<ID>{guid_id}</ID>", "")
        request_xml = request_xml.replace(
            f"<Guid>{guid_id}</Guid>", f"<Guid>{prefix}{guid_id}{suffix}</Guid>",
        )
        status, _, body = request(port, "POST", "/assets/", request_xml)
        assert status == 200 and ElementTree.fromstring(body).text == guid_id
        assert request(port, "GET", f"/assets/{guid_id}/data")[2] == b"\x00\x01\xff"


def check_console(port, console, directory):
    asset_id = "abababab-abab-4bab-8bab-abababababab"
    filename = asset_id.upper()
    data = bytes(range(95))
    wire = asset_xml(asset_id, "Rewritable Collectable", data=data)
    status, _, body = request(port, "POST", "/assets/", wire)
    assert status == 200 and ElementTree.fromstring(body).text == asset_id

    assert "Syntax: show asset <ID>" in console("show asset")
    assert "Syntax: delete asset <ID>" in console("delete asset")
    assert "Usage is dump asset <ID>" in console("dump asset")
    assert "ERROR: invalid is not a valid ID format" in console("dump asset invalid")

    output = console(f"show asset {asset_id}")
    for value in (
        "Name: Wire & SQLite", "Type: LSLText (type number = 10)",
        "Content-type: application/vnd.ll.lsltext", "Size: 95 bytes", "Temporary: no",
        "Flags: Rewritable, Collectable",
    ):
        assert value in output, output
    for offset in range(0, 80, 16):
        row = data[offset:offset + 16].hex("-").upper()
        assert f"{offset:04x}: {row}" in output, output
    assert "0050:" not in output, output

    assert f"Asset dumped to file {filename}" in console(f"dump asset {filename}")
    dump = directory / filename
    assert dump.read_bytes() == data
    sentinel = b"keep the existing dump"
    dump.write_bytes(sentinel)
    assert f"File {filename} already exists." in console(f"dump asset {filename}")
    assert dump.read_bytes() == sentinel

    status, _, body = request(port, "DELETE", f"/assets/{asset_id}")
    assert status == 200 and ElementTree.fromstring(body).text == "false"
    assert f"Deleted asset {asset_id} Wire & SQLite" in console(f"delete asset {asset_id}")
    assert "Asset not found" in console(f"show asset {asset_id}")
    assert f"Could not find asset with ID {asset_id}" in console(f"delete asset {asset_id}")
    assert f"ERROR: No asset found with ID {asset_id}" in console(f"dump asset {asset_id}")

    empty_id = "cdcdcdcd-cdcd-4dcd-8dcd-cdcdcdcdcdcd"
    status, _, body = request(port, "POST", "/assets/", asset_xml(empty_id, "Normal", data=b""))
    assert status == 200 and ElementTree.fromstring(body).text == empty_id
    assert "Asset not found" in console(f"show asset {empty_id}")
    assert f"Could not find asset with ID {empty_id}" in console(f"delete asset {empty_id}")
    assert f"Asset dumped to file {empty_id}" in console(f"dump asset {empty_id}")
    assert (directory / empty_id).read_bytes() == b""
    print("PASS: native asset console inspection, exclusive binary dump, and deletion")


def check_hypergrid(executable, config, settings, private_port):
    hg_settings = settings.replace(
        "[ServiceList]",
        "[ServiceList]\nHGAssetServiceConnector=HGAssetService@0/OpenSim.Server.Handlers.dll:AssetServiceConnector",
    )
    config.write_text(hg_settings + """
[Hypergrid]
HomeURI=http://home.example
[HGAssetService]
LocalServiceModule=OpenSim.Services.HypergridService.dll:HGAssetService
UserAccountsService=OpenSim.Services.UserAccountService.dll:UserAccountService
AuthType=None
DisallowExport= lSlTeXt
DisallowImport=LSLBytecode
AllowRemoteDelete=true
AllowRemoteDeleteAllTypes=true
""", encoding="utf-8")

    creator = "00000000-0000-0000-0000-000000000000"
    with closing(sqlite3.connect(config.parent / "accounts.db")) as db, db:
        db.execute(
            "INSERT INTO UserAccounts (PrincipalID, ScopeID, FirstName, LastName, Created) "
            "VALUES (?, ?, ?, ?, ?)",
            (creator, creator, "Original", "Creator", 1),
        )

    script_id = "40404040-4040-4040-8040-404040404040"
    compiled_id = "50505050-5050-4050-8050-505050505050"
    object_id = "60606060-6060-4060-8060-606060606060"
    with server(executable, config) as public_port:
        assert request(private_port, "GET", f"/assets/{script_id}", authorized=False)[0] == 401
        status, _, body = request(private_port, "POST", "/assets/", asset_xml(script_id, "Normal"))
        assert status == 200 and ElementTree.fromstring(body).text == script_id
        for suffix in ("", "/data"):
            assert request(public_port, "GET", f"/assets/{script_id}{suffix}", authorized=False)[0] == 404
        status, _, body = request(public_port, "GET", f"/assets/{script_id}/metadata", authorized=False)
        assert status == 200 and ElementTree.fromstring(body).findtext("ID") == script_id
        batch = f"<ArrayOfString><string>{script_id}</string></ArrayOfString>"
        status, _, body = request(public_port, "POST", "/get_assets_exist", batch, authorized=False)
        assert status == 200 and ElementTree.fromstring(body).findtext("boolean") == "true"

        compiled = asset_xml(compiled_id, "Normal", asset_type=11)
        status, _, body = request(public_port, "POST", "/assets/", compiled, authorized=False)
        assert status == 200 and not ElementTree.fromstring(body).text
        status, headers, _ = request(private_port, "GET", f"/assets/{compiled_id}")
        assert status == 302 and headers["Location"].endswith(compiled_id)

        source = (
            f'<SceneObjectGroup xmlns:xmlns:x="urn:test"><SceneObjectPart>'
            f"<CreatorID><Guid>{creator}</Guid></CreatorID><Name>Example</Name>"
            f"</SceneObjectPart><SceneObjectPart><CreatorID><UUID>{creator}</UUID></CreatorID>"
            "<CreatorData>http://foreign.example;Foreign Creator</CreatorData>"
            "</SceneObjectPart></SceneObjectGroup>"
        ).encode()
        wire = asset_xml(object_id, "Normal", asset_type=6, data=source, creator=creator)
        status, _, body = request(public_port, "POST", "/assets/", wire, authorized=False)
        assert status == 200 and ElementTree.fromstring(body).text == object_id
        status, _, stored = request(private_port, "GET", f"/assets/{object_id}/data")
        assert status == 200 and stored == source.replace(b"xmlns:xmlns:x", b"xmlns:x") + b"\0"

        status, _, body = request(public_port, "GET", f"/assets/{object_id}", authorized=False)
        assert status == 200, (status, body)
        exported = ElementTree.fromstring(body)
        assert exported.findtext("CreatorID") == f"{creator};http://home.example/Original Creator"
        object_bytes = base64.b64decode(exported.findtext("Data"))
        assert object_bytes.endswith(b"\0")
        parts = ElementTree.fromstring(object_bytes[:-1]).findall("SceneObjectPart")
        assert parts[0].findtext("CreatorID/UUID") == creator
        assert parts[0].findtext("CreatorData") == "http://home.example;Original Creator"
        assert parts[1].findtext("CreatorData") == "http://foreign.example;Foreign Creator"
        assert request(public_port, "GET", f"/assets/{object_id}/data", authorized=False)[2] == object_bytes

        for method, payload in (("DELETE", None), ("POST", wire)):
            status, _, body = request(public_port, method, f"/assets/{object_id}", payload, authorized=False)
            assert status == 200 and ElementTree.fromstring(body).text == "false"
        assert request(private_port, "GET", f"/assets/{object_id}/data")[2] == stored

    settings = config.read_text(encoding="utf-8")
    settings = settings.replace(
        f"AssetServiceConnector={private_port}/OpenSim.Server.Handlers.dll:AssetServiceConnector\n", ""
    ).replace("HGAssetServiceConnector=", "PublicAssets=")
    config.write_text(settings, encoding="utf-8")
    with server(executable, config) as public_port:
        # Discover a section-qualified connector even when it is the sole registration.
        assert request(public_port, "GET", f"/assets/{script_id}", authorized=False)[0] == 404
        assert request(public_port, "GET", f"/assets/{object_id}/data", authorized=False)[2] == object_bytes
    print("PASS: public Hypergrid assets share storage and enforce exchange rules and creator mapping")


def run(executable, region_probe, root):
    config = root / "assets.ini"
    config.write_text(f"""[Network]
http_listener_port=0
[NativeServer]
ListenAddress=127.0.0.1
UserAccountDatabase=accounts.db
AuthenticationDatabase=auth.db
[ServiceList]
AssetServiceConnector=0/OpenSim.Server.Handlers.dll:AssetServiceConnector
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{root / 'assets.db'};version=3;Pooling=false
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
AllowRemoteDelete=true
RedirectURL=http://example.test
""", encoding="utf-8")
    subprocess.run([executable, "--config", str(config), "--init-db"], cwd=root, check=True, timeout=30)
    ordinary = "10101010-1010-4010-8010-101010101010"
    map_tile = "20202020-2020-4020-8020-202020202020"
    missing = "30303030-3030-4030-8030-303030303030"

    with server(executable, config, with_console=True) as (port, console):
        region_config = root / "region.ini"
        region_config.write_text(f"""[Modules]
AssetServices=RegionAssetConnector
[AssetService]
LocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector
AssetServerURI=http://127.0.0.1:{port}
AuthType=BasicHttpAuthentication
HttpAuthUsername=assets
HttpAuthPassword=secret
""", encoding="utf-8")
        result = subprocess.check_output(
            [region_probe, "--grid-store", str(region_config)], cwd=root, text=True, timeout=15
        ).strip()
        assert result == "stored|91919191-9191-4191-8191-919191919191"
        assert request(port, "GET", f"/assets/{ordinary}", authorized=False)[0] == 401
        assert request(port, "POST", "/assets/", "<broken>")[0] == 400
        check_upload_xml(port)
        for asset_id, flags in ((ordinary, "Normal"), (map_tile, "Maptile")):
            status, _, body = request(port, "POST", "/assets/", asset_xml(asset_id, flags))
            assert status == 200 and ElementTree.fromstring(body).text == asset_id
        status, _, body = request(port, "GET", f"/assets/{ordinary}")
        asset = ElementTree.fromstring(body)
        assert status == 200 and asset.findtext("Name") == "Wire & SQLite"
        assert asset.findtext("Data") == "AAH/" and asset.findtext("FullID/Guid") == ordinary
        status, headers, body = request(port, "GET", f"/assets/{ordinary}/data")
        assert status == 200 and headers["Content-Type"] == "application/octet-stream"
        assert body == b"\x00\x01\xff"
        status, headers, body = request(port, "GET", f"/assets/{ordinary}/metadata")
        metadata = ElementTree.fromstring(body)
        assert status == 200 and headers["Content-Type"] == metadata.findtext("ContentType")
        assert metadata.tag == "AssetMetadata" and metadata.findtext("FullID/Guid") == ordinary
        assert metadata.findtext("Type") == "10" and metadata.findtext("CreationDate")
        ids = f"<ArrayOfString><string>{missing}</string><string>{ordinary}</string></ArrayOfString>"
        status, _, body = request(port, "POST", "/get_assets_exist", ids, authorized=False)
        assert status == 200 and [item.text for item in ElementTree.fromstring(body)] == ["false", "true"]
        assert request(port, "POST", "/get_assets_exist", "<wrong/>")[0] == 400
        check_batch_xml(port, ordinary)
        assert request(port, "GET", f"/assets/{ordinary}/wrong")[0] == 400
        status, _, body = request(port, "POST", f"/assets/{ordinary}", asset_xml(ordinary, "Normal"))
        assert status == 200 and ElementTree.fromstring(body).text == "false"
        for asset_id, expected in ((ordinary, "false"), (map_tile, "true")):
            status, _, body = request(port, "DELETE", f"/assets/{asset_id}")
            assert status == 200 and ElementTree.fromstring(body).text == expected
        check_console(port, console, root)

    with server(executable, config) as port:
        assert request(port, "GET", f"/assets/{ordinary}/data")[2] == b"\x00\x01\xff"
        status, headers, _ = request(port, "GET", f"/assets/{map_tile}")
        assert status == 302 and headers["Location"] == f"http://example.test/assets/{map_tile}"
        for suffix in ("/", "/data/", "/metadata/"):
            path = f"/assets/{map_tile}{suffix}"
            status, headers, _ = request(port, "GET", path + "?probe=1")
            assert status == 302 and headers["Location"] == "http://example.test" + path[:-1]

    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        private_port = reservation.getsockname()[1]
    settings = config.read_text(encoding="utf-8")
    settings = settings.replace("AssetServiceConnector=0/", f"AssetServiceConnector={private_port}/")
    config.write_text(settings + "AllowRemoteDeleteAllTypes=true\n", encoding="utf-8")
    with server(executable, config) as public_port:
        assert request(public_port, "GET", f"/assets/{ordinary}/data")[0] == 404
        assert request(private_port, "GET", f"/assets/{ordinary}/data")[2] == b"\x00\x01\xff"
        status, _, body = request(private_port, "DELETE", f"/assets/{ordinary}")
        assert status == 200 and ElementTree.fromstring(body).text == "true"
    print("PASS: native region-to-grid asset HTTP, SQLite persistence, and deletion policy")
    check_hypergrid(executable, config, settings, private_port)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="native-asset-endpoint-") as directory:
        run(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()), Path(directory))
