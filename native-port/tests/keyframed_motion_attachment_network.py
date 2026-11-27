"""Exercise running KFM inventory/ground attachments through the real application.

CLI and normal HTTP/UDP/capability entry points perform all writes. SQLite is
opened read-only for inventory observations. Each invocation retains its isolated
instance, logs, downloaded object assets, and results beneath --work.
"""
import argparse
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import time
import traceback
from urllib.request import Request, urlopen
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from preview_entry_network import PreviewServer, Viewer, check, post
from keyframed_motion_persistence_network import OBJECT, REGION, original_blob


def inventory(root, owner):
    with contextlib.closing(sqlite3.connect(f'file:{(root / "inventory.db").as_posix()}?mode=ro', uri=True)) as db:
        return dict(db.execute('SELECT inventoryID,assetID FROM inventoryitems WHERE avatarID=? AND assetType=6',
                               (owner,)).fetchall())


def until(predicate, message, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError(message)


def wait_for_ack(viewer, sequence):
    # Sequence numbers never repeat in this session. Clear cached packets so
    # wait() drains the socket until this particular command is acknowledged.
    viewer.received.clear()
    viewer.wait(b'', predicate=lambda data: sequence in viewer.acknowledged_sequences)


def full_object(viewer, identity=None, local=None, attached=False, acknowledge=False):
    viewer.received.clear()
    if local is not None:
        viewer.send(bytes.fromhex('ff03') + viewer.credentials() + struct.pack('<BBI', 1, 0, local))
        if acknowledge:
            wait_for_ack(viewer, viewer.sequence)
    def suitable(data):
        if len(data) < 65 or (identity is not None and data[17:33] != identity.bytes):
            return False
        parent_offset = 53 + data[52]
        if len(data) < parent_offset + 4 or data[37] != 9:
            return False
        parent = struct.unpack_from('<I', data, parent_offset)[0]
        return (parent != 0) == attached
    data = viewer.wait(b'\x0c', predicate=suitable)
    return dict(local=struct.unpack_from('<I', data, 12)[0], identity=uuid.UUID(bytes=data[17:33]),
                position=struct.unpack_from('<3f', data, 53))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--motion-blob', type=Path)
    parser.add_argument('--original-bin', type=Path)
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--select-ground-before-attach', action='store_true',
                        help='Select the running ground object, verify it pauses, then attach it')
    args = parser.parse_args()
    args.native = args.native.resolve()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    root = args.work / ('attachment-instance-' + uuid.uuid4().hex)
    root.mkdir()
    result = dict(status='running', observations=[], instance=str(root), python_pid=os.getpid(),
                  selected_ground_before_attach=args.select_ground_before_attach,
                  native_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),
                  test_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  helper_sha256={name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
                                 for name in ('preview_entry_network.py', 'keyframed_motion_persistence_network.py')})
    try:
        blob = original_blob(args)
        (root / 'original-motion.bin').write_bytes(blob)
        control = args.native.parent / 'share/opensim-native/assets/AssetSets.xml'
        if not control.is_file():
            control = args.native.parent.parent / 'share/opensim-native/assets/AssetSets.xml'
        check(control.is_file(), 'Native runtime must include bundled asset set')
        config = root / 'region.ini'
        config.write_text(f"""[NativeServer]
ListenAddress=127.0.0.1
EnableLocalRegion=true
EnableSimulationEndpoint=true
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
ID={REGION}
Name=KFM Attachments
TerrainDatabase=region.db
UDPPort=0
BindAddress=127.0.0.1
ExternalHost=127.0.0.1
[GridService]
DefaultRegionFlags=+DefaultRegion +FallbackRegion
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
[ClientStack.LindenCaps]
Cap_GetAsset=localhost
""", encoding='utf-8')
        command = [str(args.native), '--config', str(config)]
        for options, password in ((['--init-db'], None),
                                  (['--create-user', 'Attach', 'Viewer'], 'fixture-password\n')):
            setup = subprocess.run(command + options, cwd=root, input=password,
                                   text=True, capture_output=True, timeout=40)
            check(setup.returncode == 0, setup.stdout + setup.stderr)
        server = PreviewServer(command, root, root / 'server.log')
        viewer = None
        observations = result['observations']
        try:
            url = server.start().rstrip('/')
            login = xmlrpc.client.ServerProxy(url + '/').login_to_simulator(dict(
                first='Attach', last='Viewer', passwd='fixture-password', start='home',
                channel='KFM attachment acceptance', version='1.0'))
            check(login.get('login') == 'true', str(login))
            viewer = Viewer(login)
            viewer.connect()
            owner = login['agent_id']
            asset_cap = post(login['seed_capability'], ['ViewerAsset'])['ViewerAsset']
            def asset_xml(asset_id, label):
                with urlopen(asset_cap + '?object_id=' + asset_id, timeout=10) as response:
                    data = response.read()
                (root / (label + '.xml')).write_bytes(data)
                return ET.fromstring(data)
            xml = ('<SceneObjectGroup><SceneObjectPart>'
                   f'<UUID><UUID>{OBJECT}</UUID></UUID><OwnerID><UUID>{owner}</UUID></OwnerID>'
                   '<BaseMask>647168</BaseMask><OwnerMask>647168</OwnerMask>'
                   '<NextOwnerMask>532480</NextOwnerMask>'
                   '<Name>KFM Attachment Entry</Name><GroupPosition><X>120</X><Y>121</Y><Z>30</Z></GroupPosition>'
                   '<RotationOffset><X>0</X><Y>0</Y><Z>0</Z><W>1</W></RotationOffset>'
                   '<Scale><X>0.5</X><Y>0.5</Y><Z>0.5</Z></Scale>'
                   '<Shape><PCode>9</PCode><State>0</State></Shape></SceneObjectPart><OtherParts/>'
                   f'<KeyframeMotion>{base64.b64encode(blob).decode()}</KeyframeMotion></SceneObjectGroup>')
            request = Request(f'{url}/object/{OBJECT}/', json.dumps(dict(destination_uuid=REGION, sog=xml,
                               new_position=[120, 121, 30])).encode(), {'Content-Type': 'application/json'})
            with urlopen(request, timeout=10) as response:
                check(response.read() == b'True', 'Motion import rejected')
            ground = full_object(viewer, OBJECT)
            initial = ground['position']
            time.sleep(.3)
            ground = full_object(viewer, OBJECT, ground['local'])
            check(ground['position'][0] > initial[0], 'Imported ground motion did not advance')
            before = inventory(root, owner)
            viewer.take_copy(ground['local'])
            copied = until(lambda: next(((item, asset) for item, asset in inventory(root, owner).items()
                                         if item not in before), None), 'Take-copy did not create inventory object')
            copied_item, copied_asset = copied
            check(asset_xml(copied_asset, 'running-inventory-copy').find('KeyframeMotion') is not None,
                  'Normal take-copy lost its running KFM asset')

            # Wear the running saved copy through the normal inventory admission packet.
            body = viewer.credentials() + uuid.UUID(copied_item).bytes + uuid.UUID(owner).bytes
            body += struct.pack('<BIIII', 5, 0, 0, 0, 0) + b'\x00\x00'
            viewer.send(bytes.fromhex('ffff018b') + body)
            attachment = full_object(viewer, attached=True)
            check(attachment['identity'] != OBJECT, 'Inventory attach unexpectedly reused ground identity')
            def detach_check(part, item, previous_asset, label):
                first = full_object(viewer, part['identity'], part['local'], attached=True)['position']
                time.sleep(.3)
                last = full_object(viewer, part['identity'], part['local'], attached=True)['position']
                check(first == last, 'Attachment KFM changed its viewer offset')
                viewer.send(bytes.fromhex('ffff0071') + viewer.credentials() + b'\x01' + struct.pack('<I', part['local']))
                rewritten = until(lambda: (current if (current := inventory(root, owner).get(item)) != previous_asset
                                            and current else None), 'Detach did not rewrite inventory asset')
                check(asset_xml(rewritten, label).find('KeyframeMotion') is None,
                      'Detached inventory asset incorrectly retained deleted KFM')
                observations.append(dict(flow=label, item=item, offset=list(first),
                                         original_asset=previous_asset, detached_asset=rewritten))
            detach_check(attachment, copied_item, copied_asset, 'inventory-admission-detached')

            # Also attach the original running ground object through ObjectAttach.
            if args.select_ground_before_attach:
                # message_template.msg: ObjectSelect Low 110; one ObjectData entry.
                viewer.send(bytes.fromhex('ffff006e') + viewer.credentials() + b'\x01'
                            + struct.pack('<I', ground['local']))
                selection_sequence = viewer.sequence
                wait_for_ack(viewer, selection_sequence)
                first = full_object(viewer, OBJECT, ground['local'], acknowledge=True)['position']
                time.sleep(.3)
                second = full_object(viewer, OBJECT, ground['local'], acknowledge=True)['position']
                result['selected_ground_pause'] = dict(command_sequence=selection_sequence,
                                                      acknowledged=True, first=list(first), second=list(second))
                check(first == second, 'ObjectSelect did not pause running ground KFM before attachment')
            before = inventory(root, owner)
            viewer.send(bytes.fromhex('ffff0070') + viewer.credentials() + b'\x05\x01'
                        + struct.pack('<I3f', ground['local'], 0, 0, 0))
            full_object(viewer, OBJECT, ground['local'], attached=True)
            created = until(lambda: next(((item, asset) for item, asset in inventory(root, owner).items()
                                          if item not in before), None), 'Ground attach did not create inventory item')
            detach_check(ground, created[0], created[1], 'ground-attachment-detached')
        finally:
            process = server.process
            result['server_pid'] = process.pid if process is not None else None
            try:
                server.stop()
            finally:
                result['server_exit_code'] = process.poll() if process is not None else None
                if viewer is not None:
                    viewer.socket.close()
        result['status'] = 'passed'
        result['original_motion_sha256'] = hashlib.sha256(blob).hexdigest()
    except BaseException as error:
        result.update(status='failed', error=repr(error), traceback=traceback.format_exc())
        raise
    finally:
        if (root / 'original-motion.bin').is_file():
            result['original_motion_sha256'] = hashlib.sha256((root / 'original-motion.bin').read_bytes()).hexdigest()
        if (root / 'region.ini').is_file():
            result['config_sha256'] = hashlib.sha256((root / 'region.ini').read_bytes()).hexdigest()
        result['server_process_stopped'] = result.get('server_exit_code') is not None if result.get('server_pid') else True
        (root / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))



if __name__ == '__main__':
    main()
