"""Build the pinned actual runtime oracle for the consumed native XML writer."""
import argparse,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True);tests=Path(__file__).resolve().parent
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in {'OutputType':'Exe','TargetFramework':'net8.0','EnableDefaultCompileItems':'false','Nullable':'enable','RuntimeFrameworkVersion':'8.0.31','RollForward':'Disable'}.items():ET.SubElement(props,key).text=value
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(tests/'nini_xml_text_writer_reference.cs'));file=work/'Reference.csproj';ET.ElementTree(project).write(file)
subprocess.run([a.dotnet,'build',str(file),'-o',str(work/'app'),'--nologo','-v','quiet'],check=True)
subprocess.run([sys.executable,str(tests/'compare_nini_xml_text_writer_cases.py'),'--native',str(Path(a.native).resolve()),'--dotnet',a.dotnet,'--reference',str(work/'app/Reference.dll'),'--work',str(work/'proof')],check=True)
