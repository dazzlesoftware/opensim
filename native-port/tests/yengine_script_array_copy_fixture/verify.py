"""Verify actual original Array.Copy source behavior and full migration graphs."""
from pathlib import Path
import hashlib,json,runpy
ROOT=Path(__file__).resolve().parent
S=runpy.run_path(str(ROOT/'wire_support.py'))
Reader=S['Reader'];verify_wire=S['verify_wire'];defs=S['defs'];semantic=S['semantic'];canonical=S['canonical']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(stage,name):return stage['globals'][name]['value']
def resolved(stage,name):
 value=stage['globals'][name]
 return defs(stage)[value['reference_id']] if value['type'] in ('script_class_reference','array_reference') else value
def backing(stage,name):
 value=resolved(stage,name)['objects'][0]
 return defs(stage)[value['reference_id']] if value['type']=='array_reference' else value
def cells(stage,name):return [v.get('value') for v in backing(stage,name)['items']]
def validate(row):
 name=row['name'];assert row['accepted'] and row['suspended'] and row['migrated'] and not row['diagnostics']
 for key in ('compile_exception','initial_exception','production_write_exception','binary_load_exception','queue_restore_exception'):
  assert row[key] is None,(name,key,row[key])
 success={
  'copy_integer':[11,22,33],'copy_float':[1.2345678901234567,-2.5,0.125],'copy_char':[65,66,67],
  'copy_rectangular_rank':[11,22],'copy_zero_at_end':[0,0],
  'copy_widen_integer_double':[11.,-22.,33.],'copy_box_integer_object':[11,-22,33],
  'copy_unbox_object_integer':[11,-22,33],'copy_widen_character_integer':[65,66],
  'copy_sdk_integer_object_to_integer':[11],'copy_unbox_double_object_to_double':[1.25,2.5],
 }
 errors={
  'copy_cross_metadata':('System.ArrayTypeMismatchException','float[] vs integer[]'),
  'copy_jagged_depth_mismatch':('System.ArrayTypeMismatchException','integer[] vs integer[][]'),
  'copy_negative_source':('System.ArgumentOutOfRangeException',"sourceIndex ('-1') must be greater than or equal to '0'. (Parameter 'sourceIndex')\nActual value was -1."),
  'copy_negative_destination':('System.ArgumentOutOfRangeException',"destinationIndex ('-1') must be greater than or equal to '0'. (Parameter 'destinationIndex')\nActual value was -1."),
  'copy_source_overrun':('System.ArgumentException',"Source array was not long enough. Check the source index, length, and the array's lower bounds. (Parameter 'sourceArray')"),
  'copy_negative_count':('System.ArgumentOutOfRangeException',"length ('-1') must be a non-negative value. (Parameter 'length')\nActual value was -1."),
  'copy_null_wrapper':('System.NullReferenceException','Object reference not set to an instance of an object.'),
  'copy_wrong_backing':('System.InvalidCastException',"Unable to cast object of type 'System.String' to type 'System.Array'."),
  'copy_partial_unbox_bad':('System.InvalidCastException','At least one element in the source array could not be cast down to the destination array type.'),
  'copy_narrow_double_integer':('System.ArrayTypeMismatchException','Source array type cannot be assigned to destination array type.'),
  'copy_narrow_integer_character':('System.ArrayTypeMismatchException','Source array type cannot be assigned to destination array type.'),
  'copy_null_object_integer':('System.InvalidCastException','At least one element in the source array could not be cast down to the destination array type.'),
  'copy_boxed_integer_to_double':('System.InvalidCastException','At least one element in the source array could not be cast down to the destination array type.'),
 }
 for stage in ('live_resume','final'):
  state=row[stage]
  if name in success:assert cells(state,'destination')==success[name],(name,stage,cells(state,'destination'))
  elif name=='copy_overlap_forward':assert cells(state,'source')==[1,1,2,3] and resolved(state,'source') is resolved(state,'alias')
  elif name=='copy_overlap_backward':assert cells(state,'source')==[2,3,4,4] and resolved(state,'source') is resolved(state,'alias')
  elif name=='copy_argument_count_yield_rebind':
   assert cells(state,'source')==[99] and cells(state,'destination')==[100]
   assert cells(state,'source_alias')==cells(state,'destination_alias')==[11]
   assert resolved(state,'destination')['reference_id']!=resolved(state,'destination_alias')['reference_id']
  else:assert name in errors,name
  actual=row['live_resume_exception' if stage=='live_resume' else 'resume_exception'];expected=errors.get(name)
  if expected:
   assert actual and (actual['type'],actual['message'].replace('\r\n','\n'))==expected,(name,actual)
   assert scalar(state,'after')==0
  else:assert actual is None and scalar(state,'after')==1,(name,actual)
  if name=='copy_partial_unbox_bad':assert cells(state,'destination')==[11,102,103]
  if name=='copy_null_object_integer':assert cells(state,'destination')==[5]
 if name=='copy_argument_count_yield_rebind':
  for stage in ('initial','after_load','live_resume','final'):
   state=row[stage];assert scalar(state,'order')==12345
   for field in ('source_count','start_count','destination_count','offset_count','count_count'):assert scalar(state,field)==1
  assert cells(row['initial'],'destination_alias')==[0]
 if name=='copy_sdk_integer_object_to_integer':assert backing(row['initial'],'source')['items'][0]['clr']=='System.Int32'
 if name=='copy_partial_unbox_bad':
  assert resolved(row['initial'],'source')['arrays_heap_use']==4 and resolved(row['after_load'],'source')['arrays_heap_use']==22

def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,
  scope='Unchanged original xmrArrayCopy source execution over generated C/F/I wrappers and public mutated C/F/I/O backings, with actual production migration capture',
  generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),wireSupportSha256=sha(ROOT/'wire_support.py'),platforms={},freshProcessImports={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==27;cases=[]
  for row in rows:
   validate(row);case=folder/row['name']
   assert row['runtime']=='.NET 8.0.34' and row['architecture']=='X64'
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   for filename,digest in row['sha256'].items():
    path=case/filename
    if path.is_file():assert sha(path)==digest,(platform,row['name'],filename)
   parser=Reader((case/'snapshot.bin').read_bytes());snapshot=parser.read()
   refs=verify_wire((case/'source.lsl').read_text(),row['initial'],snapshot)
   if platform=='windows':baseline[row['name']]=row
   else:
    assert (case/'snapshot.bin').read_bytes()==(ROOT/'windows'/row['name']/'snapshot.bin').read_bytes()
    assert (case/'original.yobj').read_bytes()==(ROOT/'windows'/row['name']/'original.yobj').read_bytes()
    for stage in ('initial','after_binary_load','after_load','live_resume','final'):
     assert semantic(row[stage])==semantic(baseline[row['name']][stage]),(row['name'],stage)
   fault=row['resume_exception']
   cases.append(dict(name=row['name'],sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,originalObjectSha256=sha(case/'original.yobj'),afterLoadSha256=canonical(row['after_load']),finalSha256=canonical(row['final']),independentClassDefinitions=len(parser.classes),independentObservedReferences=refs,resumeFault=None if fault is None else dict(type=fault['type'],message=fault['message'])))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 for platform,folder in [('Windows','windows-imported'),('Linux','linux-imported-windows')]:
  rows=json.loads((ROOT/folder/'metadata.json').read_text());assert len(rows)==27
  for row in rows:
   assert row['accepted'] and row['imported_capture'] and not row['suspended'] and not row['migrated']
   assert semantic(row['initial'])==semantic(baseline[row['name']]['final'])
   error=row['initial_exception'];original=baseline[row['name']]['resume_exception']
   if original:assert error['type']==original['type'] and error['message'].replace('\r\n','\n')==original['message'].replace('\r\n','\n')
   else:assert error is None
  report['freshProcessImports'][platform]=dict(cases=27,processId=rows[0]['process_id'],metadataSha256=sha(ROOT/folder/'metadata.json'))
 report['measuredBehavior']={
  'families':'Actual raw C/F/I/O families retained; actual backing CLR arrays control conversions after generated-class basename/jagged-depth gate.',
  'overlap':'Same-array copies preserve memmove ordering both ways.',
  'typeGate':'float[] vs integer[] and integer[] vs integer[][] fail; different rectangular ranks with same base and jagged depth copy.',
  'conversion':'Int32[] toDouble[] andChar[] toInt32[] widen; Int32[] toObject[] boxes; Object rawInt32 toInt32[] andObject rawDouble toDouble[] unbox. Double[] toInt32[] andInt32[] toChar[] reject; Object rawInt32 toDouble[] cannot widen after unboxing.',
  'partialWrites':'Object[11,wrong,33] toInt32[101,102,103] writes11 then faults, leaving[11,102,103]. Null Object cannot unbox toInt32 and leavesold5.',
  'arguments':'Args once left-to-right order12345. Count Sleep retains earlier wrappers despite global rebinding99/100; old destination alias receives11.',
  'ledgers':'Mutable Object backing leaves class own ledger stale live; external restore recomputes cells, e.g4 to22 for bad Object source.',
  'ranges':'Negative index/count, source overrun, zero count at end, null wrapper, wrong String backing recorded with exact platform fault text.'}
 report['crossPlatformSnapshotsIdentical']=True;report['crossPlatformOriginalObjectsIdentical']=True
 report['boundaries']=['27 bounded source cases per original OS and27 fresh-process imports per OS; no native acceptance/parity claim.','Case label copy_sdk_integer_object_to_integer is historical wording: actual List indexing producesSystem.Int32 before Object storage; no SDK-LSLInteger Array.Copy acceptance claim.','ArgumentOutOfRange messages retainWindowsCRLF/LinuxLF; cross-platform comparisons normalize only newline differences.','No full CLR conversion matrix, new raw families, arbitrary generic bases, forced GC or GC-cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='utf-8')
 print('PASS original ArrayCopy27cases perOS +fresh imports27each; SHA256 '+sha(out))
if __name__=='__main__':main()
