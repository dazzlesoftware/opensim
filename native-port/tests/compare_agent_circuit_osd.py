"""Compare original circuit packing/unpacking and its real appearance dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_asset_model import reference as asset_reference, ROOT
from compare_osd_numeric import cases as numeric_cases

def token(s):return s.encode().hex() or '.'
def cases():
    c=['reset','null-strings','roundtrip','empty','precedence','empty','reset','appearance','pack 3f19999a','pack 3f4ccccd','pack 3f800000','roundtrip','reset']
    for case in numeric_cases():c+=['set all '+case,'empty']
    for value in ['false','FALSE','False','0',' true ','false\0','fa\u00adlse','falſe','f\u200balse','', ' 4294967295 ', '+4294967295', '-0','-1','4294967296','18446744073709551615','1\0','1\0 ','<1, 2, 3>','<NaN, 1, -2>']:
        c+=['set all string '+value.encode().hex()]
    for handles in [['3','1','2','1','bad','0'],['18446744073709551615','18446744073709551616','-0','+2',' 3 ','3\0','3\0 ','-1',''],['-','5','5']]:
        c+=['seeds '+' '.join((token(h) if h!='-' else '-')+' '+token('seed-'+str(i)) for i,h in enumerate(handles)),'seed-edit 3 1','seed-edit 9 0','roundtrip']
    for mode in range(3):
        c+=['urls '+str(mode)+' '+ ' '.join(token(k)+' '+token(v) for k,v in [('z','Z'),('a','A'),('m','M'),('z','last'),('','empty-key')]),'url-edit a 1','url-edit new 0','roundtrip']
    c+=['xml-order','objects','roundtrip','counter-object','roundtrip','mutating-object','roundtrip','switching-object','roundtrip','appearance','pack 3f4ccccd','empty']
    return c

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);args=p.parse_args()
    work=args.work.resolve();asset_reference(args.dotnet,work/'assets');project=ET.parse(work/'assets/AssetModelReference.csproj');group=project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag=='Compile' and Path(item.get('Include')).name=='asset_model_reference.cs':group.remove(item)
    ET.SubElement(project.getroot().find('PropertyGroup'),'StartupObject').text='CircuitProgram'
    sources=['AgentCircuitData.cs','AvatarAppearance.cs','AvatarWearable.cs','AvatarAttachment.cs','WearableCacheItem.cs','IAssetCache.cs','EntityTransferContext.cs','Culture.cs']
    version=(ROOT/'OpenSim/Framework/VersionInfo.cs').read_text(encoding='utf-8-sig');stub=work/'VersionInfo.cs';stub.write_text('namespace OpenSim.Framework {public class VersionInfo {'+'\n'.join(re.search(r'public readonly static float '+name+r' = [^;]+;',version).group() for name in ['SimulationServiceVersionAcceptedMax','SimulationServiceVersionSupportedMax'])+'}}',encoding='utf-8')
    for source in [*(ROOT/'OpenSim/Framework'/s for s in sources),stub,ROOT/'native-port/tests/avatar_appearance_reference.cs',ROOT/'native-port/tests/agent_circuit_osd_reference.cs',ROOT/'native-port/tests/osd_fixture_value.cs']:ET.SubElement(group,'Compile',Include=str(source))
    for name in ['OpenMetaverse','OpenMetaverse.StructuredData']:
        node=ET.SubElement(group,'Reference',Include=name);ET.SubElement(node,'HintPath').text=str(ROOT/'bin'/(name+'.dll'))
    path=work/'AgentCircuitReference.csproj';project.write(path,encoding='utf-8',xml_declaration=True)
    built=subprocess.run([args.dotnet,'build',str(path),'-c','Release','-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    unicode_results=[]
    for command in [[args.dotnet,str(work/'app/AgentCircuitReference.dll')],[args.native]]:
        run=subprocess.run(command+['ignorables'],capture_output=True,text=True,encoding='utf-8',timeout=60)
        if run.returncode:raise RuntimeError(run.stderr)
        unicode_results.append(run.stdout.splitlines())
    assert unicode_results[0]==unicode_results[1], 'Invariant boolean Unicode comparison mismatch'
    commands=cases();results=[]
    for command in [[args.dotnet,str(work/'app/AgentCircuitReference.dll')],[args.native]]:
        run=subprocess.run(command,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=60)
        if run.returncode:raise RuntimeError(run.stdout[-500:]+run.stderr)
        results.append(run.stdout.splitlines())
    if any(len(rows)!=len(commands) for rows in results):raise AssertionError([len(commands),*map(len,results)])
    differences=[{'index':i,'input':c,'source':a,'native':b} for i,(c,a,b) in enumerate(zip(commands,*results)) if a!=b]
    report={'passed':not differences,'observations':len(commands),'unicodeBooleanScalars':0x110000-0x800,'sourceHashes':{s:hashlib.sha256((ROOT/'OpenSim/Framework'/s).read_bytes()).hexdigest() for s in sources},'libraryHashes':{s:hashlib.sha256((ROOT/'bin'/(s+'.dll')).read_bytes()).hexdigest() for s in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData']},'differences':differences[:10]}
    (work/'agent-circuit-parity-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:1],indent=2))
    print(f'PASS: {len(commands)} agent circuit state/pack observations')

if __name__=='__main__':main()
