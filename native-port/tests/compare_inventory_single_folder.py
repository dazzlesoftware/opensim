"""Original single-folder method exception boundaries and decoded results."""
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
    reference = args.reference or build_reference(args.dotnet, work, single=True)
    libraries = {}
    for name in ['OpenSim.Framework.dll', 'OpenMetaverseTypes.dll']:
        libraries[name] = hashlib.sha256((ROOT / 'bin' / name).read_bytes()).hexdigest()
        assert hashlib.sha256((reference.parent / name).read_bytes()).hexdigest() == libraries[name]
    folder = dict(Name='Folder & café', Type='8', Version='17',
        ID='11111111-1111-4111-8111-111111111111', Owner='22222222-2222-4222-8222-222222222222',
        ParentID='33333333-3333-4333-8333-333333333333')
    replies = [{}, {'RESULT': 'False'}, {'RESULT': 'True'}, {'other': 'value'},
        {'folder': ''}, {'folder': 'not a dictionary'}, {'folder': {}}, {'folder': folder},
        {'RESULT': 'False', 'folder': folder}, {'RESULT': ' True ', 'folder': folder},
        {'folder': {**folder, 'Type': '65536'}}, {'folder': {**folder, 'ID': 'invalid'}}]
    cases = [dict(method=method, reply=reply, transport_failure=failure)
        for method in ['GetRootFolder', 'GetFolderForType', 'GetFolder']
        for reply, failure in [(reply, False) for reply in replies] + [({}, True)]]
    original = run([args.dotnet, str(reference)], cases)
    native = run([args.native, '--single'], cases)
    differences = [dict(input=case, original=a, native=b)
                   for case, a, b in zip(cases, original, native) if a != b]
    source = ROOT / 'OpenSim/Services/Connectors/Inventory/XInventoryServicesConnector.cs'
    report = dict(passed=not differences, observations=len(cases), differences=differences,
        sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(), libraries=libraries,
        scope='Exact GetRootFolder, GetFolderForType, GetFolder, CheckReturn and BuildFolder; actual original model types; controlled MakeRequest')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native single-folder observations')
