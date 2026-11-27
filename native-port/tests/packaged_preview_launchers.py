"""Check the shipped Windows/Docker entry points in separate QA worlds."""
import argparse
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request
import uuid
import xmlrpc.client

from preview_entry_network import Viewer


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def ready(port, process=None):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if process and process.poll() is not None:
            raise RuntimeError('Windows launcher exited; inspect its log')
        try:
            with urllib.request.urlopen(f'http://127.0.0.1:{port}/get_grid_info', timeout=1) as response:
                if response.status == 200:
                    return
        except OSError:
            pass
        time.sleep(.1)
    raise TimeoutError('Packaged launcher did not become ready')


def probe(port):
    viewers, ids = [], []
    try:
        proxy = xmlrpc.client.ServerProxy(f'http://127.0.0.1:{port}/')
        for last in ['First', 'Second']:
            login = proxy.login_to_simulator(dict(first='Preview', last=last, passwd='preview-password',
                start='last', channel='Packaged preview check', version='1.0'))
            assert login.get('login') == 'true', login
            assert int(login['sim_port']) == port, 'Advertised UDP port differs from the published port'
            viewer = Viewer(login)
            viewers.append(viewer)
            viewer.connect()
            ids.append(login['agent_id'])
        assert ids[0] != ids[1]
        for viewer, peer in zip(viewers, reversed(ids)):
            viewer.wait(b'\x0c', uuid.UUID(peer).bytes)
        return ids
    finally:
        for viewer in viewers:
            viewer.send(bytes.fromhex('ffff00fc') + viewer.credentials())
            viewer.wait(bytes.fromhex('ffff00fd'))
            viewer.socket.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--pwsh', default=shutil.which('pwsh'), required=shutil.which('pwsh') is None)
    args = parser.parse_args()
    root = args.root.resolve()
    evidence = root / 'evidence'
    evidence.mkdir(exist_ok=True)
    smoke = Path(tempfile.mkdtemp(prefix='windows-launcher-', dir=evidence))
    shutil.copytree(root / 'windows-amd64', smoke, dirs_exist_ok=True)
    instance = 'qa-' + uuid.uuid4().hex[:16]
    results = dict(realFirestorm=False, qaInstance=instance, platforms={})
    for platform in ['Windows', 'LinuxDocker']:
        port = free_port()
        rounds = []
        for cycle in range(2):
            process = None
            log = None
            docker_command = [args.pwsh, '-NoProfile', '-File', str(root / 'Start-Linux-Docker.ps1'),
                              '-Instance', instance, '-Port', str(port)]
            if platform == 'Windows':
                log = (evidence / f'windows-launcher-{cycle}.log').open('w')
                process = subprocess.Popen([args.pwsh, '-NoProfile', '-File', str(smoke / 'Start-Preview.ps1'),
                    '-Port', str(port)], stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True)
            else:
                subprocess.run(docker_command, check=True, timeout=120)
            try:
                ready(port, process)
                rounds.append(probe(port))
            finally:
                if process:
                    if process.poll() is None:
                        process.stdin.write('quit\n')
                        process.stdin.flush()
                    process.wait(timeout=45)
                    log.close()
                    assert process.returncode == 0, process.returncode
                else:
                    subprocess.run(docker_command + ['-Mode', 'Stop'], check=True, timeout=60)
        assert rounds[0] == rounds[1], 'Clean restart changed account identities'
        results['platforms'][platform] = dict(passed=True, freshBootstrap=True, mutualAvatarUdp=True,
            advertisedUdpPortVerified=True, cleanRestartStableAccounts=True, accountIds=rounds[0])
        (evidence / 'packaged-launcher-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('PASS: packaged Windows/Docker launchers, two avatars and clean restart')


if __name__ == '__main__':
    main()
