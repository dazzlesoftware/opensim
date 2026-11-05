"""Actual .NET8 invariant DateTime.TryParse with BCL-backed dynamic context queries."""
import argparse,datetime,hashlib,itertools,json,os,queue,random,subprocess,threading,time,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def encoded(value):return '~' if value is None else value.encode('utf-16-le','surrogatepass').hex() or '.'
def compile_oracle(dotnet,work):
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,key).text=value
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(ROOT/'native-port/tests/invariant_datetime_parse_reference.cs'))
 cs=work/'InvariantDateTimeReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True)
 result=subprocess.run([dotnet,'build',str(cs),'-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True,encoding='utf-8',timeout=90);assert result.returncode==0,result.stdout+result.stderr
 return [dotnet,str(work/'app/InvariantDateTimeReference.dll')]
class Peer:
 def __init__(self,command,env=None):
  self.proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1,env=env);self.lines=queue.Queue();self.errors=[]
  def read():
   for line in self.proc.stdout:self.lines.put(line.rstrip('\r\n'))
   self.lines.put(None)
  def errors():
   for line in self.proc.stderr:self.errors.append(line)
  threading.Thread(target=read,daemon=True).start();threading.Thread(target=errors,daemon=True).start()
 def send(self,line):self.proc.stdin.write(line+'\n');self.proc.stdin.flush()
 def read(self):
  value=self.lines.get(timeout=20);assert value is not None,('Peer exited',self.proc.poll(),''.join(self.errors));return value
 def ask(self,line):self.send(line);return self.read()
 def close(self):
  self.proc.stdin.close()
  try:self.proc.wait(timeout=3)
  except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
def corpus(quick):
 rows=[None,'',' ','\t','2024年1月2日','2024년1월2일','2024年1月2日 3時4分5秒','2024-01-02T03:04:05.00000005','2024-01-02T03:04:05,1234567','Tue, 02 Jan 2024 03:04:05 GMT','Monday, 02 Jan 2024','2024-01-02T03:04:05+14:01','2024-01-02T03:04:05+14:00','12:34','January 2','1/2/30','1/2/49','1/2/50','2024-1-2T3:4:5','2024-01-02 3:4:5','#2024-01-02#','2024-01-02\x00','2024-01-02\x00\x00','2024/\u200f01/\u200f02']
 months=['January','February','March','April','May','June','July','August','September','October','November','December']
 for year,month,day in itertools.product([1,49,50,99,100,999,1600,1900,2000,2024,9999],range(1,13),[0,1,2,28,29,30,31,32]):
  rows.extend([f'{year}-{month}-{day}',f'{month}/{day}/{year}',f'{day} {months[month-1]} {year}',f'{year}年{month}月{day}日'])
 for timepart,date in itertools.product(['0:0','3:4:5','03:04:05','12:00','23:59:59','24:00','24:00:00','23:60:00','23:59:60','12 AM','12PM','0AM','13 PM','12:34:56 PM','1 PM','1P','12:34 AM','12.34'],['','2024-01-02 ','2024-01-02T']):
  rows.append(date+timepart)
 for fraction,separator,marker in itertools.product(['','0','1','5','00000005','00000015','00000025','1234567','12345678','123456789','999999999','00000000000000000001'],['.',','],['T',' ']):
  rows.append('2024-01-02'+marker+'03:04:05'+separator+fraction)
 for h,m,sign,fmt in itertools.product([0,1,7,14,15,23,99],[0,1,30,59,60],['+','-'],range(4)):
  offset=[f'{h}',f'{h:02}',f'{h:02}{m:02}',f'{h:02}:{m:02}'][fmt];rows.append('2024-01-02T03:04:05'+sign+offset)
 for word in months+['Sun','Mon','Tue','Wed','Thu','Fri','Sat','Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday','AD','A.D.','AM','PM','GMT','Z']:
  rows.extend([word+' 2 2024','2 '+word+' 2024','2024 2 '+word,word.lower()+' 2 2024',word.upper()+' 2 2024'])
 def hebrew(number):
  letters=''
  while number>=400:letters+='ת';number-=400
  for value,char in [(300,'ש'),(200,'ר'),(100,'ק')]:
   if number>=value:letters+=char;number-=value
  if number in [15,16]:letters+='טו' if number==15 else 'טז';number=0
  for value,char in [(90,'צ'),(80,'פ'),(70,'ע'),(60,'ס'),(50,'נ'),(40,'מ'),(30,'ל'),(20,'כ'),(10,'י')]:
   if number>=value:letters+=char;number-=value
  if number:letters+='אבגדהוזחט'[number-1]
  return letters+"'" if len(letters)==1 else letters[:-1]+'"'+letters[-1:]
 for n in range(1,1000):
  value=hebrew(n);rows.extend(['1/1/'+value,value+' January 2024','2024/1/'+value])
 for date in ['0001-01-01','0001-01-02','0001-12-31','9999-01-01','9999-12-31','2024-03-10','2024-11-03','2024-02-29','1900-02-29']:
  for timepart,zone in itertools.product(['00:00:00','01:30:00','02:30:00','23:59:59.99999999'],['','Z','GMT','+14:00','-14:00','+00:00','-07:00','-06:00']):rows.append(date+'T'+timepart+zone)
 if not quick:
  for unit in range(65536):rows.append('Jan'+chr(unit)+'uary 2 2024')
 for month,day,form in itertools.product(range(1,13),[0,1,2,28,29,30,31,32],range(10)):
  variants=[f'2024年{month} {day}',f'{month}月2024 {day}',f'2024 {month}月{day}',f'2024年{month}月{day}',f'2024 {month} {day}日',f'2024年{month} {day}日',f'{month}月{day}日2024',f'2024년{month} {day}',f'{month}월{day}일2024',f'2024年{month}月']
  rows.append(variants[form])
 tokens=months+['Sun','Monday','Tue','Wednesday','Thu','Friday','Sat','AD','A.D.','AM','PM','GMT','Z']
 for first,second,separator in itertools.product(tokens,tokens,['',' ','.',',','\\u00a0','\\x00']):
  rows.append(first+separator+second+' 2 2024')
 for core in ['2024-01-02','2024-01-02T03:04:05','Jan 2 2024','1/2/24','12:34','January 2','2 January','2024年']:
  for prefix,suffix in itertools.product(['',' ','#','##','\\x00','\\u200f'],['',' ','#','##','\\x00','\\x00\\x00','\\t#','\\u200f']):rows.append(prefix+core+suffix)
 fraction_rng=random.Random(614911)
 for _ in range(4000):
  digits=''.join(fraction_rng.choice('0123456789') for _ in range(fraction_rng.choice([1,2,3,6,7,8,9,15,16,17,32,128,512])))
  rows.append('2024-01-02'+fraction_rng.choice(['T',' '])+'03:04:05'+fraction_rng.choice(['.',','])+digits+fraction_rng.choice(['','Z','+00:00','+14:00','x']))
 rng=random.Random(521944)
 pieces=['2024','1','2','31','50','99','9999','January','Jan','Sunday','Tue','AD','A.D.','AM','PM','GMT','Z','T','/',':','.',',','-','+',' ','\t','\n','#','\x00','年','月','日','時','分','秒','\u200f','\ud800','\udfff','א','"',"'",'abc']
 for _ in range(5000 if quick else 18000):rows.append(''.join(rng.choice(pieces) for _ in range(rng.randrange(1,12))))
 return rows
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--native',required=True);parser.add_argument('--dotnet',required=True);parser.add_argument('--work',required=True,type=Path);parser.add_argument('--quick',action='store_true');parser.add_argument('--globalization-invariant',action='store_true');args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
 oracle=compile_oracle(args.dotnet,work);environment=os.environ.copy();environment['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT']='1' if args.globalization_invariant else '0';source=Peer(oracle,env=environment);native=Peer([args.native,'--probe']);started=time.monotonic();differences=[];queries={};total_queries=0
 try:
  metadata=source.ask('meta');assert metadata.startswith('M|8.'),metadata;cutoff=int(metadata.split('|')[2]);source_tokens=[]
  for _ in range(199):source_tokens.append(source.read())
  assert source.read()=='END'
  raw=subprocess.run([args.native,'--tokens'],capture_output=True,text=True,encoding='utf-8',timeout=20);assert raw.returncode==0,raw.stderr
  native_tokens=raw.stdout.splitlines();assert source_tokens==native_tokens,[(s,n) for s,n in zip(source_tokens,native_tokens)if s!=n][:4]
  rows=corpus(args.quick)
  for year,month,day in [(1,1,1),(49,2,28),(99,12,31),(100,1,1),(999,10,15),(1600,2,29),(1900,2,28),(2000,2,29),(2024,3,10),(2024,11,3),(9999,12,31)]:
   days=(datetime.date(year,month,day)-datetime.date(1,1,1)).days
   for seconds,fraction,kind,pattern in itertools.product([0,1,11045,86399],[0,1,1234567,9999999],range(3),list('dDfFgGmMoOrRstTuyY')):
    ticks=(days*86400+seconds)*10000000+fraction;rows.append(('format',ticks,kind,pattern))
  observed=0
  for index,text in enumerate(rows):
   if isinstance(text,tuple):
    _,ticks,kind,pattern=text;generated=source.ask(f'F {ticks} {kind} {pattern}');assert not generated.startswith('EX:'),(text,generated);text=bytes.fromhex(generated).decode('utf-16-le','surrogatepass')
   raw=source.ask('P '+encoded(text));assert raw.startswith('R:'),(index,raw);expected,now=raw.rsplit('|',1);native.send(encoded(text)+' '+now+' '+str(cutoff)+' '+str(int(args.globalization_invariant)));callback_log=[]
   while True:
    actual=native.read()
    if not actual.startswith('CTX '):break
    query=actual[4:];total_queries+=1
    if query not in queries:queries[query]=source.ask(query)
    response=queries[query];assert not response.startswith('EX:'),(query,response);native.send(response.replace('|',' '));callback_log.append([query,response])
   observed+=1
   if actual!=expected:
    differences.append(dict(index=index,input=encoded(text),source=expected,native=actual,context=callback_log))
    if len(differences)>=20:break
  report=dict(passed=not differences and observed==len(rows),cases=observed,plannedCases=len(rows),completeCorpus=observed==len(rows),quick=args.quick,globalizationInvariant=args.globalization_invariant,tokenHashSlots=199,metadata=metadata,runtimeAssembly=dict(path=metadata.split('|')[4],sha256=hashlib.sha256(Path(metadata.split('|')[4]).read_bytes()).hexdigest()),dependencies={str(file.relative_to(ROOT)):hashlib.sha256(file.read_bytes()).hexdigest() for file in [ROOT/'native-port/include/opensim/framework/viewer_unicode_data.hpp',ROOT/'native-port/third_party/dotnet_collation/ignore_case_tables.inc']},contextQueries=total_queries,uniqueContextQueries=len(queries),elapsedSeconds=time.monotonic()-started,scope='Actual .NET8 invariant-culture DateTime.TryParse with stylesNone. Native grammar executes independently; dynamic local timezone offsets/ambiguity and leap-second support are answered by actual BCL providers over a test-only protocol, not a production CLR fallback. Source and native199-slot invariant token tables are compared exactly. Missing-date defaults use captured current local dates; midnight/system-timezone changes during a case remain unverified. Complete native OS provider hosting and other cultures/custom DateTimeFormatInfo remain separate requirements.',differences=differences)
  (work/'invariant-datetime-parse-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert report['passed'],json.dumps(differences[:3]);print(f'PASS: {observed} actual BCL invariant datetime grammar/tick/kind observations')
 finally:source.close();native.close()
if __name__=='__main__':main()
