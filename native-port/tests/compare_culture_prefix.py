"""Current invariant-culture String.StartsWith compared with native ICU prefix matching."""
import argparse,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');properties=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(properties,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(ROOT/'native-port/tests/culture_prefix_reference.cs'));cs=w/'Prefix.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');r=subprocess.run([a.dotnet,'build',str(cs),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 words=['','a','ab','abc','ABC','a-b','a\0b','\0abc','ab\0','é','e\u0301','æ','ae','Æ','Ａ','a\u034f','a\u0301','😀','試験','\u00ad','\u200b','\ufeff','\0','\t','-','GET:/a','GET:/a\u0301'];cases=list(itertools.product(words,words));points=list(range(128))+list(range(0x300,0x370))+list(range(0x2000,0x2070))+[0xad,0x34f,0x61c,0xfeff,0xfffd,0x10ffff];rng=random.Random(1);points +=[(lambda cp:cp if cp<0xd800 else cp+0x800)(rng.randrange(0x10f800)) for _ in range(200)]
 for code in points:
  c=chr(code)
  for text,prefix in [('a'+c+'b','a'),('a'+c+'b','a'+c),(c+'ab','ab'),('ab',c),('',''+c),('GET:/a'+c+'/tail','GET:/a')]:cases.append((text,prefix))
 commands=[x.encode().hex()+'|'+y.encode().hex() for x,y in cases];rows=[]
 for cmd in [[a.dotnet,str(w/'app/Prefix.dll')],[a.native]]:
  r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stdout[-1000:]+r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'text':x,'prefix':y,'source':s,'native':n} for (x,y),s,n in zip(cases,*rows) if s!=n];(w/'culture-prefix-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'scope':'Current invariant-culture String.StartsWith against OS ICU; valid UTF8, combining/ignorable/control/supplementary forms. Other cultures, invalid UTF16, OS/ICU runtime profiles and Unix execution remain pending.','differences':diffs},indent=2),encoding='utf-8')
 if diffs:raise AssertionError(json.dumps(diffs[:8]))
 print(f'PASS: {len(cases)} cultural prefix observations')
if __name__=='__main__':main()
