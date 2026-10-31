"""Complete original Util.FieldToString overloads and CleanString."""
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def extract(s,sig):
 a=s.index(sig);b=s.index('{',a);i=b+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[a:i]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);source=ROOT/'OpenSim/Framework/Util.cs';s=source.read_text(encoding='utf-8');generated=w/'ExtractedFields.cs';generated.write_text('using System;using System.Text;\npartial class Util{\n'+'\n'.join(extract(s,sig) for sig in ['public static string FieldToString(byte[] bytes)','public static string FieldToString(byte[] bytes, string fieldName)','public static string CleanString('])+'\n}\n',encoding='utf-8')
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [generated,ROOT/'native-port/tests/field_string_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 cs=w/'FieldsReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'],text=True,capture_output=True);assert r.returncode==0,r.stdout+r.stderr
 commands=[f'{n} {seed} {mode} {label}' for n in [0,1,2,3,15,16,17,31,32,33,254,255] for seed in range(0,256,17) for mode in range(5) for label in range(4)];commands +=[f'1 {seed} 2 {label}' for seed in range(256) for label in range(4)];rows=[]
 for command in [[a.dotnet,str(w/'app/FieldsReference.dll')],[a.native,'--probe']]:
  r=subprocess.run(command,input='\n'.join(commands)+'\n',text=True,capture_output=True,timeout=60);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(commands);diff=[dict(command=c,source=s,native=n) for c,s,n in zip(commands,*rows) if s!=n];report=dict(passed=not diff,cases=len(commands),sourceHashes={'OpenSim/Framework/Util.cs':hashlib.sha256(source.read_bytes()).hexdigest()},scope='Unchanged complete original FieldToString overloads and CleanString; real platform Environment.NewLine and Encoding.UTF8. Printable last-byte dropping, CR/LF/NUL clean, all bytes, hex spacing/ASCII boundary, UTF8/binary dumps and nullable/Unicode label. Native labels are valid UTF8; invalid CLR UTF16 label behavior remains separate.',differences=diff[:2]);(w/'field-string-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:1]);print(f'PASS:{len(commands)} original field string traces')
if __name__=='__main__':main()
