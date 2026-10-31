"""Compare actual default appearance/link methods and framework models.

Random UUIDs and item creation clocks are instrumented for deterministic traces.
Inventory storage and avatar persistence are callbacks; LLSD inventory serialization
is excluded from the model adapter and is not established by this fixture.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract,ROOT
from compare_asset_model import reference as asset_reference
def reference(dotnet,work):
    w=work.resolve();asset_reference(dotnet,w/'assets')
    project=ET.parse(w/'assets/AssetModelReference.csproj');group=project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag=='Compile' and Path(item.get('Include')).name=='asset_model_reference.cs':group.remove(item)
    service='OpenSim/Services/UserAccountService/UserAccountService.cs';source=(ROOT/service).read_text(encoding='utf-8-sig')
    methods=[extract(source,r'protected void '+n+r'\(').replace('UUID.Random()','NextId()') for n in ['CreateDefaultAppearanceEntries','CreateCurrentOutfitLink','EstablishAppearance']]
    methods.append(extract(source,r'private void CopyWearablesAndAttachments\(').replace('CopyWearablesAndAttachments(','OriginalCopyWearablesAndAttachments(').replace('UUID.Random()','NextId()'))
    methods.append(extract(source,r'private void ApplyNextOwnerPermissions\('))
    wrapper=w/'Methods.cs';wrapper.write_text('using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;using PermissionMask=OpenSim.Framework.PermissionMask;public partial class ProvisionerReference{'+ '\n'.join(methods)+'}',encoding='utf-8')
    inventory='OpenSim/Framework/InventoryItemBase.cs';source=(ROOT/inventory).read_text(encoding='utf-8-sig');source=source[:source.index('public void ToLLSDxml')]+ '}}';source=source.replace('(int)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds','Fixture.Clock()');item=w/'InventoryItemBase.cs';item.write_text(source,encoding='utf-8')
    source=(ROOT/'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig');permissions=extract(source,r'public enum PermissionMask\s*:\s*uint');constants=w/'Constants.cs';constants.write_text('namespace OpenSim.Framework {'+permissions+'}',encoding='utf-8')
    version=(ROOT/'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig');stub=w/'VersionInfo.cs';stub.write_text('namespace OpenSim.Framework {public class VersionInfo {'+'\n'.join(re.search(r'public readonly static float '+n+r' = [^;]+;',version).group() for n in ['SimulationServiceVersionAcceptedMax','SimulationServiceVersionSupportedMax'])+'}}',encoding='utf-8')
    sources=['AvatarAppearance.cs','AvatarWearable.cs','AvatarAttachment.cs','WearableCacheItem.cs','IAssetCache.cs','EntityTransferContext.cs','InventoryNodeBase.cs','InventoryFolderBase.cs']
    account='OpenSim/Services/Interfaces/IUserAccountService.cs';util=w/'Util.cs';util.write_text('namespace OpenSim.Framework {public static class Util {public static int UnixTimeSinceEpoch()=>0;}}',encoding='utf-8')
    for path in [*(ROOT/'OpenSim/Framework'/s for s in sources),ROOT/account,util,wrapper,item,constants,stub,ROOT/'native-port/tests/appearance_provisioner_reference.cs',ROOT/'native-port/tests/appearance_transfer_fixture.cs']:ET.SubElement(group,'Compile',Include=str(path))
    for name in ['OpenMetaverse','OpenMetaverse.StructuredData']:
        node=ET.SubElement(group,'Reference',Include=name);ET.SubElement(node,'HintPath').text=str(ROOT/'bin'/(name+'.dll'))
    path=w/'AppearanceProvisioner.csproj';project.write(path,encoding='utf-8',xml_declaration=True)
    r=subprocess.run([dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return path,[service,inventory,account,'OpenSim/Framework/Util.cs',*['OpenSim/Framework/'+s for s in sources]]

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();_,paths=reference(a.dotnet,w)
    commands=[f'{avatar} {outcome} {target} {mutation} 0' for avatar in range(2) for outcome in range(5) for target in range(18) for mutation in range(5)]
    commands += [f'0 {outcome} {target} 0 {link}' for link in [1,2] for outcome in range(5) for target in range(4)]
    commands += [f'{avatar} {outcome} {target} {mutation} {link}' for avatar in range(2) for outcome in range(3) for target in range(7) for mutation in range(5) for link in range(3,14)]
    commands += [f'{wrong} {outcome} {target} {mutation} 100' for wrong in range(2) for outcome in range(5) for target in range(25) for mutation in range(13)]
    commands += [f'{wrong} {outcome} {target} 0 {variant}' for wrong in range(2) for outcome in range(3) for target in range(10) for variant in range(101,107)]
    rows=[]
    for cmd in [[a.dotnet,str(w/'app/AppearanceProvisioner.dll')],[a.native]]:
        r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=45)
        if r.returncode:raise RuntimeError(r.stdout[-500:]+r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0])==len(rows[1])==len(commands)
    differences=[{'profile':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y]
    (w/'appearance-provisioner-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':len(commands),'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in paths},'differences':differences[:2]},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:1],indent=2)[:5000])
    print(f'PASS: {len(commands)} appearance provisioning source observations')
if __name__=='__main__':main()
