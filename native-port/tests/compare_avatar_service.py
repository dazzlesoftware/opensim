"""Compare actual avatar service methods against callback-controlled storage."""
import argparse,hashlib,json,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference,ROOT,extract
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();base,_=reference(a.dotnet,w/'base');project=ET.parse(base);props=project.getroot().find('PropertyGroup');ET.SubElement(props,'StartupObject').text='AvatarServiceReference';group=project.getroot().find('ItemGroup')
    service='OpenSim/Services/AvatarService/AvatarService.cs';source=(ROOT/service).read_text(encoding='utf-8-sig');methods=[extract(source,r'public [^\n{]+ '+name+r'\(') for name in ['GetAppearance','SetAppearance','GetAvatar','SetAvatar','ResetAvatar','SetItems','RemoveItems']]
    snapshot=extract((ROOT/'native-port/tests/avatar_appearance_reference.cs').read_text(encoding='utf-8'),r'static string Snapshot\(');wrapper=w/'Service.cs';wrapper.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Data;using OpenSim.Framework;using OpenSim.Services.Interfaces;public partial class AvatarServiceReference{'+snapshot+'\n'.join(methods)+'}',encoding='utf-8')
    for f in [ROOT/'OpenSim/Services/Interfaces/IAvatarService.cs',ROOT/'OpenSim/Data/IAvatarData.cs',ROOT/'OpenSim/Framework/Culture.cs',ROOT/'native-port/tests/avatar_service_reference.cs',wrapper]:ET.SubElement(group,'Compile',Include=str(f))
    path=w/'AvatarService.csproj';project.write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    def token(s):return '-' if s is None else s.encode().hex() or '.'
    commands=[f'{mode} {outcome} {target} {mutation} '+token('1,2') for mode in range(7) for outcome in range(3) for target in range(7) for mutation in range(11)]
    for height in [None,'','bad','1,2','1,234','1,2,3','1.5','0','-0','-1','10','10.1','NaN','Infinity','-Infinity','1e999','0,0','12\0','12\0 ']:commands.append('0 0 0 0 '+token(height))
    rows=[]
    for cmd in [[a.dotnet,str(w/'app/AvatarService.dll')],[a.native]]:
        r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=45)
        if r.returncode:raise RuntimeError(r.stdout[-500:]+r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0])==len(rows[1])==len(commands)
    differences=[{'profile':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y]
    (w/'avatar-service-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':len(commands),'sourceHash':hashlib.sha256((ROOT/service).read_bytes()).hexdigest(),'differences':differences[:2]},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:1],indent=2)[:4000])
    print(f'PASS: {len(commands)} avatar service source observations')
if __name__=='__main__':main()
