"""Actual BCL filename-reader lifecycle comparison; resolver URI mapping remains required.
The U,S labels are native provider trace labels mirrored by the harness, not
assertions that a concrete native System.Uri implementation has been supplied.
"""
import argparse,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',default='dotnet');p.add_argument('--work','--work-dir',dest='work_dir',required=True);a=p.parse_args()
w=pathlib.Path(a.work_dir).resolve();w.mkdir(parents=True,exist_ok=True)
s=pathlib.Path(__file__).with_name('nini_xml_uri_reader_reference.cs')
(w/'Program.cs').write_bytes(s.read_bytes())
(w/'reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>disable</Nullable><RuntimeFrameworkVersion>8.0.31</RuntimeFrameworkVersion><RollForward>Disable</RollForward></PropertyGroup></Project>')
subprocess.run([a.dotnet,'build',str(w/'reference.csproj'),'-c','Release','--nologo','-v:q'],check=True)
reference=subprocess.check_output([a.dotnet,str(w/'bin/Release/net8.0/reference.dll')],text=True).splitlines()
native=subprocess.check_output([str(pathlib.Path(a.native).resolve()),'--probe'],text=True).splitlines()
(w/'reference-output.txt').write_text('\n'.join(reference)+'\n');(w/'native-output.txt').write_text('\n'.join(native)+'\n')
if reference!=native:
 for i,(x,y) in enumerate(zip(reference,native)):
  if x!=y: print(f'{i}: BCL {x}\nNative {y}')
 raise SystemExit('URI reader mismatch')
report={'cases':len(reference),'equal':True,'scope':'actual XmlTextReader filename controlled-resolver first-Read, retry, stream ownership/state; URI provider required','runtime':subprocess.check_output([a.dotnet,'--list-runtimes'],text=True)}
(w/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
