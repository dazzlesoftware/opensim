"""Complete a seated crossing using two native executables and normal viewer packets."""

import argparse
import base64
import contextlib
import json
from pathlib import Path
import platform
import re
import socket
import struct
import subprocess
import time
import uuid
import urllib.request
import xml.etree.ElementTree as ET
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, decode, post


def region_handle(value):
    # EventQueue U64 values use the SDK's eight-byte, big-endian binary form.
    if isinstance(value, int) or value.isdecimal():
        return int(value)
    return int.from_bytes(base64.b64decode(value), 'big')


def object_row(data):
    check(len(data) >= 53 and data[0] == 12 and data[11] == 1,
          'Expected one full object update')
    length = data[52]
    check(len(data) >= 57 + length and length in (60, 76), 'Invalid full object motion data')
    position_offset = 69 if length == 76 else 53
    return dict(local=struct.unpack_from('<I', data, 12)[0],
                identity=uuid.UUID(bytes=data[17:33]),
                parent=struct.unpack_from('<I', data, 53 + length)[0],
                position=struct.unpack_from('<3f', data, position_offset))


def wait_object(viewer, identity, predicate=lambda row: True):
    data = viewer.wait(b'\x0c', predicate=lambda packet:
                       len(packet) >= 53 and packet[17:33] == identity.bytes
                       and predicate(object_row(packet)))
    return object_row(data)


def wait_script_chat(viewer, identity, message):
    def matches(data):
        if len(data) < 5:
            return False
        start = 5 + data[4]
        if len(data) < start + 49 or data[start:start + 16] != identity.bytes:
            return False
        size = struct.unpack_from('<H', data, start + 47)[0]
        return data[start + 49:start + 49 + size].rstrip(b'\0') == message.encode()
    return viewer.wait(bytes.fromhex('ffff008b'), predicate=matches)


def script_query(viewer):
    message = b'checkpoint\0'
    viewer.send(bytes.fromhex('ffff0050') + viewer.credentials() + struct.pack('<H', len(message))
                + message + b'\x01' + struct.pack('<i', 7221))


def create_script(viewer, local, identity, caps):
    agent = uuid.UUID(viewer.login['agent_id']).bytes
    zero = bytes(16)
    name = b'Crossing checkpoint\0'
    inventory = zero + identity.bytes + agent + agent + zero
    inventory += struct.pack('<5I', *([0x7fffffff] * 5)) + b'\0' + zero
    inventory += struct.pack('<bbIBi', 10, 10, 0, 0, 0)
    inventory += bytes([len(name)]) + name + b'\x01\0' + struct.pack('<iI', int(time.time()), 0)
    viewer.send(bytes.fromhex('ffff0130') + viewer.credentials() + zero + struct.pack('<IB', local, 1) + inventory)
    wait_script_chat(viewer, identity, 'Script running')
    viewer.send(bytes.fromhex('ffff0121') + viewer.credentials() + struct.pack('<I', local))
    reply = viewer.wait(bytes.fromhex('ffff0122'), identity.bytes,
                        predicate=lambda data: len(data) >= 23 and struct.unpack_from('<h', data, 20)[0] >= 1)
    filename = reply[23:23 + reply[22]]
    check(bool(filename), 'New script must appear in downloadable task inventory')
    transfer = uuid.uuid4().int & ((1 << 64) - 1)
    viewer.send(bytes.fromhex('ffff009c') + struct.pack('<Q', transfer) + bytes([len(filename)]) + filename
                + bytes([99, 1, 1]) + zero + struct.pack('<h', -1))
    pieces, final, total = {}, None, None
    while final is None or total is None or len(pieces) != final + 1:
        packet = viewer.wait(b'\x12', predicate=lambda data:
                             len(data) >= 15 and struct.unpack_from('<Q', data, 1)[0] == transfer
                             and (struct.unpack_from('<I', data, 9)[0] & 0x7fffffff) not in pieces)
        raw = struct.unpack_from('<I', packet, 9)[0]
        number = raw & 0x7fffffff
        size = struct.unpack_from('<H', packet, 13)[0]
        payload = packet[15:15 + size]
        check(number < 1024, 'Task inventory transfer must stay within fixture bounds')
        if number == 0:
            total = struct.unpack_from('<I', payload)[0]
            payload = payload[4:]
        pieces[number] = payload
        if raw & 0x80000000:
            final = number
        viewer.send(b'\x13' + struct.pack('<QI', transfer, raw))
    text = b''.join(pieces[number] for number in range(final + 1))
    check(len(text) == total, 'Task inventory download length must match')
    items = re.findall(rb'\bitem_id\s+([0-9a-f-]{36})', text)
    check(len(items) == 1 and name.rstrip(b'\0') in text, 'Expected exactly the new task script')
    item = uuid.UUID(items[0].decode())
    upload = post(caps['UpdateScriptTask'], dict(item_id=item, task_id=identity, is_script_running=True))
    check(upload['state'] == 'upload', str(upload))
    source = b'''integer saved = 41;
default {
    state_entry() {
        llListen(7221, "", llGetOwner(), "checkpoint");
        llOwnerSay("checkpoint ready " + (string)saved);
    }
    listen(integer channel, string name, key sender, string message) {
        ++saved;
        llOwnerSay("checkpoint value " + (string)saved);
    }
}'''
    request = urllib.request.Request(upload['uploader'], source, {'Content-Type': 'application/octet-stream'})
    with urllib.request.urlopen(request, timeout=10) as response:
        complete = decode(ET.fromstring(response.read()))
    check(complete.get('state') == 'complete' and complete.get('compiled') in ('1', 'true', True), str(complete))
    wait_script_chat(viewer, identity, 'checkpoint ready 41')
    script_query(viewer)
    wait_script_chat(viewer, identity, 'checkpoint value 42')
    return item


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    native = args.native.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    result_path = args.work / 'results.json'
    result_path.unlink(missing_ok=True)
    root = args.work / ('fixture-' + uuid.uuid4().hex)
    root.mkdir()
    control = native.parent / 'share/opensim-native/assets/AssetSets.xml'
    if not control.is_file():
        control = native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
    check(control.is_file(), 'Native runtime must include its bundled asset set')
    ids = [uuid.uuid4(), uuid.uuid4()]
    destination_handle = ((1001 * 256) << 32) | (1000 * 256)
    configs = []
    for index, name in enumerate(('Seated Source', 'Seated Destination')):
        config = root / f'region-{index}.ini'
        config.write_text(f'''[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableRegionAssets=true
EnableSimulationEndpoint=true
EnableGridEndpoint=true
EnablePresenceEndpoint=true
EnableGridUserEndpoint=true
GridRegionProvider=SQLite
GridRegionDatabase=regions.db
UserAccountDatabase=users.db
AuthenticationDatabase=users.db
InventoryDatabase=inventory.db
GridUserDatabase=gridusers.db
EnableAvatarService=true
AvatarDatabase=avatars.db
[Network]
http_listener_port=0
[NativeRegion]
ID={ids[index]}
Name={name}
LocationX={1000 + index}
LocationY=1000
TerrainDatabase=region-{index}.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[PresenceService]
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=presence.db
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
[LoginService]
RequireInventory=true
[Terrain]
InitialTerrain=flat
[Modules]
AssetServices=LocalAssetServicesConnector
AssetCaching=FlotsamAssetCache
[AssetCache]
FileCacheEnabled=false
MemoryCacheEnabled=true
[AssetService]
LocalServiceModule=OpenSim.Services.AssetService.dll:AssetService
StorageProvider=OpenSim.Data.SQLite.dll
ConnectionString=URI=file:{root / 'assets.db'};version=3;Pooling=false
DefaultAssetLoader=OpenSim.Framework.AssetLoader.Filesystem.dll
AssetLoaderArgs={control}
''', encoding='utf-8')
        configs.append(config)
    commands = [[str(native), '--config', str(config)] for config in configs]
    for options, password in ((['--init-db'], None),
                              (['--create-user', 'Seated', 'Rider'], 'crossing-password\n')):
        completed = subprocess.run(commands[0] + options, input=password, text=True, cwd=root,
                                   capture_output=True, timeout=40)
        check(completed.returncode == 0, completed.stdout + completed.stderr)
    servers = [PreviewServer(command, root, args.work / f'region-{index}.log')
               for index, command in enumerate(commands)]
    viewers = []
    checks = []
    try:
        source_url = servers[0].start()
        destination_url = servers[1].start()
        login = xmlrpc.client.ServerProxy(source_url).login_to_simulator(dict(
            first='Seated', last='Rider', passwd='crossing-password',
            start='uri:Seated Source&128&128&25', channel='Seated crossing acceptance', version='1.0'))
        check(login.get('login') == 'true', str(login))
        check(login['seed_capability'].startswith(source_url.rstrip('/') + '/'),
              'Login must enter the source process')
        source = Viewer(login)
        viewers.append(source)
        source.connect()
        checks.append('Fresh CLI account logs into source through XML-RPC and UDP')
        caps = post(login['seed_capability'], ['EventQueueGet', 'UpdateScriptTask'])
        local, identity = source.create_box()
        script_item = create_script(source, local, identity, caps)
        checks.append('Viewer creates, downloads and edits a task script; its listener changes the global to 42')
        source.received.clear()
        source.send(b'\x06' + source.credentials() + identity.bytes + struct.pack('<3f', 0, 0, 0))
        source.wait(b'\x15', identity.bytes)
        source.send(b'\x07' + source.credentials())
        agent = uuid.UUID(login['agent_id'])
        initial = wait_object(source, agent, lambda row: row['parent'] == local)
        checks.append('Viewer rezzes a cube and sits through ordinary sit packets')
        source.send(bytes.fromhex('ff04') + source.credentials() + b'\x01'
                    + struct.pack('<I3f', local, 260, 129, 25))
        ack = 0
        crossed = None
        destination = None
        deadline = time.monotonic() + 20
        while crossed is None and time.monotonic() < deadline:
            events = post(caps['EventQueueGet'], dict(ack=ack, done=False))
            ack = int(events['id'])
            for event in events['events']:
                if event['message'] == 'EnableSimulator':
                    info = event['body']['SimulatorInfo'][0]
                    if region_handle(info['Handle']) != destination_handle:
                        continue
                    if destination is None:
                        child_login = dict(login, sim_ip=socket.inet_ntoa(base64.b64decode(info['IP'])),
                                           sim_port=int(info['Port']))
                        destination = Viewer(child_login)
                        viewers.append(destination)
                        code = struct.pack('<I', int(login['circuit_code']))
                        destination.send(bytes.fromhex('ffff0003') + code
                                         + uuid.UUID(login['session_id']).bytes + agent.bytes)
            crossed = next((event for event in events['events'] if event['message'] == 'CrossedRegion'), None)
        check(crossed is not None, 'Seated object move did not deliver CrossedRegion')
        region = crossed['body']['RegionData'][0]
        endpoint = (socket.inet_ntoa(base64.b64decode(region['SimIP'])), int(region['SimPort']))
        check(region_handle(region['RegionHandle']) == destination_handle,
              'CrossedRegion must address the destination region')
        check(region['SeedCapability'].startswith(destination_url.rstrip('/') + '/'),
              'CrossedRegion seed must address the destination process')
        checks.append('Production object handoff and rider creation produce destination CrossedRegion')
        arrival_login = dict(login, sim_ip=endpoint[0], sim_port=endpoint[1],
                             seed_capability=region['SeedCapability'])
        check(destination is not None, 'Viewer must connect the announced neighbor before crossing')
        check(destination.endpoint == endpoint, 'Neighbor circuit and crossing destination must agree')
        destination.login = arrival_login
        code = struct.pack('<I', int(login['circuit_code']))
        destination.send(bytes.fromhex('ffff0003') + code
                         + uuid.UUID(login['session_id']).bytes + agent.bytes)
        destination.send(bytes.fromhex('ffff00f9') + destination.credentials() + code)
        destination.wait(bytes.fromhex('ffff00fa'))
        box = wait_object(destination, identity)
        check(all(abs(actual - expected) < .001 for actual, expected in zip(box['position'], (4, 129, 25))),
              f'Destination object position differs: {box}')
        arrived = wait_object(destination, agent, lambda row: row['parent'] == box['local'])
        check(all(abs(actual - expected) < .001
                  for actual, expected in zip(arrived['position'], initial['position'])),
              f'Seated offset changed: {initial} -> {arrived}')
        checks.append('Destination completes movement and publishes rider parented to the transferred cube')
        checks.append('Object UUID, destination-local position and seated offset survive the crossing')
        script_query(destination)
        wait_script_chat(destination, identity, 'checkpoint value 43')
        checks.append('Destination script listener resumes with saved global 42 and replies with 43 without reset')
        result = dict(platform=platform.system(), passed=True, checks=checks,
                      realFirestormVerified=False, twoNativeExecutables=True,
                      fixture=str(root), agent=str(agent), object=str(identity), script=str(script_item))
    finally:
        for viewer in viewers:
            with contextlib.suppress(OSError):
                viewer.socket.close()
        for server in reversed(servers):
            server.stop()
    checks.append('Both isolated native processes stop cleanly')
    result_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
