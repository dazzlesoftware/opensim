"""Compile the original complete account model and compare construction/export."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
    items=ET.SubElement(project,'ItemGroup');sources=['OpenSim/Services/Interfaces/IUserAccountService.cs','OpenSim/Framework/Culture.cs']
    for source in sources+['native-port/tests/user_account_reference.cs']:ET.SubElement(items,'Compile',Include=str(ROOT/source))
    item=ET.SubElement(items,'Reference',Include='OpenMetaverseTypes');ET.SubElement(item,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
    path=work/'AccountModelReference.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
    built=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    commands=['default','export','principal','export','init','export','random','export','objects','export']
    cases=['null .','bool 0','bool 1','uuid .']+[f'int {n}' for n in [0,-1,-2147483648,2147483647]]+[f'long {n}' for n in [2147483648,-2147483649,9223372036854775807]]
    cases += ['real '+struct.pack('>d',n).hex() for n in [0.,-0.,1.,1.5,float('nan'),float('inf')]]
    cases += ['string '+(s.encode().hex() or '.') for s in ['', 'First','123','-2147483648','2147483648','+12',' 12 ','12\0','12\0 ','\u00a012\u00a0','true','FALSE',' true\0 ','bad','00000000000000000000000000000007','{00000000-0000-0000-0000-000000000007}','a*b;a*c;bad;a*b*c;*;empty*','試験','😀']]
    for case in cases:
        for key in ['FirstName','LastName','Email','PrincipalID','ScopeID','UserLevel','UserFlags','UserTitle','UserCountry','LocalToGrid','Created','ServiceURLs']:
            commands += ['set '+key+' '+case,'export']
        commands += ['all '+case,'export']
    results=[]
    for cmd in [[a.dotnet,str(work/'app/AccountModelReference.dll')],[a.native]]:
        run=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=30)
        if run.returncode:raise RuntimeError(run.stdout[-500:]+run.stderr)
        rows=run.stdout.splitlines();assert len(rows)==len(commands),(len(rows),len(commands));results.append(rows)
    differences=[{'input':cmd,'source':x,'native':y} for cmd,x,y in zip(commands,*results) if x!=y]
    report={'passed':not differences,'observations':len(commands),'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},'differences':differences[:10]}
    (work/'user-account-parity-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:2],indent=2))
    print(f'PASS: {len(commands)} user account model observations')
if __name__=='__main__':main()
