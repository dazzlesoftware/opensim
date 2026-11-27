"""Independently verify original retained constructor frames and portable coordinates."""
import hashlib,json,runpy
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
D=runpy.run_path(str(ROOT/'decode.py'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(o,k):return o['globals'][k]['value']
def main():
 report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,scope='Actual original retained generated-array constructor argument across sleeping next argument, with bounded heap pressure',generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),decoderSha256=sha(ROOT/'decode.py'),platforms={})
 baseline={}
 for platform in ('windows','linux'):
  folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==2;cases=[]
  for row in rows:
   name=row['name'];pressure=name=='constructor_pressure';assert name in ('constructor_basic','constructor_pressure')
   assert row['accepted'] and row['suspended'] and row['initial_exception'] is None and row['production_write_exception'] is None and row['live_resume_exception'] is None
   assert row['migrated']==(not pressure)
   case=folder/name;capture=json.loads((case/'retained-constructor-capture.json').read_text());live=json.loads((case/'retained-constructor-live.json').read_text())
   assert len(capture['retained'])==1 and live['retained_count']==1 and live['live_input_is_captured_reference'] and live['resume_exception'] is None
   retained=capture['retained'][0];assert retained['function']=='default state_entry' and retained['slot']==1 and retained['call_no']==1 and retained['class_index']==0 and retained['dimensions']==[4] and retained['own_heap_usage']==20
   assert capture['global_heap']==live['global_heap']==(128 if pressure else 16) and capture['locals_heap']==live['locals_heap']==0
   for stage in ('initial','live_resume'):
    o=row[stage];assert scalar(o,'before')==16 and scalar(o,'during')==(128 if pressure else 16)
    assert scalar(o,'result')==(4 if stage=='live_resume' else 0) and scalar(o,'after')==int(stage=='live_resume')
   if pressure:
    fault=row['binary_load_exception'];assert fault['type']=='OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException' and fault['message']=='(OWNER)oldtotal=128, newtotal=148, limit=128'
    assert 'XMRSDTypeClObj.Restore' in fault['stack'] and 'XMRInstArrays.RecvArrays' in fault['stack']
   else:
    assert row['binary_load_exception'] is None and row['queue_restore_exception'] is None and row['resume_exception'] is None
    assert scalar(row['final'],'result')==4 and scalar(row['final'],'after')==1
   for field,digest in row['sha256'].items():
    path=case/field
    if path.is_file():assert sha(path)==digest,(name,field)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   reader=D['Reader']((case/'snapshot.bin').read_bytes());snapshot=reader.read();assert len(reader.classes)==1
   assert snapshot['globals'][9]['items']==[None]
   event=snapshot['frames'][0];assert event[0]=='default state_entry' and event[1]==1
   constructor=event[2]['items'][1];assert constructor is reader.classes[0] and constructor['class_index']==0
   assert constructor['instance_arrays'][3]['items']==[4] and constructor['instance_arrays'][5]['items'][0]['items']==[0]*4
   source=(case/'source.lsl').read_text();coordinates=D['constructor_coordinates'](case/'original.yobj',source)
   if platform=='windows':baseline[name]=dict(snapshot=(case/'snapshot.bin').read_bytes(),coordinates=coordinates)
   else:assert (case/'snapshot.bin').read_bytes()==baseline[name]['snapshot'] and coordinates==baseline[name]['coordinates']
   cases.append(dict(name=name,sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),captureSha256=sha(case/'retained-constructor-capture.json'),liveSha256=sha(case/'retained-constructor-live.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,originalObjectSha256=sha(case/'original.yobj'),constructorTemporary=coordinates,retainedCapture=retained,livePreservedReference=True,externalMigrationAccepted=not pressure,externalFault=row['binary_load_exception'] if pressure else None))
  report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],processId=rows[0]['process_id'],metadataSha256=sha(folder/'metadata.json'),originalBackendSha256={k:v for k,v in rows[0]['sha256'].items() if k.startswith('original_')},cases=cases)
 report['measuredBehavior']={'retainedArgument':'Original captures constructor wrapper as event frame slot1 while Pause sleeps; live continuation passes exact retained reference to Use.', 'coordinate':'Constructor call/result-store temporary uses zero-based line4/column67, the constructor opening parenthesis; new token is column54.', 'pressure':'At globalheap128/limit128, live resume passes but external restore of frame-only wrapper own20 fails newtotal148.', 'basic':'Without pressure, actual external capture/resume succeeds with result4/after1.'}
 report['crossPlatformSnapshotsIdentical']=True
 report['boundaries']=['No native runtime acceptance/parity claim.', 'Native ScriptArrayNew replay remains to be tested against these fixtures.', 'Only one frame-retained integer[] constructor argument and one pressure threshold measured; no general GC/capacity claim.']
 report['verifierSha256']=sha(Path(__file__))
 out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS constructor-temp2cases perOS:',sha(out))
if __name__=='__main__':main()
