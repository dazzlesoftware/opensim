"""Original GetObjectDestination with actual SDK clamp and controlled grid lookup."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT,extract
from compare_inventory_folder_decode import run


def build_reference(dotnet,work):
    source=ROOT/'OpenSim/Region/CoreModules/Framework/EntityTransfer/EntityTransferModule.cs'
    method=extract(source.read_text(encoding='utf-8-sig'),r'public GridRegion GetObjectDestination\(')
    wrapper='''using System;using System.Linq;using System.Collections.Generic;using System.Text.Json;using OpenMetaverse;
class Reference {
static JsonElement f;static List<int[]> queries;
static class Constants {public const int RegionSize=256;}
public class GridRegion {public int RegionLocX,RegionLocY,RegionSizeX,RegionSizeY;}
public class RegionInfo {public uint WorldLocX,WorldLocY;public UUID ScopeID;}
public class GridService {public GridRegion GetRegionByPosition(UUID scope,int x,int y){
 queries.Add(new[]{x,y});if(f.GetProperty("throws").GetBoolean())throw new Exception("lookup");
 return f.GetProperty("found").GetBoolean()?new GridRegion{RegionLocX=I("destX"),RegionLocY=I("destY"),RegionSizeX=I("sizeX"),RegionSizeY=I("sizeY")}:null;}}
public class Scene {public RegionInfo RegionInfo=new();public GridService GridService=new();}
public class SceneObjectGroup {public Scene Scene;}
METHOD
static int I(string key)=>f.GetProperty(key).GetInt32();
static float F(string key)=>BitConverter.UInt32BitsToSingle(uint.Parse(f.GetProperty(key).GetString()));
static void Main(){string line;while((line=Console.ReadLine())!=null){
using var document=JsonDocument.Parse(line);f=document.RootElement;queries=new();
var group=new SceneObjectGroup();if(f.GetProperty("scene").GetBoolean())group.Scene=new Scene{RegionInfo=new RegionInfo{WorldLocX=unchecked((uint)I("sourceX")),WorldLocY=unchecked((uint)I("sourceY"))}};
Vector3 position=default;bool found=false;string outcome="return";
try{found=new Reference().GetObjectDestination(group,new Vector3(F("x"),F("y"),F("z")),out position)!=null;}
catch(Exception){outcome="throw";}
Console.WriteLine(JsonSerializer.Serialize(new {outcome,found,queries,position=new[]{position.X,position.Y,position.Z}.Select(x=>BitConverter.SingleToUInt32Bits(x).ToString())}));
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
    def bits(v):return str(struct.unpack('<I',struct.pack('<f',v))[0])
    base=dict(scene=True,sourceX=1024,sourceY=768,x=bits(260),y=bits(120),z=bits(30),destX=1280,destY=768,sizeX=256,sizeY=256,found=True,throws=False)
    cases=[dict(base,sourceX=sx,sourceY=sy,x=bits(x),y=bits(y),sizeX=w,sizeY=h)
           for sx,sy,x,y,w,h in itertools.product([0,1024,2147483647,-2147483648],[0,768],
               [-256,-.9,-0.,.9,255.9,256,300],[-256,-.9,0,255.9,512],[0,16,256,512,-1],[0,16,512])]
    for scene,found,throws,word in itertools.product([False,True],[False,True],[False,True],
            [0,0x80000000,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0x4f000000,0xcf000000]):
        cases.append(dict(base,scene=scene,found=found,throws=throws,x=str(word),y=str(word),z=str(word)))
    rng=random.Random(2635)
    for _ in range(400):cases.append(dict(base,sourceX=rng.randint(-2147483648,2147483647),destX=rng.randint(-2147483648,2147483647),x=str(rng.getrandbits(32)),y=str(rng.getrandbits(32))))
    original=run([args.dotnet,str(reference)],cases);native=run([args.native],cases)
    differences=[dict(input=c,original=a,native=b) for c,a,b in zip(cases,original,native) if a!=b]
    report=dict(passed=not differences,observations=len(cases),differences=differences,libraries=libraries,
        sourceSha256=hashlib.sha256((ROOT/'OpenSim/Region/CoreModules/Framework/EntityTransfer/EntityTransferModule.cs').read_bytes()).hexdigest(),
        scope='Exact original destination method with actual SDK clamp and controlled grid model. Signed lookup coordinates, unchecked overflow, float-to-int x64 behavior, arrival bits, zero-sized destination fallback and null/throw boundaries compared. Actual grid service/provider behavior, asynchronous transfer, other CPU architectures and full teleport are not proven.')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not differences,f'{len(differences)} differences; see {work / "results.json"}'
    print(f'PASS: {len(cases)} original/native object destination observations')


if __name__=='__main__':main()
