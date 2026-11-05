"""Actual original Initialise + bundled Nini bool/alias/default/order closure."""
import argparse,hashlib,itertools,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def encode(s):return '~' if s is None else s.encode('utf-16-be','surrogatepass').hex() or '-'
def ini(export=None,syntax=None):return '[SimulatorFeatures]\n'+('' if export is None else 'ExportSupported='+export+'\n')+('' if syntax is None else 'ScriptSyntax='+syntax+'\n')
def invoke(cmd,rows):
 r=subprocess.run(cmd,input='\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert not r.returncode,(r.returncode,r.stdout[-1200:],r.stderr[-1200:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--before');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 # The normal whole-module gate compiles the unchanged original module and real
 # Nini/Caps/SDK classes, and also checks RegionLoaded separately from Initialise.
 r=subprocess.run([sys.executable,str(Path(__file__).with_name('compare_simulator_features.py')),'--native',a.native,'--dotnet',a.dotnet,'--work',str(w/'whole-module')],capture_output=True,text=True,timeout=180);assert not r.returncode,r.stdout+r.stderr;print(r.stdout.strip())
 rows=[];labels=[]
 def add(label,aliases,*texts):rows.append(str(aliases)+' '+ ' '.join(encode(t) for t in texts));labels.append(label)
 values=[None,'','true','false','TrUe','FaLsE','True','False','Yes','No','On','Off','1','0',' true ','false\t','tr\u00adue','fa\u200blse','t\u034frue','true\0','\0false','t\u0301rue','True\ud800','${missing}','"True"','"False"','TRUE\u200d','\ufeffTrue','True\U0001f600','" TrUe "','"false\t"']
 for aliases,export,syntax in itertools.product(range(4),values,values):add('typed-getter',aliases,ini(export,syntax))
 for aliases,first,second in itertools.product(range(4),[ini('true','bad-script'),ini('bad-export','false'),ini('tr\u00adue','false'),ini('false','true'),ini('Yes','Off')],['',ini(),ini(None,'false'),ini('true',None),ini('invalid-export','invalid-script')]):add('same-order-live-defaults',aliases,first,second)
 for aliases in range(4):
  for text in [None,'','[Other]\nx=True\n','[SimulatorFeatures]\nExportSupported=first\nExportSupported=True\n','[Bad','[SimulatorFeatures]\nExportSupported=True\n[SimulatorFeatures]\nScriptSyntax=False\n']:
   add('source-shape-null-defaults',aliases,text,ini())
 source=invoke([a.dotnet,str(w/'whole-module/app/Features.dll'),'--config-probe'],rows);native=invoke([a.native,'--config-probe'],rows);assert len(source)==len(native)==len(rows)
 differences=[dict(index=i,label=labels[i],row=row,source=s,native=n) for i,(row,s,n) in enumerate(zip(rows,source,native)) if s!=n]
 before=[]
 if a.before:
  previous=invoke([a.before,'--config-probe'],rows);assert len(previous)==len(rows);before=[dict(index=i,label=labels[i],row=row,source=s,previous=n) for i,(row,s,n) in enumerate(zip(rows,source,previous)) if s!=n];assert before,'Before snapshot must reproduce the proven manual ASCII/config alias defect'
 selected={}
 if a.before:
  for name,aliases,export,syntax,changed in [('icu-equivalent',0,'tr\u00adue',None,True),('owning-source-alias',1,'On',None,True),('argv-alias-does-not-leak',2,'On',None,True),('ordinary-defaults',0,None,None,False)]:
   row=str(aliases)+' '+encode(ini(export,syntax));index=rows.index(row);selected[name]=dict(row=row,source=source[index],after=native[index],before=previous[index]);assert (previous[index]!=source[index])==changed,(name,selected[name])
 report=dict(cases=len(rows),initialisations=sum(len(row.split())-1 for row in rows),differences=differences[:8],before_executed=bool(a.before),before_difference_count=len(before) if a.before else None,before_examples=before[:8],selected_before_after=selected,source_hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'bin/Nini.dll',ROOT/'OpenSim/Region/ClientStack/Linden/Caps/SimulatorFeaturesModule.cs']},scope='Original complete module Initialise method through actual Nini.dll, invariant ICU equivalent strings, raw UTF16 source errors, config-local/source aliases vs separately merged argv aliases, exact field read order and repeated-init defaults/partial failures. RegionLoaded bool.Parse remains separately covered by full original-module gate.');(w/'simulator-features-config-results.json').write_text(json.dumps(report,indent=2));assert not differences,json.dumps(differences[:3]);print(f'PASS: {len(rows)} original Initialise/Nini configuration flows'+(f'; old-initialise mismatches={len(before)}' if a.before else ''))
if __name__=='__main__':main()
