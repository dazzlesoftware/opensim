"""Validate independently loaded foreign same-index casts and static accessors."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
number=lambda s,n:s['globals'][n]['value']
platforms={};baseline={};backend={}
operations=['cast_length','method_get','method_set','index_get','index_set','compound']
for platform in ['windows','linux']:
    cases=[]
    for family,clr,own in [('float','System.Double[]',12),('char','System.Char[]',6)]:
        for operation in operations:
            name=family+'_'+operation;folder=root/platform/name
            r=json.loads((folder/'metadata.json').read_text());injection=json.loads((folder/'foreign-injection.json').read_text())
            assert r['accepted'] and r['suspended'] and r['migrated'] and r['runtime']=='.NET 8.0.34' and r['architecture']=='X64'
            assert all(r[k] is None for k in ['initial_exception','production_write_exception','binary_load_exception','queue_restore_exception'])
            assert injection['foreignClassIndex']==0 and injection['foreignType']==family+'[]' and injection['foreignBackingType']==clr
            assert injection['ownerDifferent'] and injection['callerCodeDifferent'] and injection['injectedAlias']
            for stage,script_type in [('initial',family+'[]'),('after_load','integer[]'),('live_resume',family+'[]'),('final','integer[]')]:
                value=r[stage]['globals']['foreign'];assert value['type']=='script_class' and value['script_type']==script_type
                assert value['class_index']==0 and value['arrays_heap_use']==own and value['integers']==[1]
                backing=value['objects'][0];assert backing['clr']==clr and backing['length']==1
                if family=='float':assert backing['items'][0]['bits']=='4027000000000000'
                else:assert backing['items'][0]['value']==65
            for stage,kind in [('live_resume',family+'[]'),('final','integer[]')]:
                state=r[stage];assert number(state,'stage')==1 and number(state,'kind')==kind
                assert state['globals']['input']['type']=='script_class_reference'
                assert state['globals']['input']['reference_id']==state['globals']['foreign']['reference_id']
                assert number(state,'rhs')==(1 if operation=='compound' else 0)
                if operation=='cast_length':assert number(state,'after')==number(state,'result')==1
                else:assert number(state,'after')==number(state,'result')==0
            for key in ['live_resume_exception','resume_exception']:
                error=r[key]
                if operation=='cast_length':assert error is None
                else:
                    assert error['type']=='System.InvalidCastException'
                    assert error['message']==f"Unable to cast object of type '{clr}' to type 'System.Int32[]'."
            il=(folder/'original.il.txt').read_text()
            if operation in ['method_get','index_get','compound']:assert 'integer[].Get(integer)' in il
            if operation in ['method_set','index_set','compound']:assert 'integer[].Set(integer,integer)' in il
            hashes=r['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
            for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
                assert hashes[key]==sha(repo/'bin'/file)
                if key in backend:assert backend[key]==hashes[key]
                backend[key]=hashes[key]
            for file,digest in hashes.items():
                path=folder/file
                if path.is_file():assert sha(path)==digest
            stable={stage:{key:r[stage][key] for key in ['globals','heap_used','arrays_heap_used','locals_heap_used']} for stage in ['initial','after_load','live_resume','final']}
            if platform=='windows':baseline[name]=stable
            else:
                assert stable==baseline[name]
                for file in ['snapshot.bin','original.yobj','source.lsl','foreign-source.lsl','foreign-original.yobj']:
                    assert (folder/file).read_bytes()==(root/'windows'/name/file).read_bytes()
            cases.append({'name':name,'sourceSha256':sha(folder/'source.lsl'),'objectSha256':sha(folder/'original.yobj'),'snapshotSha256':sha(folder/'snapshot.bin'),'metadataSha256':sha(folder/'metadata.json'),'foreignSourceSha256':sha(folder/'foreign-source.lsl'),'foreignObjectSha256':sha(folder/'foreign-original.yobj'),'injectionSha256':sha(folder/'foreign-injection.json'),'liveCastAccepted':True,'accessorFault':None if operation=='cast_length' else f"Unable to cast object of type '{clr}' to type 'System.Int32[]'."})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','cases':cases}
proof={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Actual original independently compiled/loaded caller integer[] index0 and foreign float[]/char[] index0; source-created wrappers injected through unchanged PopObject before captured caller cast; compiled static method/index/compound accessors.24cases Win/Linux.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRSDTypeClObj.cs','MMRScriptCompValu.cs','MMRScriptReduce.cs','XMRInstAbstract.cs']},'platforms':platforms,'findings':['Same numeric classindex0 cast succeeds despite foreign float[]/char[] logical name; wrapper aliases retained; Length returns1 without backing-family cast.','Live dynamic typename remainsforeignfloat[]/char[]. Caller statically bound integer[].Get/Set/indexGet/indexSet/compound all throw exact raw Double[]/Char[] toInt32[] cast faults; compound RHS runs once first.','Actual original caller MigrateIn resolves raw classindex0 in caller objectcode, changing wrapper logicaltype tointeger[] but preserving raw Double[]/Char[] backing and ownheap12/6; same statically bound accessorfaults follow.','No backing writes occur before any failing accessor; original cells remain11.5 binary64 orchar65.'],'pending':['Native foreign livecast/staticaccessor checks not run by this reference harness.','Cross-instance reference injection is a backend test boundary, not ordinary source transfer acceptance.','No arbitrary classindex layouts, ranks, delegates, macOS orARM64 scope.']}
p=root/'verification.json'
if p.exists():assert json.loads(p.read_text())==proof
else:p.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original foreign-element24cases;proof SHA256 '+sha(p))
