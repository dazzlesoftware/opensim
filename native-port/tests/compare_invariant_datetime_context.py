"""Actual BCL timezone-rule evaluator comparison; native OS loaders are separate."""
import argparse,calendar,datetime,hashlib,json,os,random,subprocess,time,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
from compare_invariant_datetime_parse import Peer
ROOT=Path(__file__).resolve().parents[2];MAX=3155378975999999999;DAY=864000000000;MASK=(1<<62)-1

def compile_oracle(dotnet,work):
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for name in ['invariant_datetime_context_reference.cs','invariant_datetime_context_unix_reference.cs']:ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests'/name))
 file=work/'ContextReference.csproj';ET.ElementTree(project).write(file,encoding='utf-8',xml_declaration=True)
 r=subprocess.run([dotnet,'build',str(file),'--configuration','Release','-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True,timeout=90);assert not r.returncode,r.stdout+r.stderr
 return [dotnet,str(work/'app/ContextReference.dll')]
def ticks(y,m,d):return (datetime.date(y,m,d)-datetime.date(1,1,1)).days*DAY

def transition(y,data):
 tod,month,week,day,dow,fixed=data
 if not fixed:
  first=(datetime.date(y,month,1).weekday()+1)%7
  if week<=4:day=1+(dow-first+7)%7+(week-1)*7
  else:
   last=calendar.monthrange(y,month)[1];day=last-((first+last-1)%7-dow+7)%7
 else:day=min(day,calendar.monthrange(y,month)[1])
 return ticks(y,month,day)+tod

def queries(base,rules,quick):
 points={0,1,DAY-1,DAY,DAY+1,MAX,MAX-1,ticks(9999,12,31),ticks(2024,1,1),ticks(2024,3,10),ticks(2024,11,3)}
 for r in rules:
  start,end,delta,extra,no=r[:5];start&=MASK;end&=MASK
  for edge in [start,end]:
   for diff in [-DAY,-1,0,1,DAY]:
    if 0<=edge+diff<=MAX:points.add(edge+diff)
  if no:continue
  ys=datetime.date.fromordinal(start//DAY+1).year;ye=datetime.date.fromordinal(end//DAY+1).year
  for y in {ys,ye,(ys+ye)//2,2024,1,9999}:
   if not ys<=y<=ye:continue
   for trans in [r[5:11],r[11:17]]:
    edge=transition(y,trans)
    for diff in [-DAY,-delta-1,-delta,-1,0,1,delta,delta+1,DAY]:
     for offset in {0,-base-extra,-base-extra-delta}:
      if 0<=edge+diff+offset<=MAX:points.add(edge+diff+offset)
 rng=random.Random(52991)
 for _ in range(10 if quick else 150):points.add(rng.randrange(MAX+1))
 for t in sorted(points):
  yield 'U '+str(t)
  for kind in [0,2,3]:yield 'L '+str(t|(kind<<62))

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--quick',action='store_true');p.add_argument('--native-windows-loader',action='store_true');p.add_argument('--native-context',action='store_true');p.add_argument('--globalization-invariant',action='store_true');p.add_argument('--now-rule-binding',action='store_true');p.add_argument('--iana-wheel',type=Path);a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
 environment=os.environ.copy();environment['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT']='1' if a.globalization_invariant else '0';source=Peer(compile_oracle(a.dotnet,work),env=environment);native=Peer([a.native,'--probe']);started=time.monotonic();differences=[];cases=0;zone_count=0;planned=0
 try:
  metadata=source.ask('META');assert metadata.startswith('8.'),metadata
  source.send('IDS');ids=[]
  while (line:=source.read())!='END':ids.append(line)
  if a.quick:ids=[name for name in ids if any(word in name for word in ['Pacific Standard','Mountain Standard','Morocco','Lord Howe','Russia','UTC','GMT','Samoa'])]
  ids=['LOCAL'] if a.native_context else ['LOCAL']+ids
  for name in ids:
   source.send('ZONE '+name);header=source.read();assert header.startswith('ZONE '),header;model=[]
   while (line:=source.read())!='END':model.append(line)
   if a.native_context:native.send('NATIVE '+str(int(a.globalization_invariant)))
   elif a.native_windows_loader:native.send('WZONE '+name)
   else:
    native.send(header)
    for line in model:native.send(line)
   assert native.read()=='READY'
   _,base,count=header.split();rules=[list(map(int,line.split())) for line in model];assert len(rules)==int(count)
   qs=list(queries(int(base),rules,a.quick));planned+=len(qs)
   for q in qs:
    expected=source.ask(q);actual=native.ask(q);cases+=1
    # Exceptions are compared by category, not message. No expected exceptions for valid models.
    if actual!=expected:
     differences.append(dict(zone=name,query=q,source=expected,native=actual))
     if len(differences)>=20:break
   zone_count+=1
   if differences:break
  iana_observations=0
  if a.iana_wheel and not differences:
   names=[]
   with zipfile.ZipFile(a.iana_wheel)as archive:
    for name in archive.namelist():
     if archive.read(name).startswith(b'TZif'):names.append(name.removeprefix('tzdata/zoneinfo/'))
   for name in ['America/Denver','Etc/UTC','UTC','Unknown/Zone','america/denver']:
    for suffix in ['', '\0','\0x','\0\n','\n','\r','\\','\ud800','x'*1000]:names.append(name+suffix)
   for name in names:
    q='CONVERT '+name.encode('utf-16-le','surrogatepass').hex();expected=source.ask(q);actual=native.ask(q);iana_observations+=1
    if actual!=expected:differences.append(dict(query=q,source=expected,native=actual));break
  now_binding_observations=0
  if a.now_rule_binding and not differences:
   for historical in ['LOCAL','Pacific Standard Time','Fiji Standard Time','Libya Standard Time']:
    source.send('ZONE '+historical);h=source.read();native.send(h)
    while (line:=source.read())!='END':native.send(line)
    assert native.read()=='READY'
    for current in ids:
     source.send('CURRENT_ZONE '+current);h=source.read();models=[]
     while (line:=source.read())!='END':models.append(line)
     native.send(h.replace('ZONE ','CURRENT_ZONE ',1))
     for line in models:native.send(line)
     assert native.read()=='READY'
     base=int(h.split()[1]);rules=[list(map(int,line.split())) for line in models[:1]]
     for q in queries(base,rules,a.quick):
      if not q.startswith('U '):continue
      q=q.replace('U ','NOWOFFSET ',1);expected=source.ask(q);actual=native.ask(q);now_binding_observations+=1
      if expected!=actual and not(expected.startswith('EX:')and actual.startswith('EX:')):differences.append(dict(historical=historical,current=current,query=q,source=expected,native=actual));break
     if differences:break
    if differences:break
  provider_observations=0
  if a.native_context and not differences:
   assert source.ask('CUTOFF')==native.ask('CUTOFF');provider_observations+=1
   for y,m,d in [(1972,6,30),(1972,12,31),(2015,6,30),(2016,12,31),(2017,1,1),(2024,1,1)]:
    for h,minute in [(23,59),(0,0),(16,59),(17,59)]:
     q=f'LEAP {y} {m} {d} {h} {minute}';expected=source.ask(q);actual=native.ask(q);provider_observations+=1
     if actual!=expected:differences.append(dict(query=q,source=expected,native=actual))
   for _ in range(100):
    before=int(source.ask('UTCNOW'))&MASK;utc=int(native.ask('UTCNOW'));after=int(source.ask('UTCNOW'))&MASK
    if utc>>62!=1 or not before<=(utc&MASK)<=after:differences.append(dict(query='UTCNOW',before=before,native=utc,after=after))
    before=int(source.ask('UTCNOW'))&MASK;local=int(native.ask('NOW'));after=int(source.ask('UTCNOW'))&MASK
    offset=int(source.ask('L '+str(local)));converted=(local&MASK)-offset
    expected=source.ask('U '+str(converted)).split('|')
    if not before<=converted<=after or local>>62!=(3 if expected[1]=='1' else 2):differences.append(dict(query='NOW',before=before,native=local,offset=offset,after=after))
    provider_observations+=2
  report=dict(passed=not differences and zone_count==len(ids),cases=cases,plannedCases=planned,zones=zone_count,availableZones=len(ids),completeCorpus=not differences and zone_count==len(ids),metadata=metadata,elapsedSeconds=time.monotonic()-started,nativeWindowsLoader=a.native_windows_loader,nativeContext=a.native_context,globalizationInvariant=a.globalization_invariant,ianaConversionObservations=iana_observations,providerObservations=provider_observations,nowRuleBindingObservations=now_binding_observations,scope=('Actual current-Windows native context callbacks: local zone offsets, cutoff, leap validation and clock brackets. ' if a.native_context else 'Actual Windows native registry/local-zone loader and BCL offset comparison. ' if a.native_windows_loader else 'Source-derived shared native evaluator supplied exact serialized BCL AdjustmentRule state. ')+'Actual BCL internal offset evaluator oracle. No actual Unix execution, platform fault injection or clock leap-event simulation is certified by this run.',differences=differences)
  (work/'invariant-datetime-context-results.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],json.dumps(differences[:3]);print(f'PASS: {cases} actual BCL timezone rule observations in {zone_count} zones')
 finally:source.close();native.close()
if __name__=='__main__':main()
