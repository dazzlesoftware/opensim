"""Actual invariant BCL rawUTF16 comparer/lower/search and bundled AliasText oracle."""
import argparse,hashlib,itertools,json,os,random,re,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):return '-' if s is None else s.encode('utf-16-le','surrogatepass').hex() or '.'
def call(cmd,rows=None):
 env=dict(os.environ);env['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT']='0';env['DOTNET_SYSTEM_GLOBALIZATION_USENLS']='0';env.pop('DOTNET_SYSTEM_GLOBALIZATION_APPLOCALICU',None);env.pop('DOTNET_ICU_VERSION_OVERRIDE',None)
 r=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',env=env,timeout=180);assert r.returncode==0,(r.returncode,r.stdout[-2000:],r.stderr[-2000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);args=p.parse_args();w=args.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/invariant_config_culture_reference.cs'));reference=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(reference,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'InvariantConfigCultureReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([args.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);source=[args.dotnet,str(w/'app/InvariantConfigCultureReference.dll')]
 rows=[]
 def lower(value):rows.append('L '+enc(value))
 def equal(a,b):rows.append('E '+enc(a)+' '+enc(b))
 def index(s,p,start):rows.append('I '+enc(s)+' '+enc(p)+' '+str(start))
 values=[None,'','ascii','ASCII','a\0b','ab','a\u00adb','\0','\u00ad','\u034f','\u200b','\u200d','a\u0301','á','ΟΣ','ος','οσ','I','i','İ','ı','ſ','s','æ','ae','é','e\u0301','カ','か','Ａ','A','\ud800','\ud801','\udc00','\ufffd','😀','\U00010400','\U00010428','Straße','STRASSE']
 for value in values:lower(value)
 for a,b in itertools.product(values,repeat=2):equal(a,b)
 for s,p in itertools.product(values,repeat=2):
  for start in sorted(set([-2147483648,-2,-1,0,1,2,3,len(s or ''),len(s or '')+1,2147483647])):index(s,p,start)
 for unit in range(65536):
  c=chr(unit);lower(c);equal(c,'\ufffd');index('x'+c+'y',c,0);index('x'+c+'y',c,2)
 header=(ROOT/'native-port/include/opensim/framework/file_system_name_case_data.hpp').read_text(encoding='utf-8-sig');scalars=set(int(v,16)for v in re.findall(r'0x([0-9a-f]+)',header));scalars={v for v in scalars if 0x10000<=v<=0x10ffff}
 for scalar in sorted(scalars):
  text=chr(scalar);lower(text);index('x'+text+'y',text,1);index('x'+text+'y',text,2)
 for context in ['ΟΣ','Σ','ΣΣ','ΑΣΑ','AΣ\u0301','İIıi','\ud800ΟΣ\udfff','A\0B','𐐀𐐨']:
  lower(context)
 for marker in ['\u0600','\u06dd','\u070f','\u0d4e','\U000110bd','\U0001f1e6','\U0001f3fb','\u0301','\u200d']:
  for text,pattern in [('x'+marker+'y',marker),('x'+marker+'y','y'),(marker+'asset_id','asset_id'),('a'+marker,marker)]:
   for start in range(len(text)+1):index(text,pattern,start)
 for text,pattern in [('x\r\ny','\r'),('x\r\ny','\n'),('x\r\ny','\r\n'),('👩\u200d👧','👧'),('🇦🇧','🇦'),('🇦🇧','🇧')]:
  for start in range(len(text.encode('utf-16-le'))//2+1):index(text,pattern,start)
 rng=random.Random(18474);alphabet='AaΣσςİIıi\u00ad\u034f\u200b\u200d\0\ud800\udfffé\u0301😀'
 for _ in range(6000):
  a=''.join(rng.choices(alphabet,k=rng.randrange(20)));b=''.join(rng.choices(alphabet,k=rng.randrange(7)));lower(a);equal(a,b);index(a,b,rng.randrange(-2,len(a)+3))
 outputs=[call(source,rows),call([args.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows)
 differences=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(rows,*outputs)) if s!=n];runtime=call(source+['--runtime']);counts={kind:sum(row.startswith(kind+' ') for row in rows) for kind in ['L','E','I']}
 report=dict(passed=not differences,cases=len(rows),counts=counts,executedPlatform=sys.platform,bclVersion=runtime[0],bclSha256=hashlib.sha256(Path(runtime[1]).read_bytes()).hexdigest(),invariantSortVersion=runtime[2],niniSha256=hashlib.sha256((ROOT/'bin/Nini.dll').read_bytes()).hexdigest(),scope='Normal selected-system-ICU invariant culture: rawUTF16 CaseInsensitiveComparer comparison/equality with actual bundled AliasText integer lookup (CaseInsensitiveHashCodeProvider-backed Hashtable), TextInfo lower/identity and CompareInfo.IndexOf start/error ordering. No Hashtable numeric-hash/enumeration, arbitrary cultures/objects, NLS, AppLocalIcu, globalization-invariant mode or unreported OS execution claimed.',differences=differences[:5]);(w/'invariant-config-culture-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not differences,json.dumps(differences[:1]);print('PASS:',len(rows),'invariant config culture cases',counts)
if __name__=='__main__':main()
