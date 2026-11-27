"""Validate original character conversion argument order and retained result identity."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(stage,name):return stage['globals'][name]['value']
def units(stage,name):return stage['globals'][name]['utf16']
def cells(stage):return [v['value'] for v in stage['globals']['input']['objects'][0]['items']]
def temp_coordinates(code,source):
 method=code.methods['default state_entry'];records=method['records'];out=[]
 for i,r in enumerate(records):
  if r.get('strings',[])[:2]!=['xmrChars2String','xmrinst']:continue
  store=next(x for x in records[i+1:] if 'op' in x)
  assert store['kind']==9 and store['op']==0xfe0e
  local=method['locals'][store['number']];assert local['type']=='string'
  assert store['location']==r['location']
  line=source.splitlines()[r['location'][1]];assert line[r['location'][2]]=='('
  out.append(dict(localNumber=store['number'],localName=local['name'],type=local['type'],portableLocation=r['location']))
 assert len(out)==2
 return out[0]
def validate(row):
 assert row['accepted'] and row['suspended'] and row['migrated']
 for key in ('initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','live_resume_exception','resume_exception'):assert row[key] is None,(row['name'],key)
 name=row['name'];retained=name=='chars_result_before_pause'
 for stage in ('initial','after_load','live_resume','final'):
  o=row[stage];resumed=stage in ('live_resume','final')
  if retained:
   assert scalar(o,'c1')==1 and cells(o)==[90]
   assert scalar(o,'after')==int(resumed) and scalar(o,'same_string')==int(resumed)
   assert scalar(o,'literal_equal')==int(stage=='final')
   assert units(o,'text')==([65] if resumed else [])
   if resumed:assert units(o,'received')==[65] and units(o,'fresh')==[90]
  else:
   to_chars=name.startswith('string_to_chars');count=5 if to_chars else 3
   assert scalar(o,'order')==(12345 if to_chars else 123)
   for i in range(1,6):assert scalar(o,'c'+str(i))==int(i<=count)
   assert scalar(o,'after')==int(resumed)
   pending=name.endswith('_yield') and not resumed
   assert units(o,'text')==([] if pending else [65])
   assert cells(o)==([0] if to_chars and pending else [65])

def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Original character conversion argument order, argument yield and retained String result before later mutation/Sleep',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==5;cases=[]
  for row in rows:
   validate(row);name=row['name'];case=folder/name
   for key,digest in row['sha256'].items():
    path=case/key
    if path.is_file():assert sha(path)==digest,(name,key)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   code=D['ObjectReader']((case/'original.yobj').read_bytes()).read()
   reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==1
   wrapper=snapshot['globals'][9]['items'][0];assert wrapper is reader.classes[0]
   assert wrapper['instance_arrays'][3]['items']==[1] and wrapper['instance_arrays'][5]['items'][0]['element_type']=='ch'
   retained=None
   if name=='chars_result_before_pause':
    captures=json.loads((case/'retained-strings-capture.json').read_text());live=json.loads((case/'retained-strings-live.json').read_text())
    assert len(captures)==1 and captures[0]['function']=='default state_entry' and captures[0]['call_no']==1 and captures[0]['slot']==1 and captures[0]['utf16']==[65]
    assert live['received_is_retained'] and not live['received_is_literal'] and live['received_content_hash_matches_retained']
    frame=snapshot['frames'][0];assert frame[0]=='default state_entry' and frame[1]==1
    retained_value=frame[2]['items'][1];literal=snapshot['globals'][5]['items'][2]
    assert retained_value=='A' and isinstance(retained_value,D['ManagedString']) and retained_value is literal
    coord=temp_coordinates(code,(case/'source.lsl').read_text());assert coord['portableLocation'][1:]==[4,97]
    retained=dict(captureSha256=sha(case/'retained-strings-capture.json'),liveSha256=sha(case/'retained-strings-live.json'),frameFunction=frame[0],slot=1,utf16=[65],resultStore=coord,wireStringAliasesLiteral=True,liveReferencePreserved=True,liveLiteralEqual=False,migratedLiteralEqual=True)
   if platform=='windows':baseline[name]=row
   else:
    assert (case/'snapshot.bin').read_bytes()==(ROOT/'windows'/name/'snapshot.bin').read_bytes()
    for stage in ('initial','after_load','live_resume','final'):assert row[stage]['globals']==baseline[name][stage]['globals']
   cases.append(dict(name=name,sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),originalObjectSha256=sha(case/'original.yobj'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,sourceHash=code.source_hash,retainedResult=retained))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),cases=cases,originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')})
 report['measuredBehavior']={'Chars2StringArguments':'Source(),Start(),Count() evaluate once left-to-right:123; Count Sleep retains prior arguments without replay.', 'String2CharsArguments':'Source(),Start(),Destination(),Offset(),Count() evaluate once left-to-right:12345; Count Sleep occurs before mutation and resumes one copy.', 'retainedResult':'Chars2String resultA survives later Pause mutating backing toZ; actual live received reference is the captured frame StringA, while fresh conversion returnsZ.', 'identity':'Live converted StringA differs from equal literalA; external dictionary merges them, so migrated received==literal becomes1; same_string remains1.', 'coordinate':'Original retained String result-store uses opening parenthesis, zero-based line4/column97.'}
 report['crossPlatformSnapshotsIdentical']=True
 report['boundaries']=['Five bounded actual source cases per original Windows/Linux runtime; no native execution/parity claim.', 'String content hashes compared only for equality within one process, never numeric across processes.', 'Independent raw String decoder avoids Python single-character interning when asserting wire identity.', 'No forced GC or GC-cause claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS char conversion eval5cases perOS:',sha(out))
if __name__=='__main__':main()
