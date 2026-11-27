"""Verify unchanged original conversion range matrix independently of native helpers."""
from pathlib import Path
import hashlib,json,itertools,collections
root=Path(__file__).resolve().parent;repo=root.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
limits=[-2147483648,-1,0,1,2,3,2147483647]
backings=['normal','null_backing','wrong_int','null_wrapper'];sources=['null','empty','AZ','pair']
null_error=('System.NullReferenceException','Object reference not set to an instance of an object.')
index_error=('System.IndexOutOfRangeException','Index was outside the bounds of the array.')
cast_error=('System.InvalidCastException',"Unable to cast object of type 'System.Int32[]' to type 'System.Char[]'.")
def predict(row,newline):
    backing=row['backing'];chars=[65,90] if backing in ['normal','null_wrapper'] else None
    cells={'backingClr':None if backing=='null_backing' else 'System.Int32[]' if backing=='wrong_int' else 'System.Char[]','utf16':chars,'integers':[7,8] if backing=='wrong_int' else None}
    if backing=='null_wrapper':return null_error,None,cells
    if backing=='wrong_int':return cast_error,None,cells
    if row['operation']=='chars_to_string':
        start,count=row['start'],row['count']
        if backing=='null_backing':return ('System.ArgumentNullException',"Value cannot be null. (Parameter 'value')"),None,cells
        parameter='startIndex' if start<0 else 'length' if count<0 else None
        value=start if start<0 else count
        if parameter:
            return ('System.ArgumentOutOfRangeException',f"{parameter} ('{value}') must be a non-negative value. (Parameter '{parameter}')"+newline+f'Actual value was {value}.'),None,cells
        if start>2-count:
            return ('System.ArgumentOutOfRangeException',f"startIndex ('{start}') must be less than or equal to '{2-count}'. (Parameter 'startIndex')"+newline+f'Actual value was {start}.'),None,cells
        return None,chars[start:start+count],cells
    text=None if row['sourceName']=='null' else [] if row['sourceName']=='empty' else [65,90] if row['sourceName']=='AZ' else [55357,56832]
    for i in range(max(0,min(row['count'],3))):
        if text is None:return null_error,None,cells
        src=row['sourceStart']+i
        if not 0<=src<len(text):return index_error,None,cells
        if chars is None:return null_error,None,cells
        dst=row['destinationStart']+i
        if not 0<=dst<len(chars):return index_error,None,cells
        chars[dst]=text[src]
    assert row['count']<=2
    return None,None,cells
platforms={};baseline=None;backend={}
for platform,newline in [('windows','\r\n'),('linux','\n')]:
    folder=root/platform/'conversion_matrix';r=json.loads((folder/'metadata.json').read_text());rows=json.loads((folder/'range-matrix.json').read_text())
    assert r['accepted'] and r['suspended'] and r['migrated'] and r['runtime']=='.NET 8.0.34' and r['architecture']=='X64'
    assert len(rows)==5684
    expected={('chars_to_string',b,s,c) for b,s,c in itertools.product(backings,limits,limits)}|{('string_to_chars',b,s,ss,ds,c) for b,s,ss,ds,c in itertools.product(backings,sources,limits,limits,limits)}
    keys=set();canonical=[];faults=collections.Counter()
    for row in rows:
        key=(row['operation'],row['backing'],row['start'],row['count']) if row['operation']=='chars_to_string' else (row['operation'],row['backing'],row['sourceName'],row['sourceStart'],row['destinationStart'],row['count'])
        assert key in expected and key not in keys;keys.add(key)
        error,result,cells=predict(row,newline)
        actual=None if row['error'] is None else (row['error']['type'],row['error']['message'])
        assert actual==error,(key,actual,error)
        assert row['cells']==cells,(key,row['cells'],cells)
        if row['operation']=='chars_to_string':assert row['resultUtf16']==result
        if error:faults[error[0]]+=1
        canonical.append({**row,'error':None if error is None else {'type':error[0],'message':error[1].replace('\r\n','\n')}})
    assert keys==expected
    if baseline is None:baseline=canonical
    else:assert canonical==baseline
    hashes=r['sha256'];assert hashes['generator_source']==sha(root/'Reference.cs') and hashes['generator_assembly']==sha(root/'bin/Release/net8.0/Reference.dll')
    for key,file in [('original_yengine_assembly','OpenSim.Region.ScriptEngine.YEngine.dll'),('original_scene_assembly','OpenSim.Region.Framework.dll'),('original_plugins_assembly','OpenSim.Region.ScriptEngine.Shared.Api.dll')]:
        assert hashes[key]==sha(repo/'bin'/file)
        if key in backend:assert backend[key]==hashes[key]
        backend[key]=hashes[key]
    for file,digest in hashes.items():
        p=folder/file
        if p.is_file():assert sha(p)==digest
    platforms[platform]={'runtime':r['runtime'],'architecture':r['architecture'],'rows':len(rows),'matrixSha256':sha(folder/'range-matrix.json'),'metadataSha256':sha(folder/'metadata.json'),'sourceSha256':sha(folder/'source.lsl'),'objectSha256':sha(folder/'original.yobj'),'snapshotSha256':sha(folder/'snapshot.bin'),'faultCounts':dict(faults),'successes':sum(row['error'] is None for row in rows)}
proof={'schemaVersion':1,'passed':True,'nativeParityVerified':False,'fullParityVerified':False,'scope':'5684 actual unchanged original xmrChars2String/xmrString2Chars direct backend calls perOS on source-created char[] wrapper, with explicit backend backing/null injection; exact range/null/cast order, partial UTF16 writes, bounded at3loopiterations.','generatorSha256':sha(root/'Reference.cs'),'generatorAssemblySha256':sha(root/'bin/Release/net8.0/Reference.dll'),'verifierSha256':sha(Path(__file__)),'originalBackendAssemblySha256':backend,'originalSourceSha256':{f:sha(repo/'OpenSim/Region/ScriptEngine/YEngine'/f) for f in ['XMRInstAbstract.cs','XMRSDTypeClObj.cs']},'platforms':platforms,'findings':['Negative startIndex precedes negative length validation; knownCtor range messages include actualvalue and OS newline.','Null wrapper or wrong Int32[] backing checked even fornegative/zerocount; null backing CharsToString alwaysArgumentNull(value).','StringToChars count<=0 skips source/backing access afterdestinationcast; positive counts readsourcebeforewritingdestination and preservepartialUTF16writes.','No huge loops/allocations: normal source/destinationlength2 andanypositive count beyond2faults byiteration3.'],'pending':['No native helper parity claim in this oracle.','No arrayslongerthan2 or additional malformedtypes tested.','No macOS/ARM64 claim.']}
p=root/'verification.json'
if p.exists():assert json.loads(p.read_text())==proof
else:p.write_text(json.dumps(proof,indent=2,ensure_ascii=True)+'\n')
print('PASS original conversionrange11368calls;proofSHA256 '+sha(p))
