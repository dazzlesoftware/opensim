"""Exact original PrimLimitsModule.DoCommonChecks versus native sampled-count rule."""
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
    source=ROOT/'OpenSim/Region/OptionalModules/PrimLimitsModule/PrimLimitsModule.cs'
    method=extract(source.read_text(encoding='utf-8-sig'),r'private string DoCommonChecks\(')
    wrapper='''using System;using System.Collections.Generic;using System.Text.Json;using OpenMetaverse;
class Reference {
class LandData {public UUID OwnerID;public bool IsGroupOwned;}
class Counts {public int Simulator;public Dictionary<UUID,int> Users=new();}
class ILandObject {public int capacity;public LandData LandData=new();public Counts PrimCounts=new();public int GetSimulatorMaxPrimCount()=>capacity;}
class EstateSettings {public UUID EstateOwner;public UUID[] EstateManagers;}
class RegionInfo {public int MaxPrimsPerUser;public EstateSettings EstateSettings=new();}
class Scene {public RegionInfo RegionInfo=new();}Scene m_scene=new();
METHOD
static UUID Id(JsonElement f,string key)=>new UUID(f.GetProperty(key).GetString());
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);var f=document.RootElement;
UUID owner=Id(f,"owner");var r=new Reference();var lo=new ILandObject();
lo.capacity=f.GetProperty("capacity").GetInt32();lo.PrimCounts.Simulator=f.GetProperty("simulator").GetInt32();lo.PrimCounts.Users[owner]=f.GetProperty("user").GetInt32();
lo.LandData.OwnerID=Id(f,"parcelOwner");lo.LandData.IsGroupOwned=f.GetProperty("groupOwned").GetBoolean();
r.m_scene.RegionInfo.MaxPrimsPerUser=f.GetProperty("maximum").GetInt32();
r.m_scene.RegionInfo.EstateSettings.EstateOwner=Id(f,"estateOwner");
r.m_scene.RegionInfo.EstateSettings.EstateManagers=f.GetProperty("manager").GetBoolean()?new[]{owner}:Array.Empty<UUID>();
string denial=r.DoCommonChecks(f.GetProperty("added").GetInt32(),owner,lo);
Console.WriteLine(JsonSerializer.Serialize(new {denial}));
}}}'''.replace('METHOD',method)
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
    zero='00000000-0000-0000-0000-000000000000';a='11111111-1111-4111-8111-111111111111';b='22222222-2222-4222-8222-222222222222'
    base=dict(added=1,owner=a,simulator=5,capacity=100,user=10,maximum=10,parcelOwner=b,groupOwned=False,estateOwner=b,manager=False)
    cases=[dict(base,added=added,simulator=simulator,capacity=capacity,user=user,maximum=maximum)
           for added,simulator,capacity,user,maximum in itertools.product([-2147483648,-1,0,1,100,2147483647],
               [-2147483648,0,10,2147483647],[-1,0,10,100,2147483647],[-2147483648,0,10,2147483647],[-1,0,10,2147483647])]
    for owner,parcel,estate,group,manager,maximum in itertools.product([zero,a,b],[zero,a,b],[zero,a,b],[False,True],[False,True],[-1,0,10]):
        cases.append(dict(base,owner=owner,parcelOwner=parcel,estateOwner=estate,groupOwned=group,manager=manager,maximum=maximum))
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/OptionalModules/PrimLimitsModule/PrimLimitsModule.cs').read_bytes()).hexdigest(),
        scope='Exact original DoCommonChecks using actual UUID equality; sampled counts/capacity, unchecked integer overflow, parcel/group/estate exemptions and denial text. Module registration/delegate chaining, count aggregation/capacity sampling, parcel lookup, alerts and full object teleport are not proven by this comparison.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native parcel prim-limit observations')


if __name__=='__main__':main()
