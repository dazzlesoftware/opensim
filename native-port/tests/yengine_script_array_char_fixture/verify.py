"""Verify actual original character-array source behavior and migration without native execution."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(stage,key):return stage['globals'][key]['value']
def units(stage,key):return stage['globals'][key]['utf16']
def refs(stage):
 out={}
 def walk(value):
  if isinstance(value,dict):
   if value.get('type') in ('script_class','clr_array'):out[value['reference_id']]=value
   for v in value.values():walk(v)
  elif isinstance(value,list):
   for v in value:walk(v)
 walk(stage['globals']);return out

def class_value(stage,name='input'):
 v=stage['globals'][name]
 return refs(stage)[v['reference_id']] if v['type'].endswith('_reference') else v

def cells(stage,name='input'):
 v=class_value(stage,name)['objects'][0]
 if v['type']=='null':return None
 if v['type'].endswith('_reference'):v=refs(stage)[v['reference_id']]
 return [item['value'] for item in v['items']]

def failure(row,key):
 error=row[key]
 return None if error is None else dict(type=error['type'],message=error['message'])

def validate(row):
 name=row['name'];assert row['accepted'] and row['suspended'] and row['initial_exception'] is None
 surrogate=name=='char_utf16_wire_fault'
 assert row['migrated']==(not surrogate)
 if surrogate:
  e=row['production_write_exception'];assert e['type']=='System.ArgumentException' and e['message']=='Unicode surrogate characters must be written out as pairs together in the same call, not individually. Consider passing in a character array instead.'
  assert row['live_resume_exception'] is None and scalar(row['live_resume'],'after')==1
  assert units(row['initial'],'text')==units(row['live_resume'],'text')==[55357,56832,0,55296]
  assert cells(row['initial'])==cells(row['live_resume'])==[55357,56832,0,55296]
 else:
  assert row['production_write_exception'] is None and row['binary_load_exception'] is None and row['queue_restore_exception'] is None
 expected={
 'char_null_array_to_string':'System.NullReferenceException',
 'char_wrong_array_to_string':'System.InvalidCastException',
 'char_null_source_one':'System.NullReferenceException',
 'char_wrong_destination_zero':'System.InvalidCastException',
 'char_null_destination_zero':'System.NullReferenceException',
 'char_range_to_string':'System.ArgumentOutOfRangeException',
 'char_partial_copy_fault':'System.IndexOutOfRangeException'}
 for key in ('live_resume_exception','resume_exception'):
  wanted=expected.get(name)
  if name=='char_empty_to_string' and key=='resume_exception':wanted='System.ArgumentNullException'
  e=row[key]
  assert (None if e is None else e['type'])==wanted,(name,key,e)
  if wanted=='System.InvalidCastException':assert e['message']=="Unable to cast object of type 'System.Int32[]' to type 'System.Char[]'."
  if wanted=='System.NullReferenceException':assert e['message']=='Object reference not set to an instance of an object.'
  if wanted=='System.ArgumentNullException':assert e['message']=="Value cannot be null. (Parameter 'value')"
  if wanted=='System.IndexOutOfRangeException':assert e['message']=='Index was outside the bounds of the array.'
 if not surrogate:
  for stage,key in [('live_resume','live_resume_exception'),('final','resume_exception')]:
   assert scalar(row[stage],'after')==int(row[key] is None),(name,stage)
 if name=='char_members':
  for stage_name in ('initial','after_load','live_resume','final'):
   stage=row[stage_name]
   for key,wanted in [('default_cell',0),('length',4),('dimension',4),('invalid_dimension',0),('flat_index',2),('observed',233)]:assert scalar(stage,key)==wanted
   assert units(stage,'text')==[65,0,233,90] and units(stage,'type_name')==list(map(ord,'char[]'))
   assert cells(stage)==[65,0,233,90] and class_value(stage)['arrays_heap_use']==12
   assert stage['globals']['input']['reference_id']==stage['globals']['alias']['reference_id']
   backing=class_value(stage)['objects'][0];assert backing['reference_id']==stage['globals']['backing']['reference_id']
 elif name=='char_utf16_string_migration':
  assert scalar(row['initial'],'before_last')==55296 and cells(row['initial'])==[0]*4
  for stage,tail in [('live_resume',55296),('final',65533)]:
   assert scalar(row[stage],'after_last')==tail and units(row[stage],'copied')==[55357,56832,0,tail] and cells(row[stage])==[55357,56832,0,tail]
 elif name=='mixed_integer_char':
  for stage in ('live_resume','final'):
   assert scalar(row[stage],'result')==83 and cells(row[stage])==[66,0] and cells(row[stage],'numbers')==[0,17]
   assert class_value(row[stage])['class_index']!=class_value(row[stage],'numbers')['class_index']
 elif name=='char_partial_copy_fault':
  assert cells(row['live_resume'])==cells(row['final'])==[65,90,0]
 elif name in ('char_empty_to_string','char_empty_source_zero'):
  assert cells(row['initial'])==[] and cells(row['after_load']) is None
 for stage in ('initial','live_resume')+(() if surrogate else ('after_load','final')):
  o=row[stage];assert o['heap_used']==o['arrays_heap_used']+o['locals_heap_used']
  for value in refs(o).values():
   if value['type']!='script_class':continue
   assert value['rank']==1 and len(value['integers'])==1
   backing=value['objects'][0]
   if backing['type'].endswith('_reference'):backing=refs(o)[backing['reference_id']]
   own=4 if backing['type']=='null' else 4+backing['length']*(2 if value['element_type']=='char' else 4)
   assert value['arrays_heap_use']==own,(name,stage,own,value)

def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Actual original rank1 char[] members, UTF16 conversions, class/backing aliases, migration and bounded conversion faults',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==14;cases=[]
  for row in rows:
   validate(row);case=folder/row['name']
   for name,digest in row['sha256'].items():
    path=case/name
    if path.is_file():assert sha(path)==digest,(row['name'],name)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   code=D['ObjectReader']((case/'original.yobj').read_bytes()).read();types={t['index']:dict(name=n,element=t['element'],rank=t['rank'],sizes=t['sizes']) for n,t in code.types.items() if t['kind']==0}
   for index,t in types.items():assert t['rank']==1 and t['sizes']==[0,0,0,1,0,1,0,0,0,0,0]
   raw_summary=None
   if row['name']!='char_utf16_wire_fault':
    reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==(2 if row['name']=='mixed_integer_char' else 1)
    for value in reader.classes:
     t=types[value['class_index']];backing=value['instance_arrays'][5]['items'][0];assert backing['element_type']==('ch' if t['element']=='char' else 'in')
     expected_length=value['instance_arrays'][3]['items'][0];assert len(backing['items'])==expected_length
    if row['name']=='char_members':
     value=reader.classes[0];assert snapshot['globals'][5]['items'][0] is value['instance_arrays'][5]['items'][0]
     assert snapshot['globals'][9]['items'][0] is snapshot['globals'][9]['items'][1]
    raw_summary=dict(snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,classDefinitions=len(reader.classes))
   else:
    assert not (case/'snapshot.bin').exists() and (case/'production-partial.bin').stat().st_size>0
   if platform=='windows':baseline[row['name']]=row
   else:
    old=baseline[row['name']]
    for stage in ('initial','live_resume')+(() if row['name']=='char_utf16_wire_fault' else ('after_load','final')):assert row[stage]['globals']==old[stage]['globals'],(row['name'],stage)
    if raw_summary:assert (case/'snapshot.bin').read_bytes()==(ROOT/'windows'/row['name']/'snapshot.bin').read_bytes()
   cases.append(dict(name=row['name'],sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),originalObjectSha256=sha(case/'original.yobj'),sourceHash=code.source_hash,generatedTypes=types,snapshot=raw_summary,productionWriteFault=failure(row,'production_write_exception'),liveFault=failure(row,'live_resume_exception'),migratedFault=failure(row,'resume_exception'),heapByStage={stage:dict(globalUsage=row[stage]['arrays_heap_used'],localUsage=row[stage]['locals_heap_used'],total=row[stage]['heap_used']) for stage in ('initial','live_resume')},partialWriteSha256=sha(case/'production-partial.bin') if (case/'production-partial.bin').exists() else None))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 report['measuredBehavior']={'members':'char[] default0, Set/Get/index/Length(dim), NUL/BMP chars, wrapper/backing aliases survive live and external resume.', 'UTF16':'Conversions preserve raw surrogate code units and NUL live. BinaryWriter.Write(char) rejects surrogate elements; string migration replaces lone D800 with FFFD while paired surrogate survives.', 'empty':'Live empty char[] converts to empty string; externally normalized null backing makes xmrChars2String throw ArgumentNullException(value). Zero-count xmrString2Chars avoids source/backing access.', 'faults':'Wrong Int32[] backing fails cast even count0; null wrapper/null source count1 fail NullReference; out-of-range partial string copy retains already written A,Z cells.', 'mixed':'Source with integer[] and char[] has distinct generated class indices and resumes to result83.'}
 report['crossPlatformSuccessfulSnapshotsIdentical']=True
 report['boundaries']=['Fourteen bounded actual source cases per OS:13 complete migration snapshots plus1 observed production write fault; no native execution/parity claim.','No exhaustive array/conversion/rank/jagged/class-method parity claim.','Exact fault messages preserved per OS, including platform newline differences.','No forced GC or GC-cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS originalchar14cases perOS:',sha(out))
if __name__=='__main__':main()
