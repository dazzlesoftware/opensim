"""Actual BCL/native current-time capture boundaries and last-write preservation."""
import argparse,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 ET.SubElement(ET.SubElement(project,'ItemGroup'),'Compile',Include=str(Path(__file__).with_name('flotsam_access_clock_reference.cs').resolve()));file=w/'Reference.csproj';ET.ElementTree(project).write(file)
 r=subprocess.run([a.dotnet,'build',str(file),'-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 observations=[]
 for engine in ['bcl','native']:
  for i in range(24):
   path=w/f'{engine}-{i}.bin';path.write_bytes(b'unchanged write timestamp');cmd=[a.native,'--clock',str(path)] if engine=='native' else [a.dotnet,str(w/'app/Reference.dll'),str(path)]
   r=subprocess.run(cmd,capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr;before,stamp,after,preserve,quantum=map(int,r.stdout.strip().split('|'));assert before<=stamp<=after and preserve and quantum,(engine,r.stdout);observations.append(dict(engine=engine,before=before,stamp=stamp,after=after,modified_preserved=bool(preserve),source_tick_quantum=bool(quantum)))
 result=dict(cases=len(observations),observations=observations);(w/'access-clock-results.json').write_text(json.dumps(result,indent=2));print(f"PASS: {len(observations)} actual BCL/native access-clock boundary observations")
if __name__=='__main__':main()
