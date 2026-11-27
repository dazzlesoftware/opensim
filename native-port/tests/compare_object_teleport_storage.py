"""Actual SceneObjectPart motion properties without a PhysicsActor, bit-for-bit."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT,extract
from compare_inventory_folder_decode import run


def build_reference(dotnet,work):
    source=(ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs').read_text(encoding='utf-8-sig')
    properties='\n'.join(extract(source,pattern) for pattern in [r'public Quaternion RotationOffset\b',
        r'public override Vector3 Velocity\b',r'public Vector3 AngularVelocity\b',r'public Vector3 Acceleration\b'])
    invalid=extract((ROOT/'OpenSim/Framework/Util.cs').read_text(encoding='utf-8-sig'),r'public static bool IsNanOrInfinity\(Vector3 v\)')
    wrapper='''using System;using System.Linq;using System.Text.Json;using System.Runtime.CompilerServices;using OpenMetaverse;
class Reference {
class Util {INVALID}
class PhysicsActor {public bool IsPhysical;public Vector3 Velocity,RotationalVelocity,Acceleration;public Quaternion Orientation;}
class ShapeState {public byte PCode=9,State=0;}
class Group {public Part RootPart;}
class Log {public void Error(string text){}}
class Entity {public virtual Vector3 Velocity{get;set;}}
class Part:Entity {
public PhysicsActor PhysActor;public Group ParentGroup;uint _parentID;ShapeState Shape=new();Log m_log=new();
Vector3 m_velocity,m_angularVelocity,m_acceleration;Quaternion m_rotationOffset;
Quaternion GetWorldRotation()=>m_rotationOffset;
PROPERTIES
}
static float[] Values(JsonElement f,string name)=>f.GetProperty(name).EnumerateArray().Select(x=>BitConverter.UInt32BitsToSingle(uint.Parse(x.GetString()))).ToArray();
static Vector3 V(JsonElement f,string name){var a=Values(f,name);return new(a[0],a[1],a[2]);}
static Quaternion Q(JsonElement f,string name){var a=Values(f,name);return new(a[0],a[1],a[2],a[3]);}
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;
var p=new Part();p.Velocity=V(f,"velocity");p.AngularVelocity=V(f,"angular");p.Acceleration=V(f,"acceleration");p.RotationOffset=Q(f,"current");
var v=p.Velocity;var a=p.AngularVelocity;var c=p.Acceleration;var q=p.RotationOffset;
Console.WriteLine(JsonSerializer.Serialize(new[]{v.X,v.Y,v.Z,a.X,a.Y,a.Z,c.X,c.Y,c.Z,q.X,q.Y,q.Z,q.W}.Select(x=>BitConverter.SingleToUInt32Bits(x).ToString())));
}}}'''.replace('INVALID',invalid).replace('PROPERTIES',properties)
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
    parser.add_argument('--keyframed-world',action='store_true')
    args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    reference=args.reference or build_reference(args.dotnet,work)
    libraries={name:hashlib.sha256((ROOT/'bin'/name).read_bytes()).hexdigest() for name in ['OpenMetaverse.dll','OpenMetaverseTypes.dll']}
    for name,digest in libraries.items():assert hashlib.sha256((reference.parent/name).read_bytes()).hexdigest()==digest
    def case(v,a,c,q):return dict(bits=True,storage=True,velocity=list(map(str,v)),angular=list(map(str,a)),acceleration=list(map(str,c)),current=list(map(str,q)))
    vectors=[[0,0,0],[0x80000000,0x3f800000,0xbf800000]]
    for axis,word in itertools.product(range(3),[0x7f800000,0xff800000,0x7fc00000,0xffc00000,0x7f800001,0x7f7fffff,1]):
        v=[0x3f800000]*3;v[axis]=word;vectors.append(v)
    rotations=[[0,0,0,0x3f800000],[0,0,0,0],[0x3dcccccd,0x3e4ccccd,0x3e99999a,0x3ecccccd],
               [0x7f800000,0,0,0],[0x7fc00000,0,0,0],[0x7f7fffff]*4,[1,0,0,0]]
    cases=[case(v,v,v,q) for v,q in itertools.product(vectors,rotations)]
    rng=random.Random(918)
    for _ in range(500):cases.append(case(*[[rng.getrandbits(32) for _ in range(n)] for n in [3,3,3,4]]))
    original=run([args.dotnet,str(reference)],cases);native=run([args.native]+(['--keyframed-world'] if args.keyframed_world else []),cases)
    if args.keyframed_world:
        # KFM writes Velocity, AngularVelocity and RotationOffset, not Acceleration.
        original=[v[:6]+v[9:] for v in original]
        native=[v[:6]+v[9:] for v in native]
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        keyframedWorld=args.keyframed_world,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs').read_bytes()).hexdigest(),
        scope=('World root writes compared with original Velocity, AngularVelocity and RotationOffset, no physics actor; Acceleration excluded because keyframed motion does not assign it. Stale object/event behavior has a separate World test. Physical actor side effects/readback and live motion scheduling remain pending.' if args.keyframed_world else 'Exact original four SceneObjectPart properties and Util.IsNanOrInfinity(Vector3), no physics actor, actual SDK normalization. Input/output float bits include infinities, NaNs and random bit patterns. Physical actor side effects/readback and asynchronous/concurrent scene behavior are not proven.'))
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native root motion storage observations')


if __name__=='__main__':main()
