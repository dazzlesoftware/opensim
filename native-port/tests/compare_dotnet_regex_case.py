"""Exact UTF16 case-equivalence sets and lazy culture behavior from pinned .NET Regex."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True);lab=Path(__file__).resolve().parent
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(lab/'dotnet_regex_case_reference.cs'));ET.ElementTree(project).write(work/'Reference.csproj')
result=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);(work/'build.txt').write_text(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr
outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 result=subprocess.run(command,capture_output=True,timeout=180);(work/(name+'.txt')).write_bytes(result.stdout);(work/(name+'-error.txt')).write_bytes(result.stderr);assert result.returncode==0,(name,result.stderr);outputs[name]=result.stdout.replace(b'\r\n',b'\n')
assert outputs['reference'].count(b'\n')==1048590,outputs['reference'][:200]
passed=outputs['reference']==outputs['native'];report=dict(passed=passed,observations=1048590,applicationParityVerified=False,scope='Pinned RegexCaseEquivalences complete UInt16 domain, four cultures and four initial lazy-cache states plus culture-name classification. Native primitive is registered for the future regex parser/runtime; login still uses std::regex and full Regex feature parity is incomplete.',sha256={name:hashlib.sha256(value).hexdigest() for name,value in outputs.items()});(work/'results.json').write_text(json.dumps(report,indent=2));assert passed,report;print('PASS: 1048590 actual .NET regex case-equivalence observations')
