# Copyright (c) Contributors, http://opensimulator.org/
# See CONTRIBUTORS.TXT for a full list of copyright holders.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the OpenSimulator Project nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Compare GridRegion/NullRegionData and extracted local registry methods.

Constructor/module loading, URI searches, console and extra features are outside
this fixture's scope. The native implementation still requires those features.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract
ROOT=Path(__file__).resolve().parents[2]
SOURCES=["OpenSim/Services/GridService/GridService.cs","OpenSim/Services/Interfaces/IGridService.cs","OpenSim/Data/IRegionData.cs","OpenSim/Data/Null/NullRegionData.cs","OpenSim/Framework/RegionFlags.cs","OpenSim/Framework/RegionURI.cs"]
METHODS=["ValidateRegionName","RegisterRegion","DeregisterRegion","GetNeighbours","GetRegionByUUID","GetRegionByHandle","GetRegionByPosition","GetRegionRange","RegionInfo2RegionData","RegionData2RegionInfo","GetDefaultRegions","GetDefaultHypergridRegions","GetFallbackRegions","GetOnlineRegions","GetHyperlinks","GetRegionFlags","ParseFlags","IsLocalGrid","GetRegionByName","GetRegionByURI","GetLocalRegionByName","GetLocalRegionByURI","GetRegionsByName","GetRegionsByURI"]
def reference(dotnet,work):
 work.mkdir(parents=True,exist_ok=True)
 source=(ROOT/SOURCES[0]).read_text(encoding="utf-8-sig")
 methods=[extract(source,r"(?:public|private) (?:static )?[^\n{]+ "+name+r"\(") for name in METHODS]
 wrapper=work/"GridService.cs"
 wrapper.write_text("""using System;using System.Collections.Generic;using Nini.Config;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Framework.Console;using OpenSim.Data;using OpenSim.Services.Interfaces;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Services.GridService {public class GridService {
readonly IRegionData m_Database;readonly IConfigSource m_config;readonly IAuthenticationService m_AuthenticationService;
readonly GridInfo m_ThisGridInfo=new();readonly HypergridLinker m_HypergridLinker=new();readonly bool m_AllowHypergridMapSearch;readonly bool m_RegionsAutoRegistration,m_DeleteOnUnregister,m_AllowDuplicateNames;readonly string LogHeader="[GRID SERVICE]";
static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(GridService));
public GridService(IRegionData store,IConfigSource config,IAuthenticationService auth){m_Database=store;m_config=config;m_AuthenticationService=auth;var section=config.Configs["GridService"];m_RegionsAutoRegistration=section.GetBoolean("RegionsAutoRegistration",true);m_DeleteOnUnregister=m_RegionsAutoRegistration&&section.GetBoolean("DeleteOnUnregister",true);m_AllowDuplicateNames=section.GetBoolean("AllowDuplicateNames",false);m_AllowHypergridMapSearch=section.GetBoolean("AllowHypergridMapSearch",false);}
public string Flags(int previous,string specification)=>ParseFlags(previous,specification).ToString();
"""+"\n".join(methods)+"}}",encoding="utf-8")
 project=ET.Element("Project",Sdk="Microsoft.NET.Sdk");props=ET.SubElement(project,"PropertyGroup")
 for name,value in {"TargetFramework":"net8.0","OutputType":"Exe","EnableDefaultCompileItems":"false","Nullable":"disable"}.items():ET.SubElement(props,name).text=value
 items=ET.SubElement(project,"ItemGroup")
 for path in [*(ROOT/path for path in SOURCES[1:]),wrapper,*(Path(__file__).parent/"grid_reference"/name for name in ["Program.cs","Stubs.cs"])]:ET.SubElement(items,"Compile",Include=str(path))
 for name in ["OpenMetaverse","OpenMetaverseTypes","Nini"]:
  item=ET.SubElement(items,"Reference",Include=name);ET.SubElement(item,"HintPath").text=str(ROOT/"bin"/(name+".dll"))
 path=work/"GridReference.csproj";ET.ElementTree(project).write(path,encoding="utf-8",xml_declaration=True)
 result=subprocess.run([dotnet,"build",str(path),"-c","Release","-o",str(work/"app"),"--nologo","-v","quiet"],capture_output=True,text=True)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 return [dotnet,str(work/"app/GridReference.dll")]
def main():
 parser=argparse.ArgumentParser();parser.add_argument("--native",required=True);parser.add_argument("--dotnet",required=True);parser.add_argument("--work",required=True,type=Path);args=parser.parse_args();command=reference(args.dotnet,args.work.resolve());differences=[];observations=0
 def runs(options,data=None):
  results=[]
  for executable in [command,[args.native]]:
   result=subprocess.run(executable+options,input=data,capture_output=True,text=True,encoding="utf-8",timeout=30)
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   results.append(result.stdout.splitlines())
  if len(results[0])!=len(results[1]):raise AssertionError((options,results))
  return results
 for seed in range(32):
  results=runs([str(seed)])
  if len(results[0])!=57:raise AssertionError(("Registry observation count",seed,len(results[0])))
  for index,(expected,actual) in enumerate(zip(*results)):
   observations+=1
   if expected!=actual:differences.append({"seed":seed,"observation":index,"reference":bytes.fromhex(expected).decode(),"native":bytes.fromhex(actual).decode()})
 cases=[{}, {"serverPort":"9000"}];base={"serverPort":"9000"}
 for key,values in {
  "uuid":[None,"","bad","00000000-0000-0000-0000-000000000001"],
  "locX":[None,"0","-1","2147483647","-2147483648","2147483648","  +257 ","+-1","1.0"],
  "locY":["-257","256","999999999999"],"sizeX":[None,"0","-256"," 1024 ","bad"],"sizeY":["4096","-1"],
  "access":[None,"0","13","14","21","22","255","256","-1","bad"],
  "flags":[None,"0","-1","2048","4294967295","bad"],"serverIP":[None,"","localhost","::1","example.invalid"],
  "serverPort":[None,"","bad","-1","65535","65536","2147483648","+9000","+-1"],
  "serverHttpPort":[None,"","0","4294967295","4294967296","-1","+1","+-1","bad"],
  "serverURI":[None,"","http://host","http://host/","arbitrary","/"],
  "regionName":[None,"","Alpha & <Beta>","\u8a66\u9a13"," With Space "],
  "regionMapTexture":[None,"bad","00000000-0000-0000-0000-000000000001"],
  "parcelMapTexture":[None,"bad","00000000-0000-0000-0000-000000000002"],
  "owner_uuid":[None,"bad","00000000-0000-0000-0000-000000000003"],
  "Token":[None,"","secret"],"regionSecret":[None,"","secret"],"unknown":[None,"ignored"]
 }.items():cases.extend({**base,key:value} for value in values)
 lines=["|".join(key.encode().hex()+"="+("N" if value is None else value.encode().hex()) for key,value in case.items()) for case in cases];results=runs(["--model"],"\n".join(lines)+"\n")
 if len(results[0])!=len(cases):raise AssertionError("Model probe length differs")
 differences.extend({"model":case,"reference":bytes.fromhex(expected).decode(),"native":bytes.fromhex(actual).decode()} for case,expected,actual in zip(cases,*results) if expected!=actual)
 report={"modelScenarioCount":len(cases),"passed":not differences,"scenarioCount":32,"observationCount":observations,"sourceMethods":METHODS,"sourceHashes":{path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in SOURCES},"differences":differences};(args.work/"grid-parity-results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
 if differences:raise AssertionError(json.dumps(differences[:5],indent=2))
 print(f"PASS: 32 registry/provider scenarios, {observations} observations and {len(cases)} model cases")
if __name__=="__main__":main()

