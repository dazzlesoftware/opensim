"""Source CrossAsyncCompleted/Stop/AttachToBackup without actors, riders or keyframes."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT,extract
from compare_inventory_folder_decode import run


def build_reference(dotnet,work):
    group=(ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_text(encoding='utf-8-sig')
    part=(ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectPart.cs').read_text(encoding='utf-8-sig')
    completed=extract(group,r'public void CrossAsyncCompleted\(').replace('SceneObjectGroup sog','Reference sog')
    stop=extract(part,r'public void Stop\(')
    backup=extract(group,r'public virtual void AttachToBackup\(')
    wrapper='''using System;using System.Linq;using System.Collections.Generic;using System.Text.Json;using OpenMetaverse;
class Reference {
class Hook {public void CrossingFailure(){}}
class SceneObjectPart {public Vector3 GroupPosition,Velocity,AngularVelocity,Acceleration,offset=new(1,2,3);
 public bool APIDActive,PIDActive;public Hook KeyframeMotion,PhysActor;
STOP
}
class ScenePresence {public void sitSOGmoved(){}}
class Parts {public SceneObjectPart[] values;public SceneObjectPart[] GetArray()=>values;}
class RegionInfo {public uint RegionSizeX,RegionSizeY;}
class Graph {public void FireAttachToBackup(Reference group){}}
class Events {public event Action OnBackup;}
class Scene {public RegionInfo RegionInfo=new();public Graph SceneGraph=new();public Events EventManager=new();}
enum PrimUpdateFlags {FullUpdatewithAnimMatOvr}
Scene m_scene=new();SceneObjectPart m_rootPart=new();SceneObjectPart RootPart=>m_rootPart;
Parts m_parts=new();List<ScenePresence> m_sittingAvatars=new();bool IsDeleted,IsAttachment,Backup,inTransit;int updates;
void ProcessBackup(){}void ScheduleGroupForUpdate(PrimUpdateFlags flags){updates++;}
BACKUP
COMPLETE
static string[] V(Vector3 v)=>new[]{v.X,v.Y,v.Z}.Select(x=>BitConverter.SingleToUInt32Bits(x).ToString()).ToArray();
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;var r=new Reference();var p=r.m_rootPart;
r.m_scene.RegionInfo.RegionSizeX=f.GetProperty("width").GetUInt32();r.m_scene.RegionInfo.RegionSizeY=f.GetProperty("height").GetUInt32();
p.GroupPosition=new(f.GetProperty("x").GetSingle(),f.GetProperty("y").GetSingle(),7);
p.Velocity=new(1,2,3);p.AngularVelocity=new(4,5,6);p.Acceleration=new(7,8,9);
p.PIDActive=f.GetProperty("move").GetBoolean();p.APIDActive=f.GetProperty("look").GetBoolean();r.Backup=f.GetProperty("backup").GetBoolean();
var child=new SceneObjectPart();r.m_parts.values=new[]{p,child};r.CrossAsyncCompleted(r);
Console.WriteLine(JsonSerializer.Serialize(new {position=V(p.GroupPosition),velocity=V(p.Velocity),angular=V(p.AngularVelocity),acceleration=V(p.Acceleration),
 move=p.PIDActive,look=p.APIDActive,backup=r.Backup,childOffset=V(child.offset),updates=r.updates}));
}}}'''.replace('STOP',stop).replace('BACKUP',backup).replace('COMPLETE',completed)
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
    cases=[dict(width=w,height=h,x=float(x),y=float(y),move=m,look=l,backup=b)
        for w,h,x,y,m,l,b in itertools.product([16,256],[16,512],[-10,.25,.5,15.5,300],[-10,.5,511.5,600],[False,True],[False,True],[False,True])]
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_bytes()).hexdigest(),
        scope='Exact original completion/Stop/AttachToBackup bodies with actual SDK clamp, existing unattached linksets, no physics actors, riders or keyframes. Clamp, stopped motion, linear/angular controller activity, backup flag, child offset and update count compared. Physics/backup callbacks, transit state, null/deleted inputs and network update payloads are not proven by this comparison.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native crossing completion observations')


if __name__=='__main__':main()
