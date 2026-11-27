"""Independent original Array2List matrix validation; no native implementation used."""
from pathlib import Path
import hashlib,json,struct,itertools,collections
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
starts=[-2147483648,-1,0,1,2,3,2147483647];counts=[-1,0,1,2,3]
modes=['source_backing','null_backing','wrong_string_backing','object_raw_string','object_raw_integer','object_raw_double','object_sdk_integer','object_sdk_float','object_sdk_string','object_sdk_key','object_vector','object_rotation','object_list','object_class','object_null','object_nested_integer','object_raw_char','object_boolean','object_single','object_prefix_null','null_wrapper','wrong_integer_wrapper']
sdk='OpenSim.Region.ScriptEngine.Shared.LSL_Types+'
invalid={'object_sdk_integer':'LSLInteger','object_sdk_float':'LSLFloat','object_sdk_string':'LSLString','object_sdk_key':'key','object_list':'list','object_class':'XMRSDTypeClObj','object_nested_integer':'Int32[]','object_raw_char':'Char','object_boolean':'Boolean','object_single':'Single'}
index_error=('System.IndexOutOfRangeException','Index was outside the bounds of the array.')
null_error=('System.NullReferenceException','Object reference not set to an instance of an object.')
def expected_error(row,family):
    mode,start,count=row['mode'],row['start'],row['count']
    if mode=='null_wrapper':return null_error
    if mode=='wrong_integer_wrapper':return ('System.InvalidCastException',"Unable to cast object of type 'System.Int32' to type 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'.")
    if mode=='wrong_string_backing':return ('System.InvalidCastException',"Unable to cast object of type 'System.String' to type 'System.Array'.")
    if count<0:return ('System.OverflowException','Arithmetic operation resulted in an overflow.')
    for i in range(count):
        if mode=='null_backing':return null_error
        index=start+i
        if not 0<=index<2:return index_error
        if mode=='object_null' or (mode in ['object_list','object_prefix_null'] and index==1):return ('System.NullReferenceException',f'null element {i}')
        bad='Char' if mode=='source_backing' and family=='char' else invalid.get(mode)
        if bad:return ('System.InvalidCastException',f'invalid element {i} type {bad}')
    return None
platforms={};baseline={};backend={}
for platform in ['windows','linux']:
    cases=[]
    for family in ['integer','float','char']:
        name=family+'_source';folder=root/platform/name;r=json.loads((folder/'metadata.json').read_text());rows=json.loads((folder/'array-to-list-matrix.json').read_text())
        assert r['accepted'] and r['suspended'] and r['migrated'] and r['runtime']=='.NET 8.0.34' and r['architecture']=='X64'
        assert all(r[k] is None for k in ['initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','live_resume_exception','resume_exception'])
        assert r['live_resume']['globals']['after']['value']==r['final']['globals']['after']['value']==1
        output=r['initial']['globals']['output'];assert output['type']=='list' and len(output['value'])==(0 if family=='char' else 2)
        assert len(rows)==770;keys=set();errors=collections.Counter();success=0
        for row in rows:
            mode,start,count=row['mode'],row['start'],row['count'];key=(mode,start,count)
            assert mode in modes and start in starts and count in counts and key not in keys;keys.add(key)
            error=expected_error(row,family);actual=None if row['error'] is None else (row['error']['type'],row['error']['message'])
            assert actual==error,(family,key,actual,error)
            if error:
                errors[error[0]]+=1
                assert all(row[k] is None for k in ['result','resultElementSourceAliases','resultPairSame','stringFieldSourceAliases'])
                continue
            success+=1;result=row['result'];assert result['type']=='list' and len(result['value'])==count
            retained=mode in ['object_vector','object_rotation'];strings=mode=='object_raw_string'
            assert row['resultElementSourceAliases']==[retained]*count
            assert row['stringFieldSourceAliases']==[strings]*count
            assert row['resultPairSame']==(retained if count==2 else None)
            for i,value in enumerate(result['value']):
                index=start+i
                if mode in ['object_raw_integer','object_prefix_null'] or (mode=='source_backing' and family=='integer'):
                    assert value['clr']==sdk+'LSLInteger' and value['value']==([7,-9][index] if mode!='object_prefix_null' else 7)
                elif mode=='object_raw_double' or (mode=='source_backing' and family=='float'):
                    expected=([1.2345678901234567,-2.25] if mode=='object_raw_double' else [11.5,2.25])[index]
                    assert value['clr']==sdk+'LSLFloat' and value['bits']==struct.pack('>d',expected).hex()
                elif strings:assert value['clr']==sdk+'LSLString' and value['utf16']==[114,97,119]
                elif retained:
                    dimensions=[1.,2.,3.] if mode=='object_vector' else [1.,2.,3.,4.]
                    assert value['clr']==sdk+('Vector3' if mode=='object_vector' else 'Quaternion')
                    assert [v['bits'] for v in value['value']]==[struct.pack('>d',v).hex() for v in dimensions]
                else:assert False,(key,value)
        assert keys==set(itertools.product(modes,starts,counts))
        hashes=r['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
        for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
            assert hashes[key]==sha(repo/'bin'/file)
            if key in backend:assert backend[key]==hashes[key]
            backend[key]=hashes[key]
        for file,digest in hashes.items():
            path=folder/file
            if path.is_file():assert sha(path)==digest
        if platform=='windows':baseline[family]=rows
        else:
            assert rows==baseline[family]
            for file in ['source.lsl','original.yobj','snapshot.bin']:assert (folder/file).read_bytes()==(root/'windows'/name/file).read_bytes()
        cases.append({'name':name,'matrixRows':len(rows),'successes':success,'faultCounts':dict(errors),'matrixSha256':sha(folder/'array-to-list-matrix.json'),'metadataSha256':sha(folder/'metadata.json'),'sourceSha256':sha(folder/'source.lsl'),'objectSha256':sha(folder/'original.yobj'),'snapshotSha256':sha(folder/'snapshot.bin')})
    platforms[platform]={'runtime':'.NET 8.0.34','architecture':'X64','sourceCases':3,'matrixRows':2310,'cases':cases}
proof={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Actual unchanged original xmrArray2List:3source-created primitive wrappers C/F/I and sourcehelpercalls plus22explicitbackendbacking/argumentmodes*7starts*5counts*3wrappers=2310directcalls/OS. FourphysicalrawfamiliesC/F/I/O only; no rawString[] backing invented.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRSDTypeClObj.cs','XMRHeapTracker.cs']},'platforms':platforms,'findings':['Raw int/double/string cells become new SDKInteger/Float/String boxes; existing SDKscalarboxes are rejected, includingSDKString/key.','SDKVector/Rotation boxed references preserved, including repeatedalias; newSDKString boxes distinct but internalCLRStringfields keep inputreferencealias.','Char/List/class/null/nestedarray/Boolean/Single cells reject exact indexedfaults; sourcechar[] onlycount0 conversion succeeds.','Wrapper/backingArraycast occursbefore negativecount allocationOverflow. Nullbackingcount0 returnsEmptyList regardlessstart; positivecountNullReference. Range checked perGetValue andnull-element index is relative outputslot.','Publicobj Object[] injection explicitlybypasses heap sizing forunsupported List/class/null children toisolatetheunchanged conversionhelper. No fabricatedscriptdescriptor or fictionalphysicalarrayfamily.'],'pending':['No native Array2List execution claim.','Discarded partialoutput fromfaultinghelper isunobservable andnotinspected; sourcehelperisreadonly.','No hugepositivecounts/arrayranks/multidimensionalarrays, macOS orARM64.']}
p=root/'verification.json'
if p.exists():assert json.loads(p.read_text())==proof
else:p.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original Array2List6sourcecaptures+4620matrixcalls;proofSHA256 '+sha(p))
