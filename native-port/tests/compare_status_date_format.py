"""Actual invariant DateTime.ToString status pattern across Gregorian/tick boundaries."""
import argparse,calendar,datetime,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DAY=864000000000
MAX=3155378975999999999

def invoke(command,rows=None):
 r=subprocess.run(command,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110)
 assert r.returncode==0,(r.returncode,r.stdout[-3000:],r.stderr[-3000:]);return r.stdout.splitlines()

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/status_date_format_reference.cs'));cs=w/'StatusDateFormatReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);managed=[a.dotnet,str(w/'app/StatusDateFormatReference.dll')]
 rows=[]
 def add(ticks,kind):
  assert 0<=ticks<=MAX and 0<=kind<=3
  rows.append(f'{ticks} {kind}')
 # Every day in the complete400-year Gregorian leap cycle; kinds/time/fraction vary.
 for day in range(146097):add(day*DAY+(day*23789%86400)*10000000+(day*1009%10000000),day%4)
 cycle=len(rows)
 # Every month end in every valid year checks the full year/padding/century range.
 for year in range(1,10000):
  for month in range(1,13):
   date=datetime.date(year,month,calendar.monthrange(year,month)[1]);add((date.toordinal()-1)*DAY+(year*month*9973%86400)*10000000+9999999,(year+month)%4)
 month_ends=len(rows)-cycle
 # Every second on leap day, with the immediately adjacent fractional endpoint.
 leap_day=(datetime.date(2000,2,29).toordinal()-1)*DAY
 for second in range(86400):add(leap_day+second*10000000+(9999999 if second%2 else 0),second%4)
 seconds=len(rows)-cycle-month_ends
 endpoints={0,1,9999999,10000000,DAY-1,DAY,DAY+1,DAY//2-1,DAY//2,DAY//2+1,MAX-1,MAX}
 for year in [1,4,99,100,399,400,999,1000,1582,1600,1700,1800,1900,1970,1999,2000,2001,2100,2400,9999]:
  for month,day in [(1,1),(2,28),(3,1),(12,31)]+([(2,29)] if calendar.isleap(year) else []):
   start=(datetime.date(year,month,day).toordinal()-1)*DAY
   for delta in [-1,0,1,DAY//2-1,DAY//2,DAY//2+1,DAY-1]:
    if 0<=start+delta<=MAX:endpoints.add(start+delta)
 for ticks,kind in itertools.product(sorted(endpoints),range(4)):add(ticks,kind)
 endpoint_count=len(rows)-cycle-month_ends-seconds
 rng=random.Random(880159)
 for _ in range(16000):add(rng.randrange(MAX+1),rng.randrange(4))
 outputs=[invoke(managed,rows),invoke([a.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows)
 diffs=[dict(command=c,source=s,native=n) for c,s,n in zip(rows,*outputs) if s!=n]
 reference=ROOT/'native-port/tests/status_date_format_reference.cs';report=dict(passed=not diffs,cases=len(rows),gregorianCycleDays=cycle,allYearMonthEnds=month_ends,leapDaySeconds=seconds,allKindEndpointCases=endpoint_count,randomTickCases=16000,referenceSha256=hashlib.sha256(reference.read_bytes()).hexdigest(),scope='Actual DateTime.ToString("MM/dd/yyyy hh:mm:ss") under invariant culture. Every Gregorian400-year-cycle day, every month end through year9999, every second on leap day, fractional tick endpoints, midnight/noon and raw kind bits0..3 are compared. The helper consumes validated AssetDateTime, so raw-tick construction/rejection belongs to that model; it is not a general DateTime formatter or parser. Native dispatcher/binder requires culture detection and a current-culture/general-format provider; non-invariant calendars/separators and other patterns are delegated rather than silently formatted invariant. Provider binding/lifetime/required-dependency behavior is separately tested in the native unit.',differences=diffs[:5]);(w/'status-date-format-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps(diffs[:3],indent=2);print(f'PASS: {len(rows)} actual invariant status-date observations')
if __name__=='__main__':main()
