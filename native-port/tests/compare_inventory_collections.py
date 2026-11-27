"""Exact original single/batch collection methods and real original DTO types."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from compare_inventory_folder_decode import build_reference, run
from compare_presence_endpoint import ROOT


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference or build_reference(args.dotnet, work, collections=True)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    first = '11111111-1111-4111-8111-111111111111'
    owner = '22222222-2222-4222-8222-222222222222'
    last = '33333333-3333-4333-8333-333333333333'
    folder = dict(Name='Folder & café', Type='8', Version='17', ID=first, Owner=owner, ParentID=last)
    item = dict(Name='Item & café', Description='Item', AssetID=first, AssetType='6', InvType='6',
        ID=first, Owner=owner, Folder=last, CreatorId=owner, NextPermissions='7', CurrentPermissions='7',
        BasePermissions='7', EveryOnePermissions='0', GroupPermissions='0', GroupID=owner,
        GroupOwned='False', SalePrice='0', SaleType='0', Flags='0', CreationDate='123')
    rows = dict(FOLDERS={'z_first': folder, 'a_last': {**folder, 'Name': 'Last'}},
                ITEMS={'z_first': item, 'a_last': {**item, 'Name': 'Last'}})
    single = [{}, {'RESULT': 'False'}, {'RESULT': 'True'}, rows,
        {'RESULT': 'False', **rows}, {'RESULT': ' True ', **rows}]
    for key in ['FOLDERS', 'ITEMS']:
        for value in ['', 'scalar', {}, {'default': {}}]:
            single.append({**rows, key: value})
        single.append({k: v for k, v in rows.items() if k != key})
        for position in range(3):
            entries = list(rows[key].values())
            entries.insert(position, 'scalar')
            single.append({**rows, key: {f'row_{i}': value for i, value in enumerate(entries)}})
    cases = [dict(method='single', ids=[first], reply=reply) for reply in single]
    collection = dict(FID=first, OWNER=owner, VERSION='17', **rows)
    def batch(value):
        return {'F_' + first: value, 'F_' + last: {**collection, 'FID': last}}
    replies = [{}, {'RESULT': 'False'}, {'RESULT': 'True'}, batch(collection),
        {'F_' + last: collection}, batch('scalar'), batch({}),
        {'RESULT': 'False', **batch(collection)}]
    for key in ['FID', 'OWNER', 'VERSION', 'FOLDERS', 'ITEMS']:
        replies.append(batch({k: v for k, v in collection.items() if k != key}))
        for value in ['', 'invalid', {}, {'nested': 'value'}]:
            replies.append(batch({**collection, key: value}))
    for version in ['-2147483649', '-2147483648', '-1', '-0', '+0', '0', '+17', ' 17 ', '\t17\r',
                    '2147483647', '2147483648', '4294967295', '+-0', '-+0', '\u00a017\u00a0']:
        replies.append(batch({**collection, 'VERSION': version}))
    for key in ['FOLDERS', 'ITEMS']:
        for position in range(3):
            value = copy.deepcopy(collection)
            entries = list(rows[key].values())
            entries.insert(position, 'scalar')
            value[key] = {f'row_{i}': row for i, row in enumerate(entries)}
            replies.append(batch(value))
            replies.append({'F_' + first: collection, 'F_' + last: value})
    cases += [dict(method='batch', ids=ids, reply=reply)
              for ids in [[first, last], [last, first], [first, first], []] for reply in replies]
    cases += [dict(method=method, ids=[first, last], reply=rows, transport_failure=True)
              for method in ['single', 'batch']]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--collections'], cases)
    differences = [dict(input=case, original=a, native=b)
                   for case, a, b in zip(cases, original, native) if a != b]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(), libraries=libraries,
        scope='Exact original GetFolderContent/GetMultipleFoldersContent/CheckReturn/BuildFolder/BuildItem; real DTOs; controlled MakeRequest. XML-representable scalar/dictionary replies only. Collection nulls, slots, metadata, order, full folder rows and item Name/ID/Owner projected; other item DTO fields have separate decoding comparisons.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native inventory collection observations')
