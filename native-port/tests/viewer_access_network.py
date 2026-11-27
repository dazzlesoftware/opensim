"""Exercise the original viewer access settings through actual native login."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    args = parser.parse_args()
    args.native = args.native.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    cases = [
        ('default', '', 'Firestorm', True),
        ('allowed-prefix', '[Startup]\nAllowedClients= Other, FiReStOrM \n', 'Firestorm', True),
        ('allow-mismatch', '[Startup]\nAllowedClients=Other\n', 'Firestorm', False),
        ('denied-prefix', '[AccessControl]\nDeniedClients= FIRESTORM \n', 'Firestorm', False),
        ('denied-wins', '[AccessControl]\nAllowedClients=firestorm\nDeniedClients=firestorm\n', 'Firestorm', False),
        ('access-override', '[Startup]\nAllowedClients=Other\n[AccessControl]\nAllowedClients=Firestorm\n', 'Firestorm', True),
        ('empty-override', '[Startup]\nAllowedClients=Other\n[AccessControl]\nAllowedClients=\n', 'Firestorm', True),
        ('banned-alias', '[Startup]\nBannedClients=Firestorm\n', 'Firestorm', False),
        ('denied-before-alias', '[Startup]\nBannedClients=Firestorm\nDeniedClients=Other\n', 'Firestorm', True),
        ('whitespace-alias', '[Startup]\nDeniedClients="   "\nBannedClients=Firestorm\n', 'Firestorm', False),
        ('empty-deny-entry', '[AccessControl]\nDeniedClients=Other,\n', 'Firestorm', False),
        ('unicode-case', '[AccessControl]\nAllowedClients=ÉCLAIR\n', 'éclair', True),
    ]
    results = []
    with tempfile.TemporaryDirectory(dir=args.work, prefix='viewer-access-') as temporary:
        root = Path(temporary)
        config = root / 'region.ini'
        base = '''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
[Network]
http_listener_port=0
[NativeRegion]
ID=11111111-1111-4111-8111-111111111111
Name=Viewer Access
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
AllowedClients=
DeniedClients=
[GatekeeperService]
; Keep the separate login-service regular expressions out of this region test.
AllowedClients=
DeniedClients=
'''
        config.write_text(base, encoding='utf-8')
        command = [str(args.native), '--config', str(config)]
        setup = [(['--init-db'], None)]
        setup += [(['--create-user', 'Preview', f'Case{index}'], 'preview-password\n')
                  for index in range(len(cases))]
        for options, password in setup:
            result = subprocess.run(command + options, cwd=root, input=password, text=True,
                                    capture_output=True, timeout=40)
            check(result.returncode == 0, result.stdout + result.stderr)
        for index, (name, settings, channel, allowed) in enumerate(cases):
            config.write_text(base + settings, encoding='utf-8')
            server = PreviewServer(command, root, args.work / f'{name}.log')
            viewer = None
            try:
                proxy = xmlrpc.client.ServerProxy(server.start())
                login = proxy.login_to_simulator(dict(first='Preview', last=f'Case{index}', passwd='preview-password',
                                                      start='home', channel=channel, version='1.0'))
                check((login.get('login') == 'true') == allowed, f'{name}: {login}')
                if allowed:
                    viewer = Viewer(login)
                    viewer.connect()
                    viewer.send(bytes.fromhex('ffff00fc') + viewer.credentials())
                    viewer.wait(bytes.fromhex('ffff00fd'))
                else:
                    check('your viewer is banned' in login.get('message', ''), f'{name}: wrong rejection: {login}')
                results.append(dict(case=name, allowed=allowed, passed=True))
            finally:
                if viewer:
                    viewer.socket.close()
                server.stop()
    (args.work / 'results.json').write_text(json.dumps(dict(passed=True, cases=results), indent=2) + '\n')
    print(f'PASS: {len(results)} viewer access configuration/login cases')


if __name__ == '__main__':
    main()
