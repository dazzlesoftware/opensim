"""Compare actual user-account URL loading/storing and System.Web encoding."""
import argparse,hashlib,json,random,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract,ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);args=p.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'OpenSim/Services/UserAccountService/UserAccountService.cs').read_text(encoding='utf-8-sig');methods=[extract(source,r'(?:private|public) [^\n{]+ '+name+r'\(') for name in ['MakeUserAccount','StoreUserAccount']]
    wrapper=work/'Methods.cs';wrapper.write_text('using System;using System.Text;using System.Collections.Generic;using OpenSim.Data;using OpenSim.Framework;using OpenSim.Services.Interfaces;public partial class AccountUrlReference {'+'\n'.join(methods)+'}',encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
    items=ET.SubElement(project,'ItemGroup')
    sources=['OpenSim/Services/Interfaces/IUserAccountService.cs','OpenSim/Data/IUserAccountData.cs','OpenSim/Framework/Culture.cs','native-port/tests/account_urls_reference.cs']
    for path in [wrapper,*(ROOT/s for s in sources)]:ET.SubElement(items,'Compile',Include=str(path))
    item=ET.SubElement(items,'Reference',Include='OpenMetaverseTypes');ET.SubElement(item,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
    path=work/'AccountUrlsReference.csproj';ET.ElementTree(project).write(path,encoding='utf-8',xml_declaration=True)
    built=subprocess.run([args.dotnet,'build',str(path),'-c','Release','-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    values=['','a=b','a=first a=last','z=1 a=2 m=3','a=b=c =empty missing a=','a%20b=two+words empty=','key=%u8a66%u9a13','key=%ff%fe%f0%80%80%80','bad=%zz key=%uD800',' key=% a=b\tmore ','a%3db=x%3dy','*=!()*-._~\'']
    commands=['decode '+(v.encode().hex() or '.') for v in values]
    for v in values+[chr(i) for i in range(256)]+['試験','😀','\u200b','%uD83D%uDE00','%uD800','%uDC00','%u0000','%C3%a9','%e2%82','%F4%90%80%80']:
        commands.append('codec '+(v.encode().hex() or '.'))
    rng=random.Random(55172)
    for _ in range(256):
        raw=' '.join(rng.choice(['a=b','a=c','=','a=b=c','a%20b=two+words','bad=%zz','%u8a66=x','blank=','']) for _ in range(rng.randrange(8)))
        commands.append('decode '+(raw.encode().hex() or '.'))
    commands += ['objects '+str(i) for i in range(7)]
    results=[]
    for executable in [[args.dotnet,str(work/'app/AccountUrlsReference.dll')],[args.native]]:
        run=subprocess.run(executable,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=30)
        if run.returncode:raise RuntimeError(run.stdout[-500:]+run.stderr)
        rows=run.stdout.splitlines();assert len(rows)==len(commands),(len(rows),len(commands));results.append(rows)
    differences=[{'input':c,'source':a,'native':b} for c,a,b in zip(commands,*results) if a!=b]
    report={'passed':not differences,'observations':len(commands),'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources[:3]+['OpenSim/Services/UserAccountService/UserAccountService.cs']},'differences':differences}
    (work/'account-urls-parity-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:3],indent=2))
    print(f'PASS: {len(commands)} account URL persistence/encoding observations')

if __name__=='__main__':main()
