"""GetMultipleItems request, result and real item-cache sequence comparisons."""
import argparse
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
    reference = args.reference or build_reference(args.dotnet, work, item=True, multiple_items=True)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    a, b, c, d = [f'{n:08d}-1111-4111-8111-111111111111' for n in range(1, 5)]
    zero = '00000000-0000-0000-0000-000000000000'
    owner = '22222222-2222-4222-8222-222222222222'
    def item(id, name):
        return dict(Name=name, Description='Item', AssetID=id, AssetType='6', InvType='6',
            ID=id, Owner=owner, Folder=owner, CreatorId=owner, NextPermissions='7', CurrentPermissions='7',
            BasePermissions='7', EveryOnePermissions='0', GroupPermissions='0', GroupID=owner,
            GroupOwned='False', SalePrice='0', SaleType='0', Flags='0', CreationDate='123')
    seeds = [[], [dict(id=a, name='Cached A')], [dict(id=a, null=True)],
        [dict(id=b, name='Cached B')], [dict(id=a, null=True), dict(id=b, name='Cached B')]]
    replies = [{}, {'RESULT': 'False'}, {'RESULT': 'True'}, {'item_z': item(a, 'Reply A')},
        {'item_z': item(b, 'Reply B'), 'item_a': item(a, 'Reply A')},
        {'item_z': item(a, 'Reply A'), 'item_scalar': 'null', 'item_last': item(d, 'Reply D')},
        {'item_0': item(a, 'Reply A'), 'item_1': item(b, 'Reply B'),
         'item_2': item(c, 'Reply C'), 'item_3': item(d, 'Reply D')},
        {'item_0': {}, 'item_1': item(d, 'Reply D')},
        {'ITEM_0': item(c, 'Ignored'), 'other': item(d, 'Ignored'), 'item_1': 'scalar'},
        {'RESULT': 'False', 'item_0': item(d, 'Ignored')}]
    # These format/combining characters are valid XML name characters, unlike
    # NUL or soft hyphen. .NET linguistic prefix matching can ignore them.
    for ignored in ['\u034f', '\u200c', '\u200d', '\ufe0f', '\ufeff']:
        replies.append({ignored + 'item_0': item(d, 'Linguistic prefix')})
        replies.append({'ite' + ignored + 'm_0': item(d, 'Linguistic prefix')})
    cases = []
    for seed in seeds:
        for ids in [[], [a], [b], [a, b], [b, a], [a, a, b], [c, a, b]]:
            for reply in replies:
                for failure in [False, True]:
                    cases.append(dict(cache=seed, steps=[
                        dict(ids=ids, reply=reply, failure=failure),
                        dict(ids=[d, a, b], reply={'RESULT': 'False'}),
                        dict(ids=[a, c], reply={}, failure=True)], inspect=[a, b, c, d, zero]))
    single_replies = [{}, {'RESULT': 'False'}, {'RESULT': 'True'}, {'item': 'scalar'},
        {'item': {}}, {'item': item(a, 'Single A')}, {'RESULT': 'False', 'item': item(a, 'Ignored')}]
    for seed in seeds:
        for reply in single_replies:
            for failure in [False, True]:
                for batch_reply in [{'RESULT': 'False'}, {'item_0': item(c, 'Batch C'), 'item_1': item(a, 'Batch A')}]:
                    cases.append(dict(cache=seed, steps=[
                        dict(method='single', ids=[a], reply=reply, failure=failure),
                        dict(ids=[a, c], reply=batch_reply),
                        dict(method='single', ids=[a], reply={'item': item(a, 'Recovered A')})],
                        inspect=[a, b, c, d, zero]))
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--multiple-items'], cases)
    differences = [dict(input=case, original=x, native=y)
                   for case, x, y in zip(cases, original, native) if x != y]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, scenarios=len(cases), observations=len(cases) * 3,
        differences=differences, libraries=libraries,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        scope='Exact GetItem/GetMultipleItems/CheckReturn/BuildItem, actual original DTO and ExpiringCacheOS assembly; native real cache. Seeded value/null entries, three sequential calls including single-to-batch transitions, request counts, Name/ID/Owner results and final cache hit/value inspected. XML-representable replies; no clock advancement, mutable reference or actual socket behavior compared.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native multiple-item cache scenarios ({len(cases) * 3} calls)')
