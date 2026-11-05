"""Actual invariant CurrentCulture String.StartsWith consumed Include- prefix."""
import argparse,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return 'null'
 b=s.encode('utf-16-le','surrogatepass');return ''.join(f'{b[i]+256*b[i+1]:04x}' for i in range(0,len(b),2)) or '-'
def call(cmd,rows=None):
 p=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert p.returncode==0,(p.returncode,p.stdout[-1500:],p.stderr[-1500:]);return p.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--quick',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/configuration_culture_reference.cs'));cs=w/'ConfigurationCultureReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 prefix='Include-';texts=[None,'','Include','include-','INCLUDE-','Include-','Include-foo',' Include-','Ｉｎｃｌｕｄｅ－']
 units=range(65536) if not a.quick else list(range(256))+[0x300,0x344,0x200e,0x202a,0x3002,0xd800,0xdc00,0xfffe,0xffff]
 for cp in units:
  c=chr(cp)
  for at in range(len(prefix)+1):texts.append(prefix[:at]+c+prefix[at:]+'file')
 rng=random.Random(7642);chars=['\x00','\u00ad','\u034f','\u061c','\u180e','\u200b','\u200c','\u200d','\u200e','\u200f','\u202a','\u2060','\ufeff','\ufe0f','\u0300','\u0301','\u0344','\ud800','\udc00','Ｉ','ı','İ','i','-','‐','‑','–','−','－','a','/']
 for _ in range(50000):
  s=prefix
  for _ in range(rng.randrange(1,8)):
   pos=rng.randrange(len(s)+1);s=s[:pos]+rng.choice(chars)+s[pos:]
  texts.append(s+rng.choice(['','suffix','\u0300','\x00']))
 for c,n in itertools.product(chars,[1,2,15,100,10000]):texts.extend([c*n+prefix,prefix+c*n,prefix[:4]+c*n+prefix[4:]])
 rows=[enc(s) for s in texts];source=call([a.dotnet,str(w/'app/ConfigurationCultureReference.dll')],rows);native=call([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows)
 diff=[dict(index=i,text=text,source=s,native=n) for i,(text,s,n) in enumerate(zip(texts,source,native)) if s!=n];report=dict(passed=not diff,cases=len(rows),scope='Actual .NET8 String.StartsWith("Include-") under CurrentCulture=InvariantCulture and native selected-ICU SimpleAffix_Iterators over raw UTF16. Arbitrary CurrentCulture and NLS/globalization-invariant modes require explicit different providers.',differences=diff[:20]);(w/'configuration-culture-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:8]);print('PASS:',len(rows),'actual invariant configuration Include- prefixes')
if __name__=='__main__':main()
