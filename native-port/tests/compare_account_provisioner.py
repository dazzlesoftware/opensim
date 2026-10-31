"""Compare actual CreateUser control flow with instrumented optional services."""
import argparse,hashlib,json,subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract,ROOT
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True);a=p.parse_args()
    from pathlib import Path
    w=Path(a.work).resolve();w.mkdir(parents=True,exist_ok=True);source='OpenSim/Services/UserAccountService/UserAccountService.cs';model='OpenSim/Services/Interfaces/IUserAccountService.cs';culture='OpenSim/Framework/Culture.cs'
    method=extract((ROOT/source).read_text(encoding='utf-8-sig'),r'public UserAccount CreateUser\(')
    wrapper=w/'Create.cs';wrapper.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Framework;public partial class ProvisionerReference{'+method+'}',encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
    items=ET.SubElement(project,'ItemGroup')
    for f in [wrapper,ROOT/model,ROOT/culture,ROOT/'native-port/tests/account_provisioner_reference.cs']:ET.SubElement(items,'Compile',Include=str(f))
    dep=ET.SubElement(items,'Reference',Include='OpenMetaverseTypes');ET.SubElement(dep,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
    path=w/'AccountProvisioner.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
    r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    commands=[f'{mask} {fault} {variant}' for mask in range(64) for fault in range(18) for variant in range(7)];rows=[]
    for cmd in [[a.dotnet,str(w/'app/AccountProvisioner.dll')],[a.native]]:
        r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=30)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0])==len(rows[1])==len(commands)
    differences=[{'profile':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y]
    (w/'account-provisioner-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':len(commands),'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in [source,model,culture]},'differences':differences[:10]},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:2],indent=2))
    print(f'PASS: {len(commands)} account provisioning source observations')
if __name__=='__main__':main()
