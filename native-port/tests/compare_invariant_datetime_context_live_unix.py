"""Actual Unix native context/BCL matrix; chmod/flock fixtures require Unix.
One native context owner per child process models one source runtime cache.
"""
import argparse,os,sys,json,time,pathlib,fcntl,hashlib,platform
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--oracle');p.add_argument('--work',type=pathlib.Path,required=True);a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True);sys.path.insert(0,str(a.source/'tests'))
from compare_invariant_datetime_context import Peer,queries,MASK,compile_oracle
if not a.oracle:
 oracle_work=a.work/'oracle';oracle_work.mkdir(exist_ok=True);a.oracle=compile_oracle(a.dotnet,oracle_work)[1]
from compare_invariant_datetime_context_unix import tzif
fixtures=a.work/'fixtures';fixtures.mkdir(exist_ok=True);(fixtures/'broken-header').write_bytes(b'TZif');(fixtures/'broken-rule').write_bytes(tzif([(7200,False),(10800,True)],[-2000000000,-1000000000,0,1000000000],[0,1,0,1],'STD0DST,J366,J300'));zone=pathlib.Path('/usr/share/zoneinfo/America/Denver').read_bytes();
if (fixtures/'denied').exists():(fixtures/'denied').chmod(0o600)
(fixtures/'denied').write_bytes(zone);(fixtures/'denied').chmod(0);(fixtures/'locked').write_bytes(zone);lock=open(fixtures/'locked','rb');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
profiles=[('unset',None,None,False),('denver','America/Denver',None,False),('new-york','America/New_York',None,False),('juneau','America/Juneau',None,False),('casablanca','Africa/Casablanca',None,False),('dublin','Europe/Dublin',None,False),('lord-howe','Australia/Lord_Howe',None,False),('apia','Pacific/Apia',None,False),('kathmandu','Asia/Kathmandu',None,False),('colon',':America/Denver',None,False),('absolute','/usr/share/zoneinfo/America/Denver',None,False),('empty','',None,False),('missing','Not/AZone',None,False),('posix-env','EST5EDT,M3.2.0,M11.1.0',None,False),('empty-tzdir','America/Denver','',False),('malformed-header','broken-header',str(fixtures),False),('malformed-rule','broken-rule',str(fixtures),False),('denied','denied',str(fixtures),False),('shared-lock','locked',str(fixtures),False),('invariant-denver','America/Denver',None,True)]
report=dict(platform=platform.platform(),passed=False,completeCorpus=False,profiles=[],plannedProfiles=len(profiles),scope='Actual Unix native context callbacks vs actual Unix .NET8.0.31 BCL. Source/native child processes use identical TZ/TZDIR profiles. Actual file permission/shared-flock failures and DST-disabled retry included. Certifies only the recorded Unix platform; excludes Windows leap-event simulation.',differences=[]);started=time.monotonic()
def save():report['elapsedSeconds']=time.monotonic()-started;(a.work/'actual-linux-context-results.json').write_text(json.dumps(report,indent=2)+'\n')
try:
 for name,tz,tzdir,invariant in profiles:
  env=os.environ.copy();env['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT']='1'if invariant else'0';env.pop('DOTNET_SYSTEM_IO_DISABLEFILELOCKING',None)
  if tz is None:env.pop('TZ',None)
  else:env['TZ']=tz
  if tzdir is None:env.pop('TZDIR',None)
  else:env['TZDIR']=tzdir
  s=Peer([a.dotnet,a.oracle],env=env);n=Peer([a.native,'--probe'],env=env);cases=0;providers=0
  try:
   assert n.ask('NATIVE '+str(int(invariant)))=='READY';metadata=s.ask('META');assert metadata.startswith('8.0.31|'),metadata;s.send('ZONE LOCAL');header=s.read();assert header.startswith('ZONE '),header;rules=[]
   while(line:=s.read())!='END':rules.append(list(map(int,line.split())))
   for q in queries(int(header.split()[1]),rules,False):
    expected=s.ask(q);actual=n.ask(q);cases+=1
    if actual!=expected:report['differences'].append(dict(profile=name,query=q,source=expected,native=actual));break
   assert s.ask('CUTOFF')==n.ask('CUTOFF');providers+=1
   for q in ['LEAP 2016 12 31 23 59','LEAP 2015 6 30 23 59','LEAP 2024 1 1 0 0']:
    assert s.ask(q)==n.ask(q),q;providers+=1
   for _ in range(10):
    before=int(s.ask('UTCNOW'))&MASK;utc=int(n.ask('UTCNOW'));after=int(s.ask('UTCNOW'))&MASK;assert utc>>62==1 and before<=(utc&MASK)<=after,(name,'UTCNOW',before,utc,after)
    before=int(s.ask('UTCNOW'))&MASK;local=int(n.ask('NOW'));after=int(s.ask('UTCNOW'))&MASK;offset=int(s.ask('L '+str(local)));converted=(local&MASK)-offset;expected=s.ask('U '+str(converted)).split('|');assert before<=converted<=after and local>>62==(3 if expected[1]=='1' else 2),(name,'NOW',before,local,offset,after);providers+=2
   report['profiles'].append(dict(name=name,tz=tz,tzdir=tzdir,globalizationInvariant=invariant,metadata=metadata,offsetObservations=cases,providerObservations=providers));save();print(name,cases,providers,'PASS'if not report['differences']else'FAIL',flush=True)
   if report['differences']:break
  finally:s.close();n.close()
 report['completeCorpus']=len(report['profiles'])==len(profiles);report['passed']=report['completeCorpus']and not report['differences'];save();assert report['passed'],report['differences'];print('PASS:',sum(r['offsetObservations']+r['providerObservations']for r in report['profiles']),'actual Unix observations',flush=True)
finally:fcntl.flock(lock,fcntl.LOCK_UN);lock.close();(fixtures/'denied').chmod(0o600)
