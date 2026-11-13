"""Real executable coverage of original local startup composition and login."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time
import urllib.request
import xmlrpc.client
from simulator_features_network import decode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--install', nargs=3, metavar=('CMAKE', 'BUILD_DIR', 'CONFIG'))
    args = parser.parse_args()
    native = str(args.native.resolve())
    args.work.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        checks.append(message)

    with tempfile.TemporaryDirectory(dir=args.work) as temporary:
        root = Path(temporary).resolve()
        sample = Path(__file__).resolve().parents[1] / 'native-region.ini.example'
        if args.install:
            cmake, build_dir, configuration = args.install
            prefix = root / 'install'
            command = [cmake, '--install', build_dir, '--prefix', str(prefix)]
            if configuration:
                command.extend(['--config', configuration])
            install = subprocess.run(command, capture_output=True, text=True,
                                     encoding='utf-8', errors='replace', timeout=120)
            check(install.returncode == 0,
                  'Install native runtime: ' + install.stderr + install.stdout[-4000:])
            native = str(prefix / 'bin' / args.native.name)
            sample = prefix / 'share/opensim-native/native-region.ini.example'
            # A neighboring source-style layout must not shadow the installed libraries.
            source_libraries = prefix / 'bin/lib64'
            source_libraries.mkdir()
            for library in (prefix / 'lib/opensim-native').iterdir():
                (source_libraries / library.name).write_bytes(b'invalid native library')
        (root / 'parts').mkdir()
        (root / 'config').mkdir()
        (root / 'config/override.INI').write_text('[NativeRegion]\nLocationY=4321\n', encoding='utf-8')
        config = root / 'startup.ini'
        config.write_text('''[Startup]
Include-services=parts/services.ini
[Environment]
OPENSIM_NATIVE_STARTUP_PORT=ignored
OPENSIM_NATIVE_STARTUP_REGION=ignored
OPENSIM_NATIVE_STARTUP_X=ignored
[Network]
http_listener_port=${Environment|OPENSIM_NATIVE_STARTUP_PORT}
''', encoding='utf-8')
        (root / 'parts/services.ini').write_text('''[Startup]
Include-region=region.ini
Include-cycle=startup.ini
[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableAssetEndpoint=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
[AssetService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:assets.db;version=3
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs=assets/AssetSets.xml
''', encoding='utf-8')
        # Original AddIncludes resolves every relative include against configDir,
        # including a reference discovered in a file in another directory.
        (root / 'region.ini').write_text('''[NativeRegion]
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
Name=${Environment|OPENSIM_NATIVE_STARTUP_REGION}
LocationX=${Environment|OPENSIM_NATIVE_STARTUP_X}
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
''', encoding='utf-8')
        env = dict(os.environ, OPENSIM_NATIVE_STARTUP_PORT='0',
                   OPENSIM_NATIVE_STARTUP_REGION='Included Startup Region',
                   OPENSIM_NATIVE_STARTUP_X='6789')
        command = [native, '--config', str(config)]
        for options, password in [(['--init-db'], None),
                                  (['--create-user', 'Startup', 'Viewer'], 'startup-password\n')]:
            result = subprocess.run(command + options, input=password,
                                    capture_output=True, text=True, env=env,
                                    cwd=root, timeout=45)
            check(result.returncode == 0, options[0] + ': ' + result.stderr)
        check(all((root / name).is_file() for name in
                  ['users.db', 'inventory.db', 'gridusers.db']),
              'Included service databases resolve against main config directory')

        def login(extra, process_env, label, coordinates=(6789, 4321), check_asset_endpoint=True,
                  check_texture_decoder=False):
            process = subprocess.Popen(command + extra + ['--console'], env=process_env,
                                       cwd=root, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding='utf-8')
            lines = queue.Queue()
            errors = []
            output_lines = []
            def output():
                for line in process.stdout:
                    output_lines.append(line)
                    lines.put(line)
                lines.put(None)
            def error():
                errors.extend(process.stderr)
            threads = [threading.Thread(target=output, daemon=True),
                       threading.Thread(target=error, daemon=True)]
            for thread in threads:
                thread.start()
            try:
                deadline = time.monotonic() + 30
                url = None
                while time.monotonic() < deadline:
                    line = lines.get(timeout=max(.01, deadline-time.monotonic()))
                    if line is None:
                        raise AssertionError(label + ' startup: ' + ''.join(errors))
                    if line.startswith('LISTENING '):
                        url = line.split(' ', 1)[1].strip()
                        break
                check(bool(url), label + ' starts listener')
                with xmlrpc.client.ServerProxy(url) as proxy:
                    credentials = dict(first='Startup', last='Viewer',
                                       passwd='startup-password', start='home',
                                       channel='Native startup test', version='1.0')
                    check(proxy.login_to_simulator({**credentials, 'passwd':'wrong'})['login'] == 'false',
                          label + ' rejects incorrect password')
                    response = proxy.login_to_simulator(credentials)
                    check(response.get('login') == 'true', label + ' authenticates and joins included region: ' + str(response))
                    check(response['seed_capability'].startswith(url), label + ' advertises actual listener')
                    check(len(response['inventory-skel-lib']) > 1,
                          label + ' loads bundled library folders from outside the source tree')
                    asset = '8ceccb12-5aee-3584-e4a5-66973a844296'
                    if check_asset_endpoint:
                        asset_url = url + f'assets/{asset}/data'
                    else:
                        seed = urllib.request.Request(
                            response['seed_capability'],
                            b'<llsd><array><string>ViewerAsset</string></array></llsd>',
                            {'Content-Type': 'application/llsd+xml'},
                        )
                        with urllib.request.urlopen(seed, timeout=5) as reply:
                            asset_url = decode(reply.read())['ViewerAsset'] + '?animatn_id=' + asset
                    with urllib.request.urlopen(asset_url, timeout=5) as reply:
                        expected = Path(__file__).resolve().parents[2] / 'bin/assets/AnimationsAssetSet/place_marker.dat'
                        check(reply.read() == expected.read_bytes(),
                              label + ' serves the bundled animation asset bytes')
                    check((response['region_x'], response['region_y']) ==
                          tuple(coordinate * 256 for coordinate in coordinates),
                          label + ' uses the configured region coordinates')
            finally:
                if process.poll() is None:
                    try:
                        process.stdin.write('quit\n')
                        process.stdin.flush()
                    except (OSError, BrokenPipeError):
                        pass
                    try:
                        process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
                for thread in threads:
                    thread.join(timeout=3)
            check(process.returncode == 0, label + ' exits cleanly: ' + ''.join(errors))
            if check_texture_decoder:
                diagnostics = ''.join(output_lines + errors)
                check('JPEG 2000 layer decoder:' not in diagnostics,
                      label + ' loads the bundled texture decoder: ' + diagnostics)

        def rejects(extra, process_env, expected, label):
            result = subprocess.run(command + extra + ['--console'], env=process_env,
                                    cwd=root, capture_output=True, text=True, timeout=30)
            check(result.returncode != 0 and expected in result.stderr and
                  'LISTENING ' not in result.stdout,
                  label + ': ' + result.stderr)

        login([], env, 'Local includes and environment')
        # argv merge must happen before NetworkServersInfo's typed read. A later
        # optional.value_or(GetInt(...)) still evaluates and rejects this value.
        login(['--port', '0', '--host', '127.0.0.1'],
              dict(env, OPENSIM_NATIVE_STARTUP_PORT='invalid-port'),
              'Command-line port precedence')
        # Environment expansion occurs before argv merge in the original. An
        # reference absent from Environment fails during that expansion;
        # a later --port must not hide this earlier failure.
        missing_env = dict(env)
        missing_env.pop('OPENSIM_NATIVE_STARTUP_PORT')
        original_main = config.read_text(encoding='utf-8')
        # Remove the declaration as well: a declared missing environment value
        # retains its file fallback when the empty Env source is merged.
        config.write_text(original_main.replace('OPENSIM_NATIVE_STARTUP_PORT=ignored\n', ''), encoding='utf-8')
        rejects(['--port', '0'], missing_env, 'OPENSIM_NATIVE_STARTUP_PORT',
                'Environment failure precedes argv merge')
        config.write_text(original_main, encoding='utf-8')
        services = root / 'parts/services.ini'
        original = services.read_text(encoding='utf-8')
        services.write_text(original.replace('EnableLocalRegion=true',
                                             'EnableLocalRegion=On'), encoding='utf-8')
        rejects([], env, 'Alias value not found: On',
                'File-backed config does not inherit argv aliases')
        services.write_text(original, encoding='utf-8')
        rejects([], dict(env, OPENSIM_NATIVE_STARTUP_X='8388608'),
                'NativeRegion LocationX is out of range',
                'Reject world coordinates that would overflow the native grid descriptor')
        services.write_text(original.replace('EnableLocalRegion=true\n', ''), encoding='utf-8')
        regions = root / 'Regions'
        regions.mkdir()
        region_file = regions / ('Regions.INI' if os.name == 'nt' else 'Regions.ini')
        region_file.write_text('''[First Region]
RegionUUID=11111111-1111-4111-8111-111111111111
Location=1000,1000
InternalPort=0
[Second Region]
RegionUUID=22222222-2222-4222-8222-222222222222
Location=1001,1000
InternalPort=0
''', encoding='utf-8')
        rejects([], env, 'Duplicate local region UDP port 0',
                'Reject duplicate configured port zero before region startup')
        region_file.write_text('[Missing UUID]\nLocation=1000,1000\nInternalPort=0\n',
                               encoding='utf-8')
        rejects([], env, 'Region Missing UUID requires a valid RegionUUID',
                'Discovered regions require their own identity')
        config.write_text(sample.read_text(encoding='utf-8')
                          .replace('assets/AssetSets.xml', './assets/AssetSets.xml')
                          .replace('http_listener_port = 9000', 'http_listener_port = 0')
                          .replace('UDPPort = 9000', 'UDPPort = 0'), encoding='utf-8')
        login([], env, 'Bundled region sample with ubODE and local assets',
              coordinates=(1000, 4321), check_asset_endpoint=False, check_texture_decoder=True)
    report = dict(passed=True, checks=checks, productionRuntime='native C++',
                  scope='Real CLI/database initialization/user creation/HTTP login through local include, nested constant-root include, cycle suppression, process environment and pre-validation argv precedence')
    (args.work / 'native-startup-network-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS:', len(checks), 'actual native startup/network checks')

if __name__ == '__main__':
    main()
