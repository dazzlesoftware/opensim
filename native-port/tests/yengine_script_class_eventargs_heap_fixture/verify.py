"""Verify unchanged original backend-injected event argument class heap thresholds."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Original MigrateIn event-argument class first-definition heap phase, backend-injected Object[] arguments',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==4;cases=[]
  for row in rows:
   case=folder/row['name'];empty=row['name'].startswith('empty');accepted='_pass' in row['name'];own=4 if empty else 16;limit=(8 if accepted else 4) if empty else (20 if accepted else 16)
   assert row['accepted'] and row['suspended'] and row['migrated']==accepted
   for key in ('initial_exception','production_write_exception','live_resume_exception'):assert row[key] is None,(row['name'],key)
   injection=json.loads((case/'argument-injection.json').read_text());entry=json.loads((case/'migrate-entry.json').read_text())
   assert injection['argument_is_global_class'] and injection['own_heap_usage']==own and injection['global_heap']==4 and injection['target_limit']==limit
   assert entry['old_global_heap']==4 and entry['target_limit']==limit and entry['new_instance']
   assert row['live_resume']['globals']['marker']['value']==1
   if accepted:
    assert row['binary_load_exception'] is None and row['queue_restore_exception'] is None and row['resume_exception'] is None and row['final']['globals']['marker']['value']==1
   else:
    error=row['binary_load_exception'];assert error['type']=='OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException'
    assert error['message']==f'(OWNER)oldtotal=4, newtotal={4+own}, limit={limit}'
   for name,digest in row['sha256'].items():
    path=case/name
    if path.is_file():assert sha(path)==digest,(row['name'],name)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==1
   args=snapshot['event_arguments'];assert args['element_type']=='ob' and len(args['items'])==1
   wrapper=args['items'][0];assert wrapper is reader.classes[0] and wrapper['reference']==1 and wrapper['class_index']==0
   assert snapshot['globals'][9]['items'][0] is wrapper
   assert wrapper['instance_arrays'][3]['items']==([0] if empty else [3])
   backing=wrapper['instance_arrays'][5]['items'][0];assert backing['element_type']=='in' and backing['items']==([] if empty else [0,0,0])
   if platform=='windows':baseline[row['name']]=(case/'snapshot.bin').read_bytes()
   else:assert (case/'snapshot.bin').read_bytes()==baseline[row['name']]
   cases.append(dict(name=row['name'],sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),injectionSha256=sha(case/'argument-injection.json'),migrationEntrySha256=sha(case/'migrate-entry.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,oldGlobalHeap=4,ownHeap=own,targetLimit=limit,externalMigrationAccepted=accepted,eventArgumentFirstDefinitionReference=1,globalUsesSameReference=True,fault=None if accepted else dict(type=row['binary_load_exception']['type'],message=row['binary_load_exception']['message'])))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 report['measuredBehavior']={'populated':'Old global ledger4 + class own16 requires20: limit16 rejects,20 accepts.','empty':'Old global ledger4 + class own4 requires8: limit4 rejects,8 accepts.','alias':'Class first defined in event arguments; global class lane is a DUPREF alias and does not rerun class receive.'}
 report['crossPlatformSnapshotsIdentical']=True
 report['boundaries']=['Event arguments injected directly into ehArgs Object[] after source state_entry suspended; source declares no event parameters.','Actual unchanged original MigrateOut/MigrateIn backend; no source event dispatch or native runtime parity claim.','No forced GC or GC cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS eventargs class heap4cases perOS:',sha(out))
if __name__=='__main__':main()
