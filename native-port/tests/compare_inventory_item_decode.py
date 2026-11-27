"""Exact original BuildItem and original item model/SDK, including default constructor state."""
import argparse
import hashlib
import json
from pathlib import Path
import time

from compare_presence_endpoint import ROOT
from compare_inventory_folder_decode import build_reference, run


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work, item=True)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    uid = '11111111-1111-4111-8111-111111111111'
    item = dict(Name='Item & café', Description='A useful item', AssetID=uid, AssetType='6', InvType='6',
        ID=uid, Owner=uid, Folder=uid, CreatorId=uid, CreatorData='http://home.invalid/;Creator Name',
        NextPermissions='2147483648', CurrentPermissions='4294967295', BasePermissions='4294967295',
        EveryOnePermissions='0', GroupPermissions='7', GroupID=uid, GroupOwned='True',
        SalePrice='12', SaleType='2', Flags='4294967295', CreationDate='123')
    cases = [item, {}]
    for key in item:
        cases.append({name: value for name, value in item.items() if name != key})
        cases.append({**item, key: dict(nested='value')})
    for key in ['AssetType', 'InvType', 'SalePrice', 'CreationDate']:
        for value in ['', '+8', '-0', '  -8  ', '\t8\t', 'junk', '1.0', '-2147483649', '-2147483648',
                      '2147483647', '2147483648', '4294967295', '\u00a08\u00a0']:
            cases.append({**item, key: value})
    for key in ['NextPermissions', 'CurrentPermissions', 'BasePermissions', 'EveryOnePermissions', 'GroupPermissions', 'Flags']:
        for value in ['', '-1', '-0', '+8', '  8  ', 'junk', '2147483648', '4294967295', '4294967296']:
            cases.append({**item, key: value})
    for value in ['', '-1', '-0', '+8', '255', '256', '65535', 'junk']:
        cases.append({**item, 'SaleType': value})
    for value in ['', 'true', 'FALSE', '  True  ', '\u00a0False\u00a0', '1', 'junk']:
        cases.append({**item, 'GroupOwned': value})
    for key in ['ID', 'Owner', 'Folder', 'AssetID', 'GroupID']:
        for value in ['', 'junk', '11111111111141118111111111111111']:
            cases.append({**item, key: value})
    for value in ['', '  ', '\t', '\u00a0\u2003', '  actual text  ']:
        cases.append({**item, 'Description': value})
    cases += [{**item, 'CreatorId': ''}, {**item, 'CreatorId': 'arbitrary creator'}, {**item, 'CreatorData': ''}]
    started = int(time.time())
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--items'], cases)
    ended = int(time.time())
    differences = []
    defaults = 0
    for case, original_row, native_row in zip(cases, original, native):
        a, b = dict(original_row), dict(native_row)
        # The actual original default constructor uses UtcNow; the two
        # processes run sequentially, so validate its wall-clock window.
        if a['CreatorIdentification'] is None and a['ID'] == '00000000-0000-0000-0000-000000000000':
            defaults += 1
            assert started - 2 <= a['CreationDate'] <= ended + 2, a
            a['CreationDate'] = '<constructor UTC timestamp>'
            if started - 2 <= b['CreationDate'] <= ended + 2:
                b['CreationDate'] = '<constructor UTC timestamp>'
        if a != b:
            differences.append(dict(input=case, original=original_row, native=native_row))
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
        defaultConstructorTimestampObservations=defaults, timestampWindow=[started-2, ended+2],
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(), libraries=libraries,
        scope='Exact original BuildItem source and real InventoryItemBase/SDK; default constructor UTC time checked in measured window')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native item observations, including {defaults} constructor-time defaults')
