"""Exercise the configured native region asset connector over HTTP."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
from xml.etree import ElementTree
import subprocess
import sys
import tempfile

asset = '91919191-9191-4191-8191-919191919191'
wire = (f'<AssetBase><Data>AAH/</Data><FullID><Guid>{asset}</Guid></FullID>'
        f'<ID>{asset}</ID><Name>Foreign &amp; local</Name><Type>0</Type>'
        '<Flags>Maptile Collectable</Flags></AssetBase>').encode()
calls = []
grid_calls = []
stored_id = "92929292-9292-4292-8292-929292929292"
write_reply = [200, stored_id]
mutation_id = "93939393-9393-4393-8393-939393939393"
mutation_path = f"/assets/{mutation_id}"
mutation_calls = []


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, body):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def grid_auth(self):
        if self.headers.get("Authorization") == "Basic Z3JpZDphc3NldA==":
            return True
        self.send_error(401)
        return False

    def do_POST(self):
        if self.path == mutation_path:
            if not self.grid_auth():
                return
            value = ElementTree.fromstring(self.rfile.read(int(self.headers["Content-Length"])))
            assert value.tag == "AssetBase"
            assert value.findtext("ID") == mutation_id and value.findtext("Data") == "BwcH"
            accepted = ("POST", mutation_path) in mutation_calls
            mutation_calls.append(("POST", self.path))
            self.reply(200, b"<boolean>true</boolean>" if accepted else b"<boolean>false</boolean>")
            return
        if self.path != "/assets/":
            self.send_error(404)
            return
        if not self.grid_auth():
            return
        body = self.rfile.read(int(self.headers["Content-Length"]))
        value = ElementTree.fromstring(body)
        assert value.tag == "AssetBase"
        assert value.findtext("ID") == asset and value.findtext("FullID/Guid") == asset
        assert value.findtext("Name") == "Grid write & read"
        assert value.findtext("Type") == "10" and value.findtext("Data") == "AAH/"
        grid_calls.append(("POST", self.path))
        status, asset_id = write_reply
        reply = f"<string>{asset_id}</string>".encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def do_GET(self):
        if self.path in (mutation_path, mutation_path + "/metadata"):
            if not self.grid_auth():
                return
            mutation_calls.append(("GET", self.path))
            if ("DELETE", mutation_path) in mutation_calls:
                self.send_error(404)
                return
            metadata = f"<FullID><Guid>{mutation_id}</Guid></FullID><ID>{mutation_id}</ID><Type>10</Type>"
            if self.path.endswith("/metadata"):
                body = f"<AssetMetadata>{metadata}</AssetMetadata>"
            else:
                updated = mutation_calls.count(("POST", mutation_path)) == 2
                data = "BwcH" if updated else "AAH/"
                body = f"<AssetBase><Data>{data}</Data>{metadata}</AssetBase>"
            self.reply(200, body.encode())
            return
        if self.path == f"/assets/{stored_id}":
            if not self.grid_auth():
                return
            grid_calls.append(("GET", self.path))
            reply = (
                f"<AssetBase><Data>AAH/</Data><FullID><Guid>{stored_id}</Guid></FullID>"
                f"<ID>{stored_id}</ID><Name>Grid write &amp; read</Name><Type>10</Type></AssetBase>"
            ).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)
            return
        calls.append(self.path)
        if self.path.startswith('/missing/'):
            self.send_error(404)
            return
        if self.path.startswith('/invalid/'):
            body = b'<NotAnAsset/>'
        elif self.path.startswith('/nil/'):
            body = b'<AssetBase xmlns:i="http://www.w3.org/2001/XMLSchema-instance" i:nil="true"/>'
        else:
            body = wire
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_DELETE(self):
        if self.path != mutation_path:
            self.send_error(404)
        elif self.grid_auth():
            mutation_calls.append(("DELETE", self.path))
            self.reply(200, b"<boolean>true</boolean>")


with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server, tempfile.TemporaryDirectory() as root:
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        allowed = asset + '|Foreign & local|0|5|0001ff'
        for deny, expected, extra in [('', allowed, []), ('texture', 'missing', []), ('', allowed, ['--store'])]:
            config = Path(root) / 'server.ini'
            config.write_text(f"""[Modules]
AssetServices=RegionAssetConnector
AssetCaching=FlotsamAssetCache
[AssetCache]
FileCacheEnabled=false
MemoryCacheEnabled=true
[AssetService]
LocalGridAssetService=OpenSim.Services.AssetService.dll:AssetService
HypergridAssetService=OpenSim.Services.Connectors.dll:HGAssetServiceConnector
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{Path(root) / 'assets.db'};version=3;Pooling=false
AssetLoaderEnabled=false
[HGAssetService]
DisallowImport={deny}
[ClientStack.LindenCaps]
Cap_GetTexture=localhost
""", encoding='utf-8')
            result = subprocess.check_output(
                [sys.argv[1], '--remote', f'http://127.0.0.1:{server.server_port}', asset, str(config), *extra],
                text=True, timeout=15).strip()
            assert result == expected, result
        assert calls == ['/assets/' + asset] * 3, calls
        for prefix in ('missing', 'invalid', 'nil'):
            result = subprocess.check_output(
                [sys.argv[1], '--remote', f'http://127.0.0.1:{server.server_port}/{prefix}', asset],
                text=True, timeout=15).strip()
            assert result == 'missing', result
        assert calls[-3:] == [f'/{prefix}/assets/{asset}' for prefix in ('missing', 'invalid', 'nil')], calls
        print('PASS: remote asset decoding, cache reuse, import denial, and invalid responses')
        config = Path(root) / "grid-assets.ini"
        config.write_text(f"""[Modules]
AssetServices=RegionAssetConnector
[AssetService]
LocalGridAssetService=OpenSim.Services.Connectors.dll:AssetServicesConnector
AssetServerURI=
AuthType=BasicHttpAuthentication
HttpAuthUsername=grid
HttpAuthPassword=asset
[Network]
asset_server_url=http://127.0.0.1:{server.server_port}
""", encoding="utf-8")
        for status, response, expected in (
            (200, stored_id, f"stored|{stored_id}"),
            (200, "00000000-0000-0000-0000-000000000000", "rejected"),
            (503, stored_id, "rejected"),
        ):
            write_reply[:] = status, response
            result = subprocess.check_output(
                [sys.argv[1], "--grid-store", str(config)], text=True, timeout=15
            ).strip()
            assert result == expected, result
        assert grid_calls == [
            ("POST", "/assets/"),
            ("GET", f"/assets/{stored_id}"),
            ("POST", "/assets/"),
            ("POST", "/assets/"),
        ], grid_calls
        print("PASS: authenticated grid asset writes, reassigned UUID, and rejection handling")
        subprocess.run(
            [sys.argv[1], "--grid-mutate", str(config), mutation_id], check=True, timeout=15
        )
        assert mutation_calls == [
            ("GET", mutation_path),
            ("GET", mutation_path + "/metadata"),
            ("POST", mutation_path),
            ("GET", mutation_path),
            ("GET", mutation_path + "/metadata"),
            ("POST", mutation_path),
            ("GET", mutation_path),
            ("DELETE", mutation_path),
            ("GET", mutation_path),
        ], mutation_calls
        print("PASS: authenticated grid asset updates, deletion, and foreign mutation rejection")
    finally:
        server.shutdown()
