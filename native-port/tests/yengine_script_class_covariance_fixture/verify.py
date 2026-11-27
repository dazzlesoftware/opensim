"""Validate backend-injected covariance using unchanged original production codecs."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
platforms={};baseline={};backend={}
expected={'string_container':('System.String[]',12,[False,False]),'class_container':('OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj[]',4,[True,True]),'list_container':('OpenSim.Region.ScriptEngine.Shared.LSL_Types+list[]',16,[False,False])}
for platform in ['windows','linux']:
    cases=[]
    for name,(clr,heap,aliases) in expected.items():
        folder=root/platform/name;row=json.loads((folder/'metadata.json').read_text())
        injection=json.loads((folder/'injection-observations.json').read_text());restored=json.loads((folder/'restored-container.json').read_text())
        assert row['accepted'] and row['suspended'] and row['migrated']
        assert row['runtime']=='.NET 8.0.34' and row['architecture']=='X64'
        for key in ['initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','resume_exception','live_resume_exception']:assert row[key] is None
        assert injection['containerClr']==restored['containerClr']==clr
        assert injection['inputAliases']==restored['inputAliases']==aliases
        assert restored['globalUsage']==heap and restored['ownUsage']==16
        error=injection['primitiveControlError']
        assert error['type']=='System.InvalidCastException'
        assert error['message']=="Unable to cast object of type 'System.Int32[]' to type 'System.Object[]'."
        assert injection['primitiveLanesRead']==6 and injection['primitiveGlobalUsage']==0
        assert row['initial']['arrays_heap_used']==4 and row['after_load']['arrays_heap_used']==heap
        for stage in ['live_resume','final']:assert row[stage]['globals']['after']['value']==31
        hashes=row['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
        for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
            assert hashes[key]==sha(repo/'bin'/file)
            if key in backend:assert backend[key]==hashes[key]
            backend[key]=hashes[key]
        for file,digest in hashes.items():
            path=folder/file
            if path.is_file():assert sha(path)==digest
        stable={stage:{key:row[stage][key] for key in ['globals','heap_used','arrays_heap_used','locals_heap_used']} for stage in ['initial','after_load','live_resume','final']}
        if platform=='windows':baseline[name]=stable
        else:
            assert stable==baseline[name]
            for file in ['snapshot.bin','original.yobj','source.lsl']:assert (folder/file).read_bytes()==(root/'windows'/name/file).read_bytes()
        cases.append({'name':name,'containerClr':clr,'globalHeapAfterLoad':heap,'inputAliases':aliases,'sourceSha256':sha(folder/'source.lsl'),'objectSha256':sha(folder/'original.yobj'),'snapshotSha256':sha(folder/'snapshot.bin'),'metadataSha256':sha(folder/'metadata.json'),'injectionSha256':sha(folder/'injection-observations.json'),'restoredContainerSha256':sha(folder/'restored-container.json')})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','cases':cases}
proof={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Backend-injected covariant global iarObjects containers in actual source-compiled paused instances; unchanged original MigrateOutEventHandler and MigrateIn; not source-generated container-type behavior.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRSDTypeClObj.cs','XMRHeapTracker.cs']},'platforms':platforms,'findings':['Nonempty String[], XMRSDTypeClObj[], and LSL list[] containers survive actual original global Object-lane migration with exact CLR container type.','Class container entries preserve aliases to the separate global input wrapper.','String/list null entries remain null. Global heap rebuilds4->12 forString,textlength4;4->16 forlist(oneSDKinteger19);classaliasesremain4.','Direct field injection intentionally leaves the pre-capture heap ledger stale4; import rebuilds usage.','Primitive Int32[] replacement through unchanged RecvArrays rejects System.Int32[] toSystem.Object[] cast after6lanesread; control repeated per covariance case.'],'pending':['Native covariance activation tests not run by this oracle.','No macOS/ARM64 or arbitrary reference-array covariance claim.']}
p=root/'verification.json'
if p.exists():assert json.loads(p.read_text())==proof
else:p.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original covariance3cases/platform+primitivecontrol;proof SHA256 '+sha(p))
