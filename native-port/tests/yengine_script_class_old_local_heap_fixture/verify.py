"""Verify unchanged original local heap counters on fresh and already-paused MigrateIn targets."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(stage,key):return stage['globals'][key]['value']
def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Actual original local heap counters across fresh-allocated versus same-paused MigrateIn and normal handler return',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 charges={'local_object_string':6,'local_typed_string':6,'local_raw_array':12,'local_combined':24};baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==4;cases=[]
  for row in rows:
   name=row['name'];charge=charges[name];raw=name in ('local_raw_array','local_combined');case=folder/name
   assert row['accepted'] and row['suspended'] and row['migrated']
   for key in ('initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','live_resume_exception','resume_exception'):assert row[key] is None,(name,key)
   for key,digest in row['sha256'].items():
    path=case/key
    if path.is_file():assert sha(path)==digest,(name,key)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   observations=json.loads((case/'old-local-observations.json').read_text());assert len(observations)==2
   for observation in observations:
    same=observation['mode']=='same_paused';assert observation['mode'] in ('same_paused','fresh_allocated')
    for key in ('prepare_exception','load_exception','resume_exception'):assert observation[key] is None,(name,key)
    for stage_name in ('before','after','completed'):
     stage=observation[stage_name];assert stage['arrays_heap_used']==16 and stage['heap_used']==stage['arrays_heap_used']+stage['locals_heap_used']
     expected_local=charge if same and stage_name!='completed' else 0
     assert stage['locals_heap_used']==expected_local,(name,observation['mode'],stage_name)
    after=observation['after'];done=observation['completed']
    assert scalar(after,'before')==16+charge and scalar(after,'resumed')==scalar(after,'cleared')==scalar(after,'marker')==0
    assert scalar(done,'before')==16+charge and scalar(done,'resumed')==16+(charge if same else 0)
    assert scalar(done,'cleared')==(16 if same else 16-charge) and scalar(done,'marker')==1
    aliases=observation['after_aliases'];assert aliases['frame_class_aliases']==1 and aliases['frame_backing_aliases']==int(raw) and aliases['own_heap']==16
   reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==1
   wrapper=snapshot['globals'][9]['items'][0];assert wrapper is reader.classes[0]
   assert wrapper['class_index']==0 and wrapper['instance_arrays'][3]['items']==[3]
   backing=wrapper['instance_arrays'][5]['items'][0];assert backing['element_type']=='in' and backing['items']==[0,0,0]
   frame=snapshot['frames'][0];assert frame[0]=='default state_entry'
   assert sum(item is wrapper for item in frame[2]['items'])==1
   assert sum(item is backing for item in frame[2]['items'])==int(raw)
   code=D['ObjectReader']((case/'original.yobj').read_bytes()).read();locals_=code.methods['default state_entry']['locals']
   source_locals={v['name']:v['type'] for v in locals_.values() if not v['name'].startswith(('__','tmp$'))}
   assert source_locals['alias']=='sdtclobj'
   if raw:assert source_locals['raw']=='htobject'
   if name in ('local_object_string','local_combined'):assert source_locals['boxed']=='htobject'
   if name in ('local_typed_string','local_combined'):assert source_locals['text']=='htstring'
   if platform=='windows':baseline[name]=(case/'snapshot.bin').read_bytes(),observations
   else:
    assert (case/'snapshot.bin').read_bytes()==baseline[name][0]
    for i,observation in enumerate(observations):
     for stage in ('before','after','completed'):
      other=baseline[name][1][i][stage]
      for key in ('globals','heap_used','arrays_heap_used','locals_heap_used'):assert observation[stage][key]==other[key],(name,stage,key)
   cases.append(dict(name=name,charge=charge,sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),observationsSha256=sha(case/'old-local-observations.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,originalObjectSha256=sha(case/'original.yobj'),sourceHash=code.source_hash,sourceLocalStorage=source_locals,observations=[dict(mode=o['mode'],oldLocal=o['before']['locals_heap_used'],afterMigrateLocal=o['after']['locals_heap_used'],afterReturnLocal=o['completed']['locals_heap_used'],resumedHeap=scalar(o['completed'],'resumed'),clearedHeap=scalar(o['completed'],'cleared'),afterReturnHeap=o['completed']['heap_used'],classAliases=o['after_aliases']['frame_class_aliases'],backingAliases=o['after_aliases']['frame_backing_aliases']) for o in observations]))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 report['measuredBehavior']={'migrationBoundary':'MigrateIn preserves target preexisting m_localsHeapUsed: fresh0, same-paused6/6/12/24.', 'restoredTrackerCharges':'Local tracker Restore sets saved payload size without incrementing m_localsHeapUsed; clearing subtracts6/6/12/24.', 'sourceCounters':'Global16; fresh resumed16 and cleared10/10/4/-8, same-paused resumed22/22/28/40 and cleared16.', 'normalReturn':'Saved entry heap base0 restores local counter0 for both modes; heapUsed returns16.', 'aliases':'Restored source local class aliases global class; raw local aliases class backing in raw and combined cases.'}
 report['crossPlatformSnapshotsIdentical']=True
 report['boundaries']=['Four bounded source cases per OS, two direct MigrateIn target modes each, generous heap limit100000000.','No native execution/parity or local-counter near-limit admission claim.','No failed partial-state or forced-GC claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS old-local4cases2modes perOS:',sha(out))
if __name__=='__main__':main()
