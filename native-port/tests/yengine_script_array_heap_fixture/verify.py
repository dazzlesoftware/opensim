"""Verify actual original generated-array heap observations without forcing GC."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scalar(stage,name):return stage['globals'][name]['value']
def validate(row):
 assert row['accepted'] and row['suspended'] and row['migrated']
 for name in ['initial_exception','production_write_exception','binary_load_exception','queue_restore_exception']:assert row[name] is None,(row['name'],name)
 for stage_name in ['initial','after_load','live_resume','final']:
  s=row[stage_name];assert s['heap_used']==s['arrays_heap_used']+s['locals_heap_used'];assert s['heap_left']==100000000-s['heap_used']
 n=row['name'];i=row['initial'];a=row['after_load'];l=row['live_resume'];f=row['final']
 if n=='heap_wrapper_alias':
  for k in ['h0','h1','h2','h3','h4']:assert scalar(i,k)==32
  assert i['globals']['input']['arrays_heap_use']==a['globals']['input']['arrays_heap_use']==16
  for s in [l,f]:assert scalar(s,'h5')==32 and scalar(s,'left')==99999968 and scalar(s,'after')==1 and s['globals']['input']['type']=='null'
 elif n=='heap_backing_replace_wrong':
  assert scalar(i,'h0')==scalar(i,'h1')==36
  for k in ['h2','h3','h4','h5']:assert scalar(i,k)==56
  for s in [i,a,l,f]:assert s['globals']['input']['arrays_heap_use']==14 and s['globals']['input']['objects'][0]['value']=='wrong'
  for s in [l,f]:assert scalar(s,'h6')==56 and scalar(s,'after')==1
  for k in ['live_resume_exception','resume_exception']:
   e=row[k];assert e['type']=='System.InvalidCastException' and e['message']=="Unable to cast object of type 'System.String' to type 'System.Int32[]'."
 elif n=='heap_local_backing_sleep':
  assert scalar(i,'h0')==scalar(i,'h1')==28 and scalar(i,'h2')==44
  assert i['heap_used']==44 and i['locals_heap_used']==16 and a['heap_used']==28 and a['locals_heap_used']==0
  assert scalar(l,'h3')==44 and scalar(l,'h4')==28 and scalar(l,'left')==99999972
  assert scalar(f,'h3')==28 and scalar(f,'h4')==12 and scalar(f,'left')==99999988
  for s in [i,a,l,f]:assert s['globals']['input']['arrays_heap_use']==20
  assert l['heap_used']==f['heap_used']==28 and scalar(l,'after')==scalar(f,'after')==1
 else:
  assert n=='heap_zero_backing'
  for k in ['h0','h1','h2']:assert scalar(i,k)==36
  assert i['globals']['input']['integers']==a['globals']['input']['integers']==[0]
  assert l['globals']['input']['integers']==f['globals']['input']['integers']==[99]
  for s in [i,a,l,f]:assert s['globals']['input']['arrays_heap_use']==4
  assert i['globals']['input']['objects'][0]['length']==0 and a['globals']['input']['objects'][0]['type']=='null'
  assert scalar(l,'after_backing')==1 and scalar(f,'after_backing')==0
  for s in [l,f]:
   for k in ['h3','h4','h5']:assert scalar(s,k)==36
 if n!='heap_backing_replace_wrong':assert row['resume_exception'] is None and row['live_resume_exception'] is None

def main():
 report={'schemaVersion':1,'passed':True,'fullParityVerified':False,'nativeParityVerified':False,'scope':'Actual unchanged original generatedinteger-array heap/global/local/instance ledgers, public backing mutations, live/migrated resume onWindows/Linuxx64','forcedGc':False,'gcCauseClaimed':False,'generatorSha256':sha(ROOT/'Reference.cs'),'generatorAssemblySha256':sha(ROOT/'bin/Release/net8.0/Reference.dll'),'platforms':{},'originalSourceSha256':{p:sha(REPO/p) for p in ['OpenSim/Region/ScriptEngine/YEngine/XMRInstAbstract.cs','OpenSim/Region/ScriptEngine/YEngine/XMRHeapTracker.cs','OpenSim/Region/ScriptEngine/YEngine/XMRSDTypeClObj.cs','OpenSim/Region/ScriptEngine/YEngine/MMRScriptReduce.cs']}}
 baseline={}
 for platform in ['windows','linux']:
  rows=json.loads((ROOT/platform/'metadata.json').read_text());assert len(rows)==4;cases=[]
  for row in rows:
   validate(row);folder=ROOT/platform/row['name']
   for name,digest in row['sha256'].items():
    p=folder/name
    if p.is_file():assert sha(p)==digest,(platform,row['name'],name)
   assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
   binary=(folder/'snapshot.bin').read_bytes()
   if platform=='windows':baseline[row['name']]=row
   else:
    old=baseline[row['name']];assert binary==(ROOT/'windows'/row['name']/'snapshot.bin').read_bytes()
    for stage in ['initial','after_load','live_resume','final']:
     for key in ['globals','heap_used','heap_left','arrays_heap_used','locals_heap_used']:assert row[stage][key]==old[stage][key],(row['name'],stage,key)
   cases.append({'name':row['name'],'sourceSha256':sha(folder/'source.lsl'),'snapshotSha256':sha(folder/'snapshot.bin'),'metadataSha256':sha(folder/'metadata.json'),'snapshotBytes':len(binary),'initialHeapUsed':row['initial']['heap_used'],'classOwnHeapUse':row['initial']['globals']['input']['arrays_heap_use'],'liveClearHeapObservation':row['live_resume']['globals'].get('h4',{}).get('value'),'migratedClearHeapObservation':row['final']['globals'].get('h4',{}).get('value')})
  report['platforms'][platform]={'metadataSha256':sha(ROOT/platform/'metadata.json'),'runtime':rows[0]['runtime'],'architecture':rows[0]['architecture'],'cases':cases}
 report['findings']=['Wrapper and instance storage are not added to xmrHeapUsed: it sums locals plus global arrays only; class instVars retain a separate arraysHeapUse.','Instance totals observed16/14/20/4bytes; global raw I[5] adds20bytes, wrapper aliases and len0 stores add0.','Local raw backing adds16locals live, external import resets locals0; clear yields live28 versus migrated12, normalreturn restores28.','Wrong public obj String persists then Get raises InvalidCastException.','Zero raw backing becomes null on external import while wrapper and own4byte dimension allocation remain.','No GC forced; clearing wrapper roots does not establish a GC lifecycle conclusion.']
 report['observerHistory']='Initial observer returned live dimension arrays; preserved pre-observer-copy outputs/source. Corrected helper copies dimension arrays with ToArray before recording; original backend unchanged.'
 report['verifierSha256']=sha(Path(__file__));out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n');print('PASS four original generated-array heap cases perOS:',sha(out))
if __name__=='__main__':main()
