"""Isolated fresh-session timing matrix and source-compatible endpoint replacement control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import time
import urllib.error
import uuid
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, post


USE = bytes.fromhex('ffff0003')
MOVEMENT = bytes.fromhex('ffff00f9')
HANDSHAKE = bytes.fromhex('ffff0094')
ACK = bytes.fromhex('fffffffb')


class TracedViewer(Viewer):
    def __init__(self, login, events, run_id):
        super().__init__(login)
        self.events = events
        self.identity = uuid.uuid4().hex
        self.run_id = run_id
        self.local_endpoint = list(self.socket.getsockname())
        self.closed = False
        self.record('socket-open')

    def record(self, event, **details):
        self.events.append(dict(event=event, monotonic_ns=time.monotonic_ns(), run=self.run_id,
                                socket=self.identity, endpoint=self.local_endpoint,
                                destination=list(self.endpoint), process=os.getpid(),
                                agent=self.login['agent_id'], session=self.login['session_id'], **details))

    def send(self, payload):
        self.record('send', message=payload[:4].hex(), sequence=self.sequence + 1)
        super().send(payload)

    def wait(self, *args, **kwargs):
        check('drop' not in kwargs, 'Timing experiment must not discard handshake packets')

        def observe(packet, data):
            self.record('receive', message=data[:4].hex(), flags=packet[0],
                        sequence=struct.unpack_from('>I', packet, 1)[0])
            return False

        return super().wait(*args, drop=observe, **kwargs)

    def circuit(self):
        return USE + struct.pack('<I', int(self.login['circuit_code'])) + \
            uuid.UUID(self.login['session_id']).bytes + uuid.UUID(self.login['agent_id']).bytes

    def connect_gap(self, gap_ms):
        for _ in range(3):
            self.send(self.circuit())
        time.sleep(gap_ms / 1000)
        self.send(MOVEMENT + self.credentials() + struct.pack('<I', int(self.login['circuit_code'])))
        self.wait(HANDSHAKE)
        self.send(bytes.fromhex('ffff0095') + self.credentials() + struct.pack('<I', 0))
        self.wait(bytes.fromhex('ffff00fa'))
        # Repeated requests from the established endpoint must be ACKed without
        # replacing the client. Drain receives so ACK trailers count as well.
        sequences = []
        for _ in range(3):
            self.send(self.circuit())
            sequences.append(self.sequence)
        self.drain(.75)
        check(set(sequences) <= self.acknowledged_sequences,
              f'Established same-endpoint requests were not all acknowledged: {sequences}')
        post(self.login['seed_capability'], [])
        self.record('arrival-complete', gap_ms=gap_ms)

    def drain(self, seconds):
        try:
            self.wait(b'never-a-valid-packet', timeout=seconds)
        except AssertionError as error:
            check(str(error).startswith('Preview UDP timeout'), str(error))

    def close(self):
        if self.closed:
            return
        self.record('socket-close')
        self.closed = True
        self.socket.close()


def caps_removed(viewer):
    deadline = time.monotonic() + 10
    while True:
        try:
            post(viewer.login['seed_capability'], [])
        except urllib.error.HTTPError as error:
            if error.code == 404:
                viewer.record('capabilities-removed')
                return
            raise
        check(time.monotonic() < deadline, 'Old capabilities survived teardown')
        time.sleep(.025)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--reference', type=Path, help='Original distribution bin directory for existing baseline helper')
    parser.add_argument('--dotnet', default='dotnet')
    args = parser.parse_args()
    check(1 <= args.cycles <= 100, 'Cycles must be between 1 and 100')
    binary = args.native.resolve()
    run_id = uuid.uuid4().hex
    work = args.work.resolve() / ('session-timing-' + run_id)
    work.mkdir(parents=True)
    events = []
    def sha256(path):
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        return digest.hexdigest()

    script = Path(__file__).resolve()
    helper = script.with_name('preview_entry_network.py')
    report = dict(runId=run_id, platform=platform.system(), native=str(binary), passed=False,
                  realFirestormVerified=False, historicalRootCauseConfirmed=False,
                  hashes=dict(native=sha256(binary), script=sha256(script), importedHelper=sha256(helper)),
                  script=str(script), importedHelper=str(helper),
                  reference=dict(status='pending', reason='No original distribution requested'), cases=[])
    config = work / 'region.ini'
    control = binary.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = binary.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Test runtime must include its bundled asset set')
    config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableRegionAssets=true
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port=0
[NativeRegion]
ID={uuid.uuid4()}
Name=Session Timing
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
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
ConnectionString=URI=file:{work / 'assets.db'};version=3;Pooling=false
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs={control}
''', encoding='utf-8')
    command = [str(binary), '--config', str(config)]
    server = PreviewServer(command, work, work / ('server-' + run_id + '.log'))
    report['serverTraceLog'] = str(server.log_path)
    viewers = []
    previous_trace = os.environ.get('OPENSIM_TRACE_CIRCUITS')
    try:
        for options, password in [(['--init-db'], None),
                                  (['--create-user', 'Timing', 'First'], 'timing-password\n')]:
            result = subprocess.run(command + options, cwd=work, input=password, text=True,
                                    capture_output=True, timeout=40)
            check(result.returncode == 0, result.stdout + result.stderr)
        os.environ['OPENSIM_TRACE_CIRCUITS'] = '1'
        url = server.start()
        report['server'] = dict(pid=server.process.pid, parentPid=os.getpid(), command=command + ['--console'],
                                workingDirectory=str(work), loginUri=url, traceMode='1', traceRunId=run_id)
        proxy = xmlrpc.client.ServerProxy(url)
        sessions = set()

        def login():
            result = proxy.login_to_simulator(dict(first='Timing', last='First', passwd='timing-password',
                                                   start='last', channel='Session timing ' + run_id, version='1.0'))
            check(result.get('login') == 'true', str(result))
            check(result['session_id'] not in sessions, 'Fresh login reused session identity')
            sessions.add(result['session_id'])
            viewer = TracedViewer(result, events, run_id)
            viewers.append(viewer)
            return viewer

        for cycle in range(args.cycles):
            for gap in (0, 10, 100, 500):
                viewer = login()
                viewer.connect_gap(gap)
                viewer.send(bytes.fromhex('ffff00fc') + viewer.credentials())
                viewer.wait(bytes.fromhex('ffff00fd'))
                caps_removed(viewer)
                viewer.close()
                viewers.remove(viewer)
                report['cases'].append(dict(cycle=cycle, gapMs=gap, freshSession=True,
                                            singleSocket=True, sameEndpointRepeats=3, passed=True))

        old = login()
        old.connect_gap(0)
        replacement = TracedViewer(old.login, events, run_id)
        viewers.append(replacement)
        check(old.socket.getsockname() != replacement.socket.getsockname(), 'Control did not change endpoint')
        replacement.send(replacement.circuit())
        replacement_sequence = replacement.sequence
        old.wait(bytes.fromhex('ffff0098'))
        replacement.drain(.75)
        check(replacement_sequence in replacement.acknowledged_sequences, 'Replacement request was not ACKed')
        caps_removed(old)
        replacement.drain(.75)
        check(not any(packet.startswith(HANDSHAKE) for packet in replacement.received),
              'Removed old session unexpectedly admitted replacement endpoint')
        old.close()
        replacement.close()
        viewers.clear()
        recovered = login()
        recovered.connect_gap(0)
        report['replacementControl'] = dict(passed=True, oldDisabled=True, oldCapabilitiesRemoved=True,
                                             replacementAcknowledged=True, replacementAdmitted=False,
                                             freshSessionRecovery=True)
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        for viewer in viewers:
            viewer.close()
        try:
            server.stop()
        except Exception as error:
            report['passed'] = False
            report['shutdownError'] = str(error)
            raise
        finally:
            if previous_trace is None:
                os.environ.pop('OPENSIM_TRACE_CIRCUITS', None)
            else:
                os.environ['OPENSIM_TRACE_CIRCUITS'] = previous_trace
            (work / 'viewer-events.json').write_text(json.dumps(events, indent=2), encoding='utf-8')
            (work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if args.reference:
        reference_work = work / 'original-baseline'
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('original_peer_network.py')),
                                 '--bin', str(args.reference.resolve()), '--work', str(reference_work),
                                 '--dotnet', args.dotnet], capture_output=True, text=True, timeout=300)
        (work / 'original-baseline-output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        result_file = reference_work / 'results.json'
        report['reference'] = dict(status='baseline-passed' if result.returncode == 0 else 'baseline-failed',
                                   timingMatrix='pending: existing original helper runs its baseline only',
                                   evidence=str(result_file), exitCode=result.returncode)
        (work / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        check(result.returncode == 0, 'Original baseline failed; see original-baseline-output.log')
    print('PASS: isolated timing matrix and replacement control; evidence: ' + str(work / 'results.json'))


if __name__ == '__main__':
    main()
