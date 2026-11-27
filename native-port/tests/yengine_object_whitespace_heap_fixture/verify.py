"""Verify actual original whitespace Object/storage heap observations."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
scalar=lambda stage,n:stage['globals'][n]['value']
platforms={};baseline={};backend={}
for platform in ['windows','linux']:
    cases=[]
    for name in ['constructor_pressure','whitespace_values','whitespace_object_array']:
        folder=root/(platform+'-final')/name;r=json.loads((folder/'metadata.json').read_text())
        assert r['accepted'] and r['suspended'] and r['initial_exception'] is None and r['production_write_exception'] is None
        assert r['runtime']=='.NET 8.0.34' and r['architecture']=='X64'
        i=r['initial'];l=r['live_resume']
        if name=='constructor_pressure':
            assert not r['migrated'] and r['binary_load_exception']['type']=='OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException'
            assert r['binary_load_exception']['message']=='(OWNER)oldtotal=128, newtotal=148, limit=128'
            assert i['heap_used']==scalar(i,'during')==128 and scalar(i,'before')==16
            assert len(scalar(i,'pressure'))==56 and scalar(i,'pressure')==' '*56
            assert scalar(l,'result')==4 and scalar(l,'after')==1 and l['heap_used']==128
            capture=json.loads((folder/'retained-constructor-capture.json').read_text())
            assert capture['global_heap']==128 and capture['locals_heap']==0
            assert len(capture['retained'])==1 and capture['retained'][0]['own_heap_usage']==20
        elif name=='whitespace_values':
            assert r['migrated'] and r['binary_load_exception'] is None
            assert [scalar(i,n) for n in ['baseline','objectheap','stringheap','listheap']]==[20,26,26,40]
            assert r['after_load']['heap_used']==40 and scalar(l,'after')==scalar(r['final'],'after')==40
        else:
            assert r['migrated'] and r['binary_load_exception'] is None
            assert [scalar(i,n) for n in ['baseline','allocated','mutated','aliased']]==[20,20,20,26]
            assert i['heap_used']==26 and r['after_load']['heap_used']==32
            assert scalar(l,'after')==26 and scalar(r['final'],'after')==32
        hashes=r['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
        for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
            assert hashes[key]==sha(repo/'bin'/file)
            if key in backend:assert backend[key]==hashes[key]
            backend[key]=hashes[key]
        for file,digest in hashes.items():
            q=folder/file
            if q.is_file():assert sha(q)==digest
        stable={stage:{key:r[stage][key] for key in ['globals','heap_used','arrays_heap_used','locals_heap_used']} for stage in ['initial','live_resume','final']}
        if platform=='windows':baseline[name]=stable
        else:
            assert stable==baseline[name]
            for file in ['snapshot.bin','original.yobj','source.lsl']:assert (folder/file).read_bytes()==(root/'windows-final'/name/file).read_bytes()
        cases.append({'name':name,'sourceSha256':sha(folder/'source.lsl'),'snapshotSha256':sha(folder/'snapshot.bin'),'objectSha256':sha(folder/'original.yobj'),'metadataSha256':sha(folder/'metadata.json'),'externalMigrationAccepted':r['migrated'],'externalFault':None if r['binary_load_exception'] is None else {k:r['binary_load_exception'][k] for k in ['type','message']}})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','cases':cases}
proof={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Actual original source-generated Object/String/List/Object-array whitespace heap and retained class-frame restore pressure; no backend storage injection.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRHeapTracker.cs','XMRSDTypeClObj.cs']},'platforms':platforms,'findings':['Three whitespace UTF16units cost6bytes in Object,0 in typedString,and14includingonepointer inList.','Object[] child String whitespace costs6; mutation leaves globalledgerstale, assigningaliasadds6; externalrestore rebuilds32 versuslive26.','56spaces Object costs112 plus16integerglobalbytes=128; liveconstructor-retained own20 stillresumes, externalframeclassrestore rejects old128,new148,limit128.'],'observerHistory':'Initial non-pressure source used reserved identifier base; compile-rejected initial Windows output preserved. Final sources use baseline; three cases all compile bothOS.','pending':['Native whitespace source/import parity not exercised by this reference harness.','No macOS/ARM64 scope.']}
p=root/'verification.json'
if p.exists():assert json.loads(p.read_text())==proof
else:p.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS actual original whitespaceheap3cases/platform;proof SHA256 '+sha(p))
