"""Exercise the configured native region asset connector over HTTP."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
import subprocess
import sys
import tempfile

asset = '91919191-9191-4191-8191-919191919191'
wire = (f'<AssetBase><Data>AAH/</Data><FullID><Guid>{asset}</Guid></FullID>'
        f'<ID>{asset}</ID><Name>Foreign &amp; local</Name><Type>0</Type>'
        '<Flags>Maptile Collectable</Flags></AssetBase>').encode()
calls = []


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
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
    finally:
        server.shutdown()
