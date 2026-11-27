"""Exact original TeleportObject orchestration with controlled scene boundaries."""
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
    text=(ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_text(encoding='utf-8-sig')
    method=extract(text,r'public int TeleportObject\(')
    constants=text[text.index('        const int OSTPOBJ_STOPATTARGET'):text.index('        public int TeleportObject(')]
    wrapper='''using System;using System.Linq;using System.Collections.Generic;using System.Text.Json;using OpenMetaverse;
class Reference {
CONSTANTS
static JsonElement f;static List<string> events;
static bool Flag(string name)=>f.GetProperty(name).GetBoolean();
static int Int(string name)=>f.GetProperty(name).GetInt32();
static string Text(string name)=>f.GetProperty(name).GetString();
static void Event(string name){events.Add(name);if(Text("throws")==name || (Text("throws")=="cooldown" && name.StartsWith("cooldown")))throw new Exception(name);}
class PhysicsActor {}
class Part {public PhysicsActor PhysActor;public object KeyframeMotion;
 public Vector3 Velocity=new(1,2,3),AngularVelocity=new(-.1f,.2f,-.3f),Acceleration=new(.01f,.02f,.03f),Scale=new(2,4,6);
 public Quaternion RotationOffset=new(0,0,.6f,.8f);
 public void ScheduleTerseUpdate(){Event("update");}}
class ScenePresence {public UUID UUID;}
class ILandObject {
 public bool IsRestrictedFromLand(UUID id){int n=id==new UUID("11111111-1111-4111-8111-111111111111")?0:1;Event("restricted"+n);return Int("restricted")==n;}
 public bool IsBannedFromLand(UUID id){int n=id==new UUID("11111111-1111-4111-8111-111111111111")?0:1;Event("banned"+n);return Int("banned")==n;}}
class LandChannel {int queries;public ILandObject current,target;
 public ILandObject GetLandObject(float x,float y){Event("land");return queries++==0?current:target;}}
class Permissions {public bool CanObjectEnterWithScripts(Reference group,ILandObject land){Event("entry");return Flag("enter");}}
class SceneState {public LandChannel LandChannel=new();public Permissions Permissions=new();
 public bool PositionIsInCurrentRegion(Vector3 position){Event("inside");return Flag("inside");}
 public bool InTeleportTargetsCoolDown(UUID a,UUID b,int timeout){
  if(a!=new UUID("11111111-1111-4111-8111-111111111111")||b!=new UUID("22222222-2222-4222-8222-222222222222"))throw new Exception("Argument order");
  Event("cooldown"+timeout);return Flag("cooldown");}
 public float GetGroundHeight(float x,float y){Event("ground");return f.GetProperty("ground").GetSingle();}}
class TeleportObjectData {public static TeleportObjectData Last;public int flags;public Vector3 vel,avel,acc;public Quaternion ori;public UUID sourceID;public TeleportObjectData(){Last=this;}}
class Util {public static void FireAndForget(Action action,object state,string name,bool flow){Event("queue");}}
bool inTransit,IsDeleted,IsSelected,attached;SceneState Scene=new();Part RootPart=new();
Vector3 AbsolutePosition=new(12,13,14);UUID UUID=new("11111111-1111-4111-8111-111111111111");
List<ScenePresence> m_sittingAvatars=new();bool IsAttachmentCheckFull()=>attached;
Reference CrossAsync(Reference group,Vector3 position,TeleportObjectData data)=>group;
void CrossAsyncCompleted(Reference group){}
METHOD
static string[] Bits(params float[] values)=>values.Select(x=>BitConverter.SingleToUInt32Bits(x).ToString()).ToArray();
static string[] Motion(Vector3 v,Vector3 a,Vector3 c,Quaternion q)=>Bits(v.X,v.Y,v.Z,a.X,a.Y,a.Z,c.X,c.Y,c.Z,q.X,q.Y,q.Z,q.W);
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);f=document.RootElement;events=new();TeleportObjectData.Last=null;
var r=new Reference();string state=Text("state");r.inTransit=state=="busy";r.IsDeleted=state=="deleted";r.attached=state=="attached";r.IsSelected=state=="selected";
r.RootPart.KeyframeMotion=state=="keyframe"?new object():null;
if(state=="noscene")r.Scene=null;
if(r.Scene!=null){var land=r.Scene.LandChannel;land.current=new();land.target=Text("parcels")=="same"?land.current:new();
 if(Text("parcels")=="no-current"||Text("parcels")=="none")land.current=null;
 if(Text("parcels")=="no-target"||Text("parcels")=="none")land.target=null;}
if(Flag("seats"))r.m_sittingAvatars=new(){new(){UUID=r.UUID},new(){UUID=new UUID("22222222-2222-4222-8222-222222222222")}};
string outcome="return";int result=0;
try{result=r.TeleportObject(new UUID("22222222-2222-4222-8222-222222222222"),new Vector3(25,27,f.GetProperty("z").GetSingle()),new Quaternion(.2f,.1f,.3f,.7f),Int("flags"));}
catch(Exception){outcome="throw";}
var p=r.RootPart;var t=TeleportObjectData.Last;
var report=new Dictionary<string,object>{{"outcome",outcome},{"inTransit",r.inTransit},{"motion",Motion(p.Velocity,p.AngularVelocity,p.Acceleration,p.RotationOffset)},
 {"position",Bits(r.AbsolutePosition.X,r.AbsolutePosition.Y,r.AbsolutePosition.Z)},{"events",events},{"planned",t==null?null:Motion(t.vel,t.avel,t.acc,t.ori)},
 {"plannedFlags",t==null?null:(object)t.flags},{"plannedSource",t?.sourceID.ToString()}};
if(outcome=="return")report["result"]=result;
Console.WriteLine(JsonSerializer.Serialize(report));
}}}'''.replace('CONSTANTS',constants).replace('METHOD',method).replace('SceneObjectGroup sog = this;','Reference sog = this;')
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
    base=dict(state='',inside=True,cooldown=False,parcels='different',enter=True,seats=True,restricted=-1,banned=-1,ground=20,z=-10,flags=0,throws='')
    cases=[]
    for state,inside,flags in itertools.product(['','busy','deleted','attached','selected','noscene','keyframe'],[False,True],range(8)):
        cases.append(dict(base,state=state,inside=inside,flags=flags))
    for parcels,enter,restricted,banned,z in itertools.product(['same','different','no-current','no-target','none'],[False,True],[-1,0,1],[-1,0,1],[-10,50]):
        cases.append(dict(base,parcels=parcels,enter=enter,restricted=restricted,banned=banned,z=z))
    for inside,cooldown,seats,flags,throws in itertools.product([False,True],[False,True],[False,True],range(8),['','inside','cooldown','land','entry','ground','update','queue']):
        cases.append(dict(base,inside=inside,cooldown=cooldown,seats=seats,flags=flags,throws=throws))
    for parcels,seats,throws in itertools.product(['same','different','no-target'],[False,True],['restricted0','restricted1','banned0','banned1']):
        cases.append(dict(base,parcels=parcels,seats=seats,throws=throws))
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs').read_bytes()).hexdigest(),
        sourceAdapter='Only the enclosing SceneObjectGroup self type is renamed Reference; method body and constants otherwise extracted unchanged.',
        scope='Original TeleportObject orchestration against native controller with controlled scene/parcel callbacks and actual SDK float operations. Return codes, transit/motion/position mutations, ordered calls, queued motion/flags/source compared. RootPart properties are modeled as fields: property-side effects, concurrent scene mutation, actual scene/physics binding, asynchronous worker execution, crossing completion/rollback and script API are not proven.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native object teleport control observations')


if __name__=='__main__':main()
