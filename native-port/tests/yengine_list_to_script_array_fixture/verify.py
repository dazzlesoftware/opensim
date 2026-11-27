"""Independently verify original xmrList2Array source paths and bounded direct CLR SetValue matrix."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def error(value):return None if value is None else dict(type=value['type'],message=value['message'])
def scalar(stage,name):return stage['globals'][name]['value']
def backing(stage):
 value=stage['globals']['input'];return value['objects'][0]
def values(array):return [v.get('utf16',v.get('value')) for v in array['items']]
def equal_elements(actual,expected):
 assert len(actual)==len(expected)
 for a,b in zip(actual,expected):assert a==b,(actual,expected)
def validate_source(row):
 assert row['accepted'] and row['suspended'] and row['migrated']
 for key in ('initial_exception','production_write_exception','binary_load_exception','queue_restore_exception'):assert row[key] is None,(row['name'],key)
 name=row['name'];wanted=None
 if name=='source_char_narrow_fault':wanted=('System.ArgumentException','Cannot widen from source type to target type either because the source type is a not a primitive type or the conversion cannot be accomplished.')
 if name=='source_integer_partial_cast':wanted=('System.InvalidCastException','Object cannot be stored in an array of this type.')
 if name=='source_integer_partial_source':wanted=('System.IndexOutOfRangeException','Index was outside the bounds of the array.')
 for key in ('live_resume_exception','resume_exception'):
  actual=row[key];assert (None if actual is None else (actual['type'],actual['message']))==wanted,(name,key)
 for stage_name in ('initial','after_load','live_resume','final'):
  stage=row[stage_name];resumed=stage_name in ('live_resume','final')
  assert scalar(stage,'after')==int(resumed and wanted is None)
  value=stage['globals']['input'];arr=backing(stage)
  assert stage['globals']['backing']['reference_id']==arr['reference_id']
  expected_own=4 if arr['element_type']=='System.Object' else 4+3*({'System.Char':2,'System.Double':8,'System.Int32':4}[arr['element_type']])
  assert value['arrays_heap_use']==expected_own
  if resumed and name=='source_integer_success':assert values(arr)==[0,11,22]
  elif resumed and name=='source_float_widen':assert values(arr)==[11,2.5,0]
  elif resumed and name=='source_object_unwrap':
   assert values(arr)==[1,2.5,[120]] and [x['clr'] for x in arr['items']]==['System.Int32','System.Double','System.String']
  elif resumed and name in ('source_integer_partial_cast','source_integer_partial_source'):assert values(arr)==[11,0,0]
  else:assert values(arr)==([None]*3 if arr['element_type']=='System.Object' else [0]*3)

def validate_matrix(rows,family):
 assert len(rows)==33
 accepted={'C':{'null','raw_char','sdk_null_string'},'I':{'null','raw_char','raw_int','sdk_int','sdk_null_string'},'F':{'null','raw_char','raw_int','raw_single','raw_double','sdk_int','sdk_float','sdk_null_string'}}
 numeric={'raw_char','raw_int','raw_single','raw_double','raw_bool','sdk_int','sdk_float'}
 payloads=['null','raw_char','raw_int','raw_single','raw_double','raw_bool','raw_string','sdk_int','sdk_float','sdk_string','sdk_null_string','sdk_key','vector','rotation','list','class']
 range_errors={'partial_source','partial_destination','negative_source','negative_destination','null_data_one','null_backing_bad_source'}
 null_errors={'null_list_zero','null_backing_one','wrong_backing_null_list','null_wrapper_zero'}
 cast_errors={'wrong_backing_zero','raw_array_wrapper_zero','raw_string_wrapper_zero'}
 for row in rows:
  name=row['name'];assert row['family']==family and row['own_before']==row['own_after'] and row['global_before']==row['global_after']
  fault=error(row['error'])
  if name in payloads:
   success=family=='O' or name in accepted[family]
   assert (fault is None)==success,(family,name,fault)
   if not success:
    assert fault['type']==('System.ArgumentException' if name in numeric else 'System.InvalidCastException')
    assert fault['message']==('Cannot widen from source type to target type either because the source type is a not a primitive type or the conversion cannot be accomplished.' if name in numeric else 'Object cannot be stored in an array of this type.')
   assert row['before']['length']==row['after']['length']==3
   if family!='O':
    expected=0 if name in ('null','sdk_null_string') or not success else {'raw_char':65,'raw_int':7,'raw_single':1.5,'raw_double':2.5,'sdk_int':9,'sdk_float':3.5}[name]
    assert values(row['after'])==[0,expected,0]
   else:
    item=row['after']['items'][1]
    expected_clr={'sdk_int':'System.Int32','sdk_float':'System.Double','sdk_string':'System.String','sdk_null_string':None}.get(name,row['source']['clr'])
    assert item['clr']==expected_clr,(name,item)
    assert row['object_destination_stored_is_source']==(not name.startswith('sdk_') or name=='sdk_key')
    if name=='sdk_string':assert row['sdk_string_stored_is_field']
    if name=='sdk_key':assert item['type']=='key'
    if name=='class':assert item['type']=='script_class' and item['objects'][0]['type']=='array_reference' and item['objects'][0]['reference_id']==row['after']['reference_id']
   continue
  if name in range_errors:assert fault==dict(type='System.IndexOutOfRangeException',message='Index was outside the bounds of the array.')
  elif name in null_errors:assert fault==dict(type='System.NullReferenceException',message='Object reference not set to an instance of an object.')
  elif name in cast_errors:
   assert fault['type']=='System.InvalidCastException'
   target='System.Array' if name=='wrong_backing_zero' else 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'
   actual='System.String' if name in ('wrong_backing_zero','raw_string_wrapper_zero') else row['before']['clr']
   assert fault['message']==f"Unable to cast object of type '{actual}' to type '{target}'."
  else:assert fault is None,(family,name,fault)
  if name in ('null_data_zero','null_data_one'):assert row['source_data_null_before'] is True and row['source_data_null_after'] is False
  if row['after']['type']=='clr_array':
   baseline=None if family=='O' else 0;valid=65 if family=='C' else 7
   expected=[baseline]*3
   if name=='partial_source':expected[0]=valid
   if name=='partial_destination':expected[2]=valid
   assert values(row['after'])==expected,(family,name,values(row['after']),expected)
 return [dict(name=r['name'],sourceClr=None if r['source'] is None else r['source']['clr'],storedClr=(r['after']['items'][1]['clr'] if r['after']['type']=='clr_array' and r['name'] in payloads else None),error=error(r['error']),ownBefore=r['own_before'],ownAfter=r['own_after'],globalBefore=r['global_before'],globalAfter=r['global_after']) for r in rows]

def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Actual original xmrList2Array source entry points and direct CLR/SDK element SetValue/partial-mutation matrix',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 baseline={};matrices={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==10;cases=[];direct=0
  for row in rows:
   validate_source(row);name=row['name'];case=folder/name
   for key,digest in row['sha256'].items():
    path=case/key
    if path.is_file():assert sha(path)==digest,(name,key)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   code=D['ObjectReader']((case/'original.yobj').read_bytes()).read();reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==1
   wrapper=snapshot['globals'][9]['items'][0];assert wrapper is reader.classes[0]
   assert wrapper['instance_arrays'][3]['items']==[3]
   raw=wrapper['instance_arrays'][5]['items'][0];assert snapshot['globals'][5]['items'][0] is raw
   observation=None
   if name.startswith('matrix_'):
    family=name[-1];observations=json.loads((case/'direct-helper-observations.json').read_text());summary=validate_matrix(observations,family);direct+=len(observations)
    observation=dict(sha256=sha(case/'direct-helper-observations.json'),cases=len(observations),summary=summary)
    if platform=='windows':matrices[family]=observations
    else:
     for a,b in zip(observations,matrices[family]):
      for key in a:
       if key=='error':assert error(a[key])==error(b[key])
       else:assert a[key]==b[key],(family,a['name'],key)
   if platform=='windows':baseline[name]=row
   else:
    assert (case/'snapshot.bin').read_bytes()==(ROOT/'windows'/name/'snapshot.bin').read_bytes()
    for stage in ('initial','after_load','live_resume','final'):assert row[stage]['globals']==baseline[name][stage]['globals']
   cases.append(dict(name=name,sourceHelperExecuted=name.startswith('source_'),sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),originalObjectSha256=sha(case/'original.yobj'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,sourceHash=code.source_hash,directHelper=observation,liveFault=error(row['live_resume_exception']),migratedFault=error(row['resume_exception'])))
  assert direct==132
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),sourceCaptureCases=10,sourceListToArrayCases=6,directHelperCases=direct,cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 report['measuredBehavior']={'unwrap':'Only LSLInteger,LSLFloat,LSLString unwrap; SDK key stays its key struct. SDK null String unwraps null.', 'primitiveNull':'Null becomes primitive element default0.', 'numericWidening':'C accepts Char; I accepts Char/Int32; F accepts Char/Int32/Single/Double. Unsupported numeric conversions throw ArgumentException; nonprimitive incompatible values throw InvalidCastException.', 'objectBacking':'Public obj Object[] accepts every tested CLR/SDK payload; raw boxes/references retained, only three SDK scalars unwrap. Class value can preserve class/backing self-cycle.', 'faultOrdering':'Wrapper validation precedes source List.Data; source Data access precedes backing Array cast; source index is evaluated before destination SetValue/null access. Nonpositive count still performs pre-loop validation.', 'partialMutation':'Previously copied elements survive subsequent source bounds/destination bounds/type failures.', 'heap':'Mutating existing backing through this helper leaves cached global/class-own ledgers unchanged, including Object[] cells whose current sizes change.'}
 report['crossPlatformSnapshotsIdentical']=True
 report['boundaries']=['Ten actual source captures per OS:four matrix setup wrappers andsix source xmrList2Array calls;132 direct helper observations per OS.','Direct CLR/SDK boxed payload matrix is backend evidence, not a source-language representation/boxing claim.','No native execution/parity claim or exhaustive CLR primitive/rank/jagged/type matrix claim.','Combined invalid element and invalid destination-index priority remains unmeasured.','No forced GC or GC-cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS original List2Array10captures+132directchecks perOS:',sha(out))
if __name__=='__main__':main()
