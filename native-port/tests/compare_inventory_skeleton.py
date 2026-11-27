"""Exact original skeleton catch boundaries, partial results and response order."""
import argparse
import hashlib
import json
from pathlib import Path

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
    reference = args.reference or build_reference(args.dotnet, work, skeleton=True)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    folder = dict(Name='First & café', Type='8', Version='17',
        ID='11111111-1111-4111-8111-111111111111', Owner='22222222-2222-4222-8222-222222222222',
        ParentID='33333333-3333-4333-8333-333333333333')
    last = {**folder, 'Name': 'Last', 'ID': '44444444-4444-4444-8444-444444444444'}
    cases = [dict(reply=reply) for reply in [{}, {'RESULT': 'False'}, {'RESULT': 'True'},
        {'OTHER': 'value'}, {'FOLDERS': ''}, {'FOLDERS': 'not a dictionary'}, {'FOLDERS': {}},
        {'FOLDERS': {'z_first': folder, 'a_last': last}},
        {'RESULT': 'False', 'FOLDERS': {'first': folder}},
        {'RESULT': ' True ', 'FOLDERS': {'first': folder}}]]
    for scalar in ['', 'null', 'invalid']:
        for position in range(3):
            rows = [folder, last]
            rows.insert(position, scalar)
            cases.append(dict(reply={'FOLDERS': {f'row_{index}': row for index, row in enumerate(rows)}}))
    for invalid_folder in [{}, {**folder, 'Type': '65536'}, {**folder, 'ID': 'invalid'}]:
        cases.append(dict(reply={'FOLDERS': {'z_first': folder, 'a_default': invalid_folder, 'b_last': last}}))
    cases += [dict(reply=reply, transport_failure=True) for reply in [{}, {'RESULT': 'False'}, {'FOLDERS': {'first': folder}}]]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--skeleton'], cases)
    differences = [dict(input=case, original=a, native=b)
                   for case, a, b in zip(cases, original, native) if a != b]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(), libraries=libraries,
        scope='Exact GetInventorySkeleton, CheckReturn, BuildFolder and original model/SDK types; controlled MakeRequest failures')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native skeleton response and request-failure observations')
