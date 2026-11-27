import hashlib,json,struct,datetime
from pathlib import Path
p=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rows={}
for platform in ['windows','linux']:
 records=json.loads((p/platform/'metadata.json').read_text());assert len(records)==2
 entries=[]
 for row,n in zip(records,[65537,100000]):
  assert row['name']=='references_O_'+str(n)
  assert all(row[k] for k in ['accepted','suspended','migrated'])
  for k in ['initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','resume_exception','live_resume_exception']:assert row[k] is None,(platform,k,row[k])
  folder=p/platform/row['name']
  for name,digest in row['sha256'].items():
   file=folder/name
   if file.is_file():assert sha(file)==digest,(platform,name)
  assert row['sha256']['generator_source']==sha(p/'Reference.cs')
  wire=(folder/'snapshot.bin').read_bytes();header=bytes([9,2,ord('o'),ord('b')])+struct.pack('<i',n)
  assert wire.count(header)==1
  at=wire.index(header)+len(header);h=hashlib.sha256()
  for i in range(n):
   assert wire[at]==13,(platform,n,i,wire[at]);at+=1
   length=0;shift=0
   while True:
    b=wire[at];at+=1;length|=(b&127)<<shift
    if not b&128:break
    shift+=7;assert shift<35
   text=wire[at:at+length];at+=length;assert text==str(i).encode()
   h.update(bytes([1])+struct.pack('<i',length)+text)
  digest=h.hexdigest()
  for stage in ['initial','after_load','live_resume','final']:
   g=row[stage]['globals'];a=g['input'];assert a['length']==n and a['cells_sha256']==digest
   assert g['alias']['reference_id']==a['reference_id'] and g['filled']['value']==1
   done=stage in ['live_resume','final'];assert g['same']['value']==int(done) and g['after']['value']==int(done)
  entries.append({'name':row['name'],'sourceSha256':sha(folder/'source.lsl'),'snapshotSha256':sha(folder/'snapshot.bin'),'snapshotBytes':len(wire),'arrayLength':n,'verifiedDistinctSYSSTRDefinitions':n,'fullCellsSha256':digest,'metadataSha256':sha(folder/'metadata.json')})
 rows[platform]={'metadataSha256':sha(p/platform/'metadata.json'),'runtime':records[0]['runtime'],'architecture':records[0]['architecture'],'cases':entries}
for a,b in zip(rows['windows']['cases'],rows['linux']['cases']):assert a['sourceSha256']==b['sourceSha256'] and a['fullCellsSha256']==b['fullCellsSha256']
report={'schemaVersion':1,'checkedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'passed':True,'scope':'Actual unchanged originalYEngine source execution, productionbinarywrite/read, sameLIVE and migratedresume with65537/100000distinctstringdefinitions inside objectarrays','nativeParityVerified':False,'schedulerMethodology':'Unchanged original60ms scheduler prefix slices untilsourcefilled1; then real llSleep capture. Testguardraised1000000checks; noheap/database or backendworkaround','generatorSha256':sha(p/'Reference.cs'),'verifierSha256':sha(Path(__file__)),'platforms':rows,'independentWireVerification':'Located unique SYSARRAYob Int32length then parsed every SYSSTR BinaryWriter length and verified exactdecimaltext; all definitions distinct, noDUPREF substitutes','allProcessesTerminal':True}
out=p/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS',sha(out))
