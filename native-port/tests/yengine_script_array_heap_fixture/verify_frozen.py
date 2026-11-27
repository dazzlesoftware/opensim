"""Validate the promoted immutable final oracle without regenerating its proof."""
from pathlib import Path
import hashlib,json,importlib.util,sys
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parent
sha=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(root/'verification.json')=='00151d05d20e4b62d5772b8bbb035d663c791b760fe54ef6d15a733f566b6fec'
proof=json.loads((root/'verification.json').read_text())
assert sha(root/'Reference.cs')==proof['generatorSha256']
assert sha(root/'verify.py')==proof['verifierSha256']
assert sha(root/'bin/Release/net8.0/Reference.dll')==proof['generatorAssemblySha256']
spec=importlib.util.spec_from_file_location('original_heap_verifier',root/'verify.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
baseline={}
for platform in ['windows','linux']:
    base=root/platform
    expected=proof['platforms'][platform]
    assert sha(base/'metadata.json')==expected['metadataSha256']
    rows=json.loads((base/'metadata.json').read_text())
    assert len(rows)==4
    for row,case in zip(rows,expected['cases'],strict=True):
        module.validate(row)
        folder=base/row['name']
        assert row['name']==case['name']
        for name,key in [('source.lsl','sourceSha256'),('snapshot.bin','snapshotSha256'),('metadata.json','metadataSha256')]:
            assert sha(folder/name)==case[key]
        for name,digest in row['sha256'].items():
            path=folder/name
            if path.is_file(): assert sha(path)==digest
        assert row['runtime']=='.NET 8.0.34' and row['architecture']=='X64'
        if platform=='windows': baseline[row['name']]=row
        else:
            assert (folder/'snapshot.bin').read_bytes()==(root/'windows'/row['name']/'snapshot.bin').read_bytes()
            for stage in ['initial','after_load','live_resume','final']:
                for key in ['globals','heap_used','heap_left','arrays_heap_used','locals_heap_used']:
                    assert row[stage][key]==baseline[row['name']][stage][key]
print('PASS: final original generated-array heap oracle, four cases per Windows/Linux')
