"""Exact OSSL source-land permission predicate with real SDK UUID equality."""
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
    source=ROOT/'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs'
    method=extract(source.read_text(encoding='utf-8-sig'),r'private bool checkAllowObjectTPbyLandOwner\(')
    wrapper='''using System;using System.Text.Json;using OpenMetaverse;
class Reference {
class LandData {public UUID OwnerID,GroupID;public bool IsGroupOwned;}
class ILandObject {public LandData LandData;}
class LandChannel {public ILandObject land;public ILandObject GetLandObject(Vector3 position)=>land;}
class EstateSettings {public bool manager;public int queries;public UUID owner;
 public bool IsEstateManagerOrOwner(UUID id){if(id!=owner)throw new Exception("Wrong owner");queries++;return manager;}}
class RegionInfo {public EstateSettings EstateSettings;}
class Scene {public LandChannel LandChannel=new();public RegionInfo RegionInfo=new();}
class Host {public UUID OwnerID,GroupID;}
Scene World=new();Host m_host=new();
METHOD
static UUID Id(JsonElement frame,string name)=>new UUID(frame.GetProperty(name).GetString());
static bool Flag(JsonElement frame,string name)=>frame.GetProperty(name).GetBoolean();
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;
var r=new Reference();r.m_host.OwnerID=Id(f,"owner");r.m_host.GroupID=Id(f,"group");
if(Flag(f,"land")){r.World.LandChannel.land=new ILandObject();
 if(Flag(f,"data"))r.World.LandChannel.land.LandData=new LandData{OwnerID=Id(f,"landOwner"),GroupID=Id(f,"landGroup"),IsGroupOwned=Flag(f,"groupOwned")};}
if(Flag(f,"estate"))r.World.RegionInfo.EstateSettings=new EstateSettings{owner=Id(f,"owner"),manager=Flag(f,"manager")};
bool allowed=r.checkAllowObjectTPbyLandOwner(new Vector3(11,22,33));
int estateQueries=r.World.RegionInfo.EstateSettings?.queries??0;
Console.WriteLine(JsonSerializer.Serialize(new {allowed,estateQueries}));
}}}'''.replace('METHOD',method)
    (work/'Reference.cs').write_text(wrapper,encoding='utf-8')
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,key).text=value
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
    ids=['00000000-0000-0000-0000-000000000000','11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222']
    cases=[dict(owner=owner,group=group,landOwner=land_owner,landGroup=land_group,
                land=land,data=data,groupOwned=group_owned,estate=estate,manager=manager)
           for owner,group,land_owner,land_group,land,data,group_owned,estate,manager in
           itertools.product(ids,ids,ids,ids,[False,True],[False,True],[False,True],[False,True],[False,True])]
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs').read_bytes()).hexdigest(),
        scope='Exact original source-land permission helper with actual UUID equality. Tests return value and lazy estate query ordering. Scene lookup, same-owner bypass, destination land, seats, motion, crossing and script dispatch are outside this predicate.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native object teleport source-land observations')


if __name__=='__main__':main()
