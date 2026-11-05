"""Build pinned actual BCL/Nini oracles for consumed document Save operations."""
import argparse,json,pathlib,subprocess,sys,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--mutation-native',required=True);p.add_argument('--backend-native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=pathlib.Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);tests=pathlib.Path(__file__).resolve().parent
refs={}
for kind in ['save','mutation','backend']:
 folder=w/kind;folder.mkdir(exist_ok=True);project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for key,value in {'OutputType':'Exe','TargetFramework':'net8.0','EnableDefaultCompileItems':'false','Nullable':'enable','RuntimeFrameworkVersion':'8.0.31','RollForward':'Disable'}.items():ET.SubElement(props,key).text=value
 items=ET.SubElement(project,'ItemGroup');name='nini_xml_document_save_reference.cs'if kind=='save'else'nini_xml_document_save_'+kind+'_reference.cs';ET.SubElement(items,'Compile',Include=str(tests/name));ref=ET.SubElement(items,'Reference',Include='Nini');ET.SubElement(ref,'HintPath').text=str(tests.parents[1]/'bin/Nini.dll');file=folder/'Reference.csproj';ET.ElementTree(project).write(file);subprocess.run([a.dotnet,'build',str(file),'-o',str(folder/'app'),'--nologo','-v','quiet'],check=True);refs[kind]=folder/'app/Reference.dll'
for kind,native,ref in [('cases',a.native,refs['save']),('mutation',a.mutation_native,refs['mutation']),('backend',a.backend_native,refs['backend']),('deep',a.native,refs['save'])]:
 subprocess.run([sys.executable,str(tests/('compare_nini_xml_document_save_'+kind+'.py')),'--native',str(pathlib.Path(native).resolve()),'--dotnet',a.dotnet,'--reference',str(ref),'--work',str(w/(kind+'-proof'))],check=True)
(w/'results.json').write_text(json.dumps({'applicationParityVerified':False,'scope':'Native document WriteTo/Save and Nini string/file/stream/event composition; URI transport, foreign writer policy and full simulator/macOS parity remain incomplete.'},indent=2))
