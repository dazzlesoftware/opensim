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
import xmlrpc.client


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
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
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
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

        def login(extra, process_env, label):
            process = subprocess.Popen(command + extra + ['--console'], env=process_env,
                                       cwd=root, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding='utf-8')
            lines = queue.Queue()
            errors = []
            def output():
                for line in process.stdout:
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
                    check(response['region_x'] == 6789*256 and response['region_y'] == 4321*256,
                          label + ' uses environment-expanded include and override-directory coordinates')
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
    report = dict(passed=True, checks=checks, productionRuntime='native C++',
                  scope='Real CLI/database initialization/user creation/HTTP login through local include, nested constant-root include, cycle suppression, process environment and pre-validation argv precedence')
    (args.work / 'native-startup-network-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS:', len(checks), 'actual native startup/network checks')

if __name__ == '__main__':
    main()
