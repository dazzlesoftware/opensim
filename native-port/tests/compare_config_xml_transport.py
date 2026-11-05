import argparse,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def run(command,rows=None):
 r=subprocess.run(command,input=rows,capture_output=True,text=True,encoding='utf-8',timeout=120);assert r.returncode==0,(r.returncode,r.stdout,r.stderr);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--download-manager',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for path in [ROOT/'native-port/tests/config_xml_transport_reference.cs',a.download_manager.resolve()]:ET.SubElement(group,'Compile',Include=str(path))
 cs=w/'XmlDownloadReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');run([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[f'{mask} {credentials} {proxy}' for mask in range(256) for credentials in [0,1] for proxy in [0,1]];text='\n'.join(rows)+'\n';source=run([a.dotnet,str(w/'app/XmlDownloadReference.dll')],text);native=run([a.native,'--probe'],text);assert len(source)==len(native)==len(rows);diff=[dict(row=row,source=s,native=n) for row,s,n in zip(rows,source,native) if s!=n];(w/'controlled-transport-results.json').write_text(json.dumps(dict(passed=not diff,cases=len(rows),differences=diff[:10]),indent=2)+'\n');assert not diff,diff[:5];print('PASS',len(rows),'unchanged XmlDownloadManager lifecycle cases')
if __name__=='__main__':main()
