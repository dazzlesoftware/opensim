"""Verify unchanged original float-array execution, migration wire and imports."""
from pathlib import Path
import hashlib,json,re,runpy,struct
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
Reader=runpy.run_path(str(ROOT/'decode.py'))['Reader']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def canonical(value):return hashlib.sha256(json.dumps(value,sort_keys=True,ensure_ascii=True,separators=(',',':')).encode()).hexdigest()
def scalar(stage,name):return stage['globals'][name]['value']
def semantic(stage):return {key:stage[key] for key in ('globals','heap_used','heap_left','arrays_heap_used','locals_heap_used','object_reference_pairs','frames')}
def bits(value):return struct.pack('>d',value).hex()
def defs(stage):
 found={}
 def visit(value):
  if isinstance(value,dict):
   if value.get('type') in ('script_class','clr_array') and 'reference_id' in value:found[value['reference_id']]=value
   for child in value.values():visit(child)
  elif isinstance(value,list):
   for child in value:visit(child)
 visit(stage);return found

def verify_wire(source,stage,snapshot):
 nodes=defs(stage);mapped={};reverse={}
 def ref(raw,expected):
  key=expected['reference_id'];assert isinstance(raw,dict) and raw['kind'] in ('array','script_class')
  if key in mapped:assert mapped[key] is raw
  else:mapped[key]=raw
  if id(raw) in reverse:assert reverse[id(raw)]==key
  else:reverse[id(raw)]=key
 def value(raw,expected):
  kind=expected['type']
  if kind=='unsupported':assert raw=={'kind':'instance'};return
  if kind=='null':assert raw is None;return
  if kind=='integer':assert type(raw)==int and raw==expected['value'];return
  if kind=='float':assert isinstance(raw,float) and bits(raw)==expected['bits'];return
  if kind=='string':assert isinstance(raw,str) and list(raw.encode('utf-16-le','surrogatepass'))==list(bytes(sum(([x&255,x>>8] for x in expected['utf16']),[])));return
  if kind in ('script_class_reference','array_reference'):
   ref(raw,expected);return
  if kind=='script_class':
   ref(raw,expected);assert raw['kind']=='script_class' and raw['class_index']==expected['class_index']
   assert raw['instance_arrays'][3]['element_type']=='in' and raw['instance_arrays'][3]['items']==expected['integers']
   objects=raw['instance_arrays'][5];assert objects['element_type']=='ob' and len(objects['items'])==len(expected['objects'])
   for item,child in zip(objects['items'],expected['objects']):value(item,child)
   return
  if kind=='clr_array':
   ref(raw,expected);assert raw['element_type']=={'System.Double':'do','System.Int32':'in'}[expected['element_type']]
   assert len(raw['items'])==expected['length']==len(expected['items'])
   for item,child in zip(raw['items'],expected['items']):value(item,child)
   return
  raise AssertionError(('unreviewed metadata value',kind))
 # All bounded cases declare globals before their first function/default.
 prefix=re.split(r'\b(?:default\b|integer\s+Index\s*\(|float\s+Right\s*\(|float\[\]\s+Base\s*\()',source,maxsplit=1)[0]
 declarations=re.findall(r'\b(integer|float|object|string)(\[\])?\s+([A-Za-z_][A-Za-z_0-9]*)\s*;',prefix)
 assert len(declarations)==len(stage['globals'])
 consumed=[0]*11
 for primitive,array,name in declarations:
  lane=9 if array else {'integer':3,'float':2,'object':5,'string':7}[primitive]
  value(snapshot['globals'][lane]['items'][consumed[lane]],stage['globals'][name]);consumed[lane]+=1
 for lane,count in enumerate(consumed):assert len(snapshot['globals'][lane]['items'])==count
 assert len(snapshot['frames'])==len(stage['frames'])
 for (name,call,slots),frame in zip(snapshot['frames'],stage['frames']):
  assert name==frame['function'] and call==frame['call_no'] and slots['element_type']=='ob'
  assert len(slots['items'])==frame['slots']==len(frame['slot_values'])
  for raw,expected in zip(slots['items'],frame['slot_values']):value(raw,expected)
 for key,node in mapped.items():
  assert key in nodes
  # Reference-only observations may precede the corresponding definition.
  definition=nodes[key]
  if definition['type']=='script_class':assert node['class_index']==definition['class_index']
 return len(mapped)

def validate(row):
 name=row['name'];assert row['accepted'] and row['suspended'] and row['migrated'] and not row['diagnostics']
 for key in ('compile_exception','initial_exception','production_write_exception','binary_load_exception','queue_restore_exception'):assert row[key] is None,(name,key,row[key])
 for stage in ('initial','after_load','live_resume','final'):
  assert row[stage]['locals_heap_used']==0
 initial=row['initial'];live=row['live_resume'];final=row['final']
 if name=='float_members_precision':
  for s in (initial,live,final):
   assert scalar(s,'default_cell')==0 and scalar(s,'dimension')==3 and scalar(s,'invalid_dimension')==0 and scalar(s,'flat_index')==2
   assert scalar(s,'type_name')=='float[]' and scalar(s,'raw_type')=='System.Double[]'
   assert s['globals']['observed']['bits']==bits(1.2345678901234567-2.5)
  assert live['globals']['after']['bits']==final['globals']['after']['bits']==bits(1.2345678901234567)
 elif name=='float_public_dimensions_backing_alias':
  for s in (initial,live,final):assert scalar(s,'length')==1 and scalar(s,'outside_dimension')==12.125
  assert scalar(final,'after')==scalar(live,'after')==13.375
 elif name=='float_compound_rhs_yield_rebind':
  assert scalar(initial,'index_count')==0 and scalar(initial,'right_count')==1 and scalar(initial,'order')==2
  for s in (live,final):assert scalar(s,'index_count')==1 and scalar(s,'right_count')==1 and scalar(s,'order')==21 and scalar(s,'result_input')==107.375 and scalar(s,'result_alias')==11.5
 elif name=='float_receiver_assign_order':
  for s in (initial,live,final):assert scalar(s,'base_count')==scalar(s,'index_count')==scalar(s,'right_count')==1 and scalar(s,'order')==312 and scalar(s,'result_input')==scalar(s,'result_alias')==2.75
 elif name=='float_receiver_compound_base_yield':
  assert scalar(initial,'base_count')==scalar(initial,'right_count')==1 and scalar(initial,'index_count')==0 and scalar(initial,'order')==23
  for s in (live,final):assert scalar(s,'base_count')==scalar(s,'index_count')==scalar(s,'right_count')==1 and scalar(s,'order')==231 and scalar(s,'result_input')==107.375 and scalar(s,'result_alias')==11.5
 elif name=='float_mixed_class_cast':
  for s in (initial,live,final):
   assert s['globals']['integers']['class_index']==0 and s['globals']['input']['class_index']==1 and scalar(s,'same')==1
   assert scalar(s,'integer_type')=='integer[]' and scalar(s,'float_type')=='float[]'
  for k in ('live_resume_exception','resume_exception'):
   assert row[k]['type']=='System.InvalidCastException' and row[k]['message']=='invalid cast from float[] to integer[]'
  assert scalar(live,'after')==scalar(final,'after')==1
 elif name=='float_zero_backing':
  assert scalar(initial,'before_wrapper')==scalar(initial,'before_backing')==1 and scalar(initial,'length')==0
  assert scalar(live,'after_backing')==1 and scalar(final,'after_backing')==0 and scalar(live,'after_wrapper')==scalar(final,'after_wrapper')==1
 elif name=='float_index_postfix':
  for s in (initial,live,final):assert scalar(s,'cursor')==1 and scalar(s,'old')==11.5 and scalar(s,'result_input')==12.5 and scalar(s,'result_alias')==23.25
 else:raise AssertionError(name)
 if name!='float_mixed_class_cast':assert row['live_resume_exception'] is None and row['resume_exception'] is None

def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,
  scope='Unchanged original rank1 float[] source execution, production migration capture and fresh-process imports on Windows/Linux x64',
  generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={},freshProcessImports={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==8;cases=[]
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
    for stage in ('initial','after_binary_load','after_load','live_resume','final'):assert semantic(row[stage])==semantic(baseline[row['name']][stage]),(row['name'],stage)
   cases.append(dict(name=row['name'],sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,originalObjectSha256=sha(case/'original.yobj'),afterLoadSha256=canonical(row['after_load']),finalSha256=canonical(row['final']),independentClassDefinitions=len(parser.classes),independentObservedReferences=refs))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 for platform,folder in [('Windows','windows-imported'),('Linux','linux-imported-windows')]:
  rows=json.loads((ROOT/folder/'metadata.json').read_text());assert len(rows)==8
  for row in rows:
   assert row['accepted'] and row['imported_capture'] and not row['suspended'] and not row['migrated']
   assert semantic(row['initial'])==semantic(baseline[row['name']]['final'])
   error=row['initial_exception'];original=baseline[row['name']]['resume_exception']
   if original:assert error['type']==original['type'] and error['message']==original['message']
   else:assert error is None
  report['freshProcessImports'][platform]=dict(cases=8,processId=rows[0]['process_id'],metadataSha256=sha(ROOT/folder/'metadata.json'))
 report['measuredBehavior']={
  'storage':'Generated float[] wraps a distinct System.Double[] (do) backing, never Object[]; own heap=4+8*backingLength.',
  'precision':'Literal1.2345678901234567 stored/captured/restored as binary64 bits3ff3c0ca428c59fb; arithmetic and nonintegral cells preserved.',
  'dimensions':'Public len0 changes Length but does not constrain backing bounds; raw Double[] alias mutation visible through wrapper.',
  'evaluation':'Simple receiver assignment order312; RHS-yield compound order21; receiver-yield compound order231; postfix index increments once and11.5 returns/stores12.5.',
  'mixedClassTable':'Combined integer[] class index0 /float[] index1; matching cast aliases exact wrapper; cross-type cast fails invalid cast from float[] to integer[].',
  'zero':'Live empty Double[] stays nonnull; external migration normalizes backing null while wrapper survives.'}
 report['crossPlatformSnapshotsIdentical']=True;report['crossPlatformOriginalObjectsIdentical']=True
 report['boundaries']=['Eight bounded rank1 float[] source cases per original OS and eight fresh-process imports per OS; no native acceptance/parity claim.','No rectangular/jagged float arrays, NaN/Infinity, all conversions or arbitrary class-table layouts verified.','No forced GC or GC-cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
 print('PASS original float8cases perOS +fresh imports8each; SHA256 '+sha(out))
if __name__=='__main__':main()
