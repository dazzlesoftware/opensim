"""Validate actual source-populated old global ClearOldArrays residuals."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
platforms={};baseline={};backend={}
for platform in ['windows','linux']:
    cases=[]
    for name in ['list_residual','stale_object_residual']:
        folder=root/(platform+'-final')/name;metadata=json.loads((folder/'metadata.json').read_text())
        assert metadata['accepted'] and metadata['suspended'] and metadata['migrated']
        assert metadata['runtime']=='.NET 8.0.34' and metadata['architecture']=='X64'
        rows=json.loads((folder/'restore-heap-observations.json').read_text());assert len(rows)==45
        canonical=[]
        for row in rows:
            assert row['preparationError'] is None
            mode=row['mode'];limit=row['limit'];before=row['before'];after=row['after'];error=row['error']
            list_case=name=='list_residual'
            if mode=='fresh_allocated':
                assert before['globalUsage']==4 and before['classes']==[None]
                old,clear,normal=4,0,14 if list_case else 10
            elif mode=='same_instance':
                old=14 if list_case else 4;clear=20 if list_case else -6;normal=14 if list_case else 10
                assert before['globalUsage']==old and before['classes'][0]['ownUsage']==16
            else:
                assert mode=='same_instance_repeat'
                old=14+20*(row['iteration']-1) if list_case else 4
                clear=20*row['iteration'] if list_case else -6
                normal=14 if list_case else 10
                assert before['globalUsage']==old
            new=clear+normal
            if old+clear>limit:
                expected_old,expected_new=old,old+clear
                assert after['globalUsage']==old and all(after[k] is None for k in ['globalIntegers','lists','objects','classes'])
            elif clear+16>limit:
                expected_old,expected_new=clear,clear+16
                assert after['globalUsage']==clear and after['globalIntegers']==[7] and after['classes'] is None
            elif clear+new>limit:
                expected_old,expected_new=clear,clear+new
                assert after['globalUsage']==clear and after['globalIntegers']==[7] and after['classes'][0]['ownUsage']==16
            else:
                expected_old=expected_new=None
                assert error is None and after['globalUsage']==new and after['classes'][0]['ownUsage']==16
            if expected_old is not None:
                assert error['type']=='OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException'
                assert error['message']==f'(OWNER)oldtotal={expected_old}, newtotal={expected_new}, limit={limit}'
            if after['globalIntegers'] is not None:
                if list_case:assert after['lists']==[{'size':10,'values':['x']}] and after['objects']==[]
                else:assert after['lists']==[] and after['objects']==[{'type':'System.Object[]','length':1,'size':6}]
            assert after['localsUsage']==0 and after['heapUsed']==after['globalUsage'] and after['heapLeft']==limit-after['heapUsed']
            canonical.append({**row,'error':None if error is None else {k:error[k] for k in ['type','message']}})
        hashes=metadata['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
        for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
            assert hashes[key]==sha(repo/'bin'/file)
            if key in backend:assert backend[key]==hashes[key]
            backend[key]=hashes[key]
        for file,digest in hashes.items():
            q=folder/file
            if q.is_file():assert sha(q)==digest
        if platform=='windows':baseline[name]=canonical
        else:
            assert canonical==baseline[name]
            for file in ['snapshot.bin','original.yobj','source.lsl']:assert (folder/file).read_bytes()==(root/'windows-final'/name/file).read_bytes()
        cases.append({'name':name,'sourceSha256':sha(folder/'source.lsl'),'objectSha256':sha(folder/'original.yobj'),'snapshotSha256':sha(folder/'snapshot.bin'),'metadataSha256':sha(folder/'metadata.json'),'observationsSha256':sha(folder/'restore-heap-observations.json'),'checks':45})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','cases':cases}
proof={'schemaVersion':1,'passed':True,'nativeParityVerified':False,'fullParityVerified':False,'scope':'Actual original source-populated List and stale Object[] old global ledgers across fresh allocated versus same-instance MigrateIn plus three repeated same-instance imports; no fabricated fields or backend instrumentation.180 restore checks Win/Linux x64.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRSDTypeClObj.cs','XMRHeapTracker.cs']},'platforms':platforms,'findings':['Source List[SDKString x] costs10 plusint4 =>oldglobal14. ClearOldArrays addsList.Size andsubtractsint4 =>residual20. Clear check14+20=34; classrestore20+own16=36; finalglobalcheck20+rebuilt34=54; accepted finalledger34.','List sameinstance repeat atgenerouslimit growsglobal14->34->54->74; classown remains16, List.Size10.','Source Object[] mutation leavesoldledger4 butcurrentObject.Size6; clearresidual-6. Sameinstance classrestore total10 requireslimit10; accepted finalledger4. Freshallocatedtarget requireslimit16 and rebuilds10.','Allpostfailure globalinteger/List/Object/classlanes observed: clearfail clearsalllanes butretainsoldledger; classfail leavesreconstructedint/List/Objectbutclasslanenull; finalcheckfailure classalreadyinstalledbutledgerstillresidual.'],'pending':['Native same-instance oldglobal residual parity not exercised.','Unattached failing inner class partialfields not inspected.','No macOS/ARM64 scope.']}
out=root/'verification.json'
if out.exists():assert json.loads(out.read_text())==proof
else:out.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original oldglobalclear180checks;proof SHA256 '+sha(out))
