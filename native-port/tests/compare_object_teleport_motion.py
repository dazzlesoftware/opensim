"""Compare exact TeleportObject motion block with the actual bundled SDK."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT
from compare_inventory_folder_decode import run


def build_reference(dotnet,work):
    text=(ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_text(encoding='utf-8-sig')
    start=text.index('        public int TeleportObject(')
    start=text.index('            bool stop =',start)
    end=text.index('            if(Scene.PositionIsInCurrentRegion',start)
    block=text[start:end]
    constants=text[text.index('        const int OSTPOBJ_STOPATTARGET'):text.index('        public int TeleportObject(')]
    wrapper='''using System;using System.Linq;using System.Text.Json;using OpenMetaverse;
class Reference {
CONSTANTS
class Part {public Vector3 Velocity,AngularVelocity,Acceleration;public Quaternion RotationOffset;}
static Part RootPart;
static string[] Motion(Quaternion rotation,int flags) {
BLOCK
return new[]{vel.X,vel.Y,vel.Z,avel.X,avel.Y,avel.Z,acc.X,acc.Y,acc.Z,ori.X,ori.Y,ori.Z,ori.W}
 .Select(x=>BitConverter.SingleToUInt32Bits(x).ToString()).ToArray(); }
static float[] Values(JsonElement f,string name)=>f.GetProperty(name).EnumerateArray().Select(x=>x.GetSingle()).ToArray();
static Vector3 V(JsonElement f,string name){var a=Values(f,name);return new Vector3(a[0],a[1],a[2]);}
static Quaternion Q(JsonElement f,string name){var a=Values(f,name);return new Quaternion(a[0],a[1],a[2],a[3]);}
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;
RootPart=new Part{Velocity=V(f,"velocity"),AngularVelocity=V(f,"angular"),Acceleration=V(f,"acceleration"),RotationOffset=Q(f,"current")};
Console.WriteLine(JsonSerializer.Serialize(Motion(Q(f,"rotation"),f.GetProperty("flags").GetInt32())));
}}}'''.replace('BLOCK',block).replace('CONSTANTS',constants)
    (work/'Reference.cs').write_text(wrapper,encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
    group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include='Reference.cs')
    for name in ['OpenMetaverse','OpenMetaverseTypes']:
        ref=ET.SubElement(group,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/(name+'.dll'))
    ET.ElementTree(project).write(work/'Reference.csproj')
    result=subprocess.run([dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    return work/'app/Reference.dll'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',required=True);parser.add_argument('--dotnet',required=True)
    parser.add_argument('--reference',type=Path);parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    reference=args.reference or build_reference(args.dotnet,work)
    libraries={name:hashlib.sha256((ROOT/'bin'/name).read_bytes()).hexdigest() for name in ['OpenMetaverse.dll','OpenMetaverseTypes.dll']}
    for name,digest in libraries.items():assert hashlib.sha256((reference.parent/name).read_bytes()).hexdigest()==digest
    rotations=[[0,0,0,1],[0,0,0,0],[0,0,0,-1],[0,0,.6,.8],[.1,.2,.3,.4],
               [0,0,1,0],[1e20,0,0,0]]
    rotations += [[value,0,0,0] for value in [1e-8,9.999e-8,1e-7,1.001e-7,1e-6,1e-5,1e-4,
                                            .0009999,.001,.0010001]]
    rotations += [[.001,0,0,value] for value in [.99998,.99999,.999999,1,-1]]
    cases=[dict(current=current,rotation=rotation,flags=flags,velocity=[1.25,-2.5,3.75],angular=[-.1,.2,-.3],acceleration=[.01,.02,.03])
           for current,rotation,flags in itertools.product([[0,0,0,1],[.1,.2,.3,.4],[0,0,.6,.8],
                [0,0,.001,.999999],[0,0,0,-1]],rotations,list(range(8))+[-1,8])]
    rng=random.Random(1068)
    for _ in range(500):
        cases.append(dict(current=[rng.uniform(-2,2) for _ in range(4)],rotation=[rng.uniform(-2,2) for _ in range(4)],
            flags=rng.randrange(8),velocity=[rng.uniform(-50,50) for _ in range(3)],angular=[rng.uniform(-5,5) for _ in range(3)],acceleration=[rng.uniform(-3,3) for _ in range(3)]))
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_bytes()).hexdigest(),
        scope='Exact original motion preparation block with actual SDK float operations, compared bit-for-bit. This does not implement or verify osTeleportObject dispatch, land/seat checks, asynchronous crossing or persistence.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native teleport motion observations')


if __name__=='__main__':main()
