"""Prepare a separate local Linux preview through the normal native CLI."""

import configparser
import os
from pathlib import Path
import re
import subprocess


def main():
    root = Path('/data')
    root.mkdir(parents=True, exist_ok=True)
    os.chdir(root)
    binary = Path('/build/opensim-native')
    config = root / 'region.ini'
    port = int(os.environ.get('OPENSIM_PREVIEW_PORT', '19091'))
    password = os.environ.get('OPENSIM_PREVIEW_PASSWORD', 'preview-password')
    if not 1 <= port <= 65535:
        raise ValueError('Preview port must be between 1 and 65535')
    if not config.exists():
        config.write_text(f'''[NativeServer]
ListenAddress=0.0.0.0
EnableLocalRegion=true
EnableRegionAssets=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port={port}
[NativeRegion]
ID=22222222-2222-4222-8222-222222222222
Name=Linux Preview
TerrainDatabase=region.db
UDPPort={port}
BindAddress=0.0.0.0
ExternalHost=127.0.0.1
Physics=ubODE
PhysicsLibrary=/src/bin/lib64/libubode-x86_64.so
[Terrain]
InitialTerrain=flat
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
[Modules]
AssetServices=LocalAssetServicesConnector
AssetCaching=FlotsamAssetCache
[AssetCache]
FileCacheEnabled=false
MemoryCacheEnabled=true
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:/data/assets.db;version=3;Pooling=false
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs=/build/share/opensim-native/assets/AssetSets.xml
[ClientStack.LindenCaps]
Cap_GetTexture=localhost
Cap_GetAsset=localhost
Cap_UploadBakedTexture=localhost
Cap_FetchInventory2=localhost
Cap_FetchInventoryDescendents2=localhost
Cap_FetchLib2=localhost
Cap_FetchLibDescendents2=localhost
''', encoding='utf-8')
    settings = configparser.ConfigParser()
    settings.read(config)
    if settings.getint('Network', 'http_listener_port') != port:
        raise ValueError('Existing preview port differs; reuse its port to preserve its settings')
    command = [str(binary), '--config', str(config)]
    if not (root / 'users.db').exists():
        subprocess.run(command + ['--init-db'], check=True)
    # Per-account markers make normal restarts skip account creation. A narrowly
    # recognized duplicate also recovers an interrupted first-time setup.
    for last in ('First', 'Second'):
        marker = root / f'account-{last.lower()}.txt'
        if marker.exists():
            continue
        result = subprocess.run(command + ['--create-user', 'Preview', last],
                                input=password + '\n', capture_output=True, text=True)
        if result.returncode:
            if 'User already exists' not in result.stderr:
                raise RuntimeError(result.stdout + result.stderr)
            marker.write_text('Existing account retained\n', encoding='utf-8')
        else:
            principal = re.search(r'^Created user ([0-9a-f-]{36})$', result.stdout, re.MULTILINE)
            if not principal:
                raise RuntimeError('Account creation did not return its principal ID')
            marker.write_text(principal[1] + '\n', encoding='utf-8')
    print(f'Local Linux preview: http://127.0.0.1:{port}/', flush=True)
    os.execv(str(binary), command + ['--console'])


if __name__ == '__main__':
    main()
