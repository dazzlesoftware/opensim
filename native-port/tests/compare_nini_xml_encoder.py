"""Build actual pinned runtime oracles and exercise the native encoder dependency."""
import argparse,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--flow-native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True);tests=Path(__file__).resolve().parent
 for name,start,extra,native,script in [('cases','Program',False,a.native,'compare_nini_xml_encoder_cases.py'),('flows','FlowProgram',True,a.flow_native,'compare_nini_xml_encoder_flows.py'),('mutation','Program',False,a.native,'compare_nini_xml_encoder_mutation.py')]:
  folder=work/name;folder.mkdir(exist_ok=True);project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
  for key,value in {'OutputType':'Exe','TargetFramework':'net8.0','StartupObject':start,'EnableDefaultCompileItems':'false','Nullable':'enable','RuntimeFrameworkVersion':'8.0.31','RollForward':'Disable'}.items():ET.SubElement(props,key).text=value
  items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(tests/'nini_xml_encoder_reference.cs'))
  if extra:ET.SubElement(items,'Compile',Include=str(tests/'nini_xml_encoder_flow_reference.cs'))
  file=folder/'Reference.csproj';ET.ElementTree(project).write(file);subprocess.run([a.dotnet,'build',str(file),'-o',str(folder/'app'),'--nologo','-v','quiet'],check=True)
  subprocess.run([__import__('sys').executable,str(tests/script),'--native',str(Path(native).resolve()),'--dotnet',a.dotnet,'--reference',str(folder/'app/Reference.dll'),'--work',str(folder/'proof')],check=True)
 mutation=json.loads((work/'mutation/proof/mutation-results.json').read_text());cases=json.loads((work/'cases/proof/encoder-results.json').read_text());flows=json.loads((work/'flows/proof/encoder-flow-results.json').read_text());(work/'encoder-results.json').write_text(json.dumps({'cases':cases['cases'],'stateReentryFlows':flows['flows'],'mutationCases':mutation['cases'],'differences':cases['differences']+flows['differences']+mutation['differences'],'scope':'Consumed native XmlTextEncoder dependency only. Complete XML writer/document Save and simulator parity remain incomplete.'},indent=2))

if __name__=='__main__':main()
