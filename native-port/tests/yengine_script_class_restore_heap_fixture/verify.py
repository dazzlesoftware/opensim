"""Validate unchanged original class restore heap timing without instrumentation."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
platforms={};baseline={};backend={}
for platform in ['windows','linux']:
    cases=[]
    for name,own in [('two_wrappers',16),('empty_wrappers',4)]:
        folder=root/(platform+'-fields')/name
        metadata=json.loads((folder/'metadata.json').read_text())
        assert metadata['accepted'] and metadata['suspended'] and metadata['migrated']
        assert metadata['runtime']=='.NET 8.0.34' and metadata['architecture']=='X64'
        hashes=metadata['sha256']
        assert hashes['generator_source']==sha(root/'Reference.cs')
        assert hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
        for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
            assert hashes[key]==sha(repo/'bin'/file)
            if key in backend:assert backend[key]==hashes[key]
            backend[key]=hashes[key]
        for file,digest in hashes.items():
            artifact=folder/file
            if artifact.is_file():assert sha(artifact)==digest
        rows=json.loads((folder/'restore-heap-observations.json').read_text());assert len(rows)==24
        canonical=[]
        for row in rows:
            assert row['preparationError'] is None
            mode=row['mode'];limit=row['limit'];before=row['before'];after=row['after'];error=row['error']
            assert before['globalUsage']==(0 if mode=='fresh_unallocated' else 4)
            if mode=='same_instance':assert [c['ownUsage'] for c in before['classes']]==[own,own]
            elif mode=='fresh_allocated':assert before['classes']==[None,None]
            else:assert before['classes'] is None
            clearing_failure=mode!='fresh_unallocated' and limit<4
            class_failure=not clearing_failure and limit<own
            if clearing_failure:
                assert error['message']==f'(OWNER)oldtotal=4, newtotal=4, limit={limit}'
                assert after['globalUsage']==4 and after['globalIntegers'] is None and after['classes'] is None
            elif class_failure:
                assert error['message']==f'(OWNER)oldtotal=0, newtotal={own}, limit={limit}'
                assert after['globalUsage']==0 and after['globalIntegers']==[0] and after['classes'] is None
            else:
                assert error is None and after['globalUsage']==4 and after['globalIntegers']==[0]
                assert len(after['classes'])==2
                for c in after['classes']:
                    assert c['ownUsage']==own and c['dimensions']==([3] if own==16 else [0])
                    assert c['objects']==([{'type':'System.Int32[]','length':3}] if own==16 else [{'type':None}])
            if error:assert error['type']=='OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException'
            assert after['localsUsage']==0 and after['heapUsed']==after['globalUsage']
            assert after['heapLeft']==limit-after['heapUsed']
            canonical.append({**row,'error':None if error is None else {k:error[k] for k in ['type','message']}})
        if platform=='windows':baseline[name]=canonical
        else:
            assert canonical==baseline[name]
            assert (folder/'snapshot.bin').read_bytes()==(root/'windows-fields'/name/'snapshot.bin').read_bytes()
        cases.append({'name':name,'observationsSha256':sha(folder/'restore-heap-observations.json'),'metadataSha256':sha(folder/'metadata.json'),'snapshotSha256':sha(folder/'snapshot.bin'),'sourceSha256':sha(folder/'source.lsl'),'restoreChecks':24})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','cases':cases}
proof={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Original actual production MigrateOutEventHandler snapshots and unchanged MigrateIn; two compiler-created integer[] wrappers, fresh allocated, same-instance reload and explicit fresh-unallocated helper controls;48 restore checks perOS.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRInstCapture.cs','XMRSDTypeClObj.cs','XMRHeapTracker.cs']},'platforms':platforms,'findings':['Own16 wrappers restore atlimit16,19,20,high in all3modes;20 is not required.','Allocated/same-instance lowlimit0/3 faults while clearing old global arrays:oldtotal4,newtotal4; global ledger remains4 but integer/class lanes become null.','Atlimit4/15, own16 class restore faults oldtotal0,newtotal16; global integer lane already[0], global ledger0,class lane stillnull.','Therefore nested class restore sees committed global ledger0 after successful clearing, before final global rebuilt4 commit; old own ledgers not aggregated.','Empty backing normalizesnull; own4 wrappers restore atlimit4 and above.','Failing inner class has not been installed in outer global class lane; unattached inner partial object not inspected.'],'pending':['No native original-import or native near-limit restore parity claim.','No macOS/ARM64 checks.']}
out=root/'verification.json'
if out.exists():assert json.loads(out.read_text())==proof
else:out.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original class restore heap:96 checks; proof SHA256 '+sha(out))
