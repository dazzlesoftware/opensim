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
"""Compare the actual grid HTTP handler with native dispatch and registry core."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.parse
import xml.etree.ElementTree as ET
from compare_grid import reference as grid_reference, ROOT, SOURCES as GRID_SOURCES
from compare_presence_endpoint import reference as helpers_reference, extract, SOURCES as AUTH_SOURCES
SOURCES=["OpenSim/Server/Handlers/Grid/GridServerPostHandler.cs","OpenSim/Server/Base/ProtocolVersions.cs"]
def reference(dotnet,work):
 grid_reference(dotnet,work/"core")
 helpers_reference(dotnet,work/"helpers")
 source=work/"core/GridService.cs";text=source.read_text(encoding="utf-8").replace("public class GridService {","public class GridService : IGridService {public Dictionary<string,object> GetExtraFeatures()=>new(){{\"ExportSupported\",\"true\"}};")
 source.write_text(text,encoding="utf-8")
 extra=work/"ExtraFeatures.cs";original=(ROOT/"OpenSim/Services/GridService/GridService.cs").read_text(encoding="utf-8-sig");method=extract(original,r"private void SetExtraServiceURLs\(")
 extra.write_text("using System;using System.Text;using System.Collections.Generic;using Nini.Config;using OpenSim.Framework;namespace OpenSim.Framework {public static class osStringBuilderCache {public static StringBuilder Acquire()=>new();public static string GetStringAndRelease(StringBuilder value)=>value.ToString();public static void Release(StringBuilder value){}}}public class ExtraFeatures {Dictionary<string,object> m_ExtraFeatures=new();public ExtraFeatures(IConfigSource config){if(config.Configs[\"GridService\"]?.GetBoolean(\"SuppressConsoleCommands\",false)!=true)SetExtraServiceURLs(config);}public Dictionary<string,object> Values=>m_ExtraFeatures;"+method+"}",encoding="utf-8")
 project=ET.parse(work/"core/GridReference.csproj");items=project.getroot().find("ItemGroup")
 for item in list(items):
  if item.tag=="Compile" and Path(item.get("Include")).name in ["Program.cs","Stubs.cs"]:items.remove(item)
 stub=work/"GridStubs.cs";text=(ROOT/"native-port/tests/grid_reference/Stubs.cs").read_text(encoding="utf-8").replace("public static class Util {","public static partial class Util {").replace("void Warn(string value);","void Warn(string value);void ErrorFormat(string value,params object[] args);").replace("public void Warn(string value){}","public void Warn(string value){}public void ErrorFormat(string value,params object[] args){}")
 stub.write_text(text,encoding="utf-8")
 host=work/"HttpHost.cs";original_host=(ROOT/"OpenSim/Framework/GridInfo.cs").read_text(encoding="utf-8-sig");host.write_text("using System;using System.Net;namespace OpenSim.Framework {"+extract(original_host,r"public enum OSHTTPURIFlags")+extract(original_host,r"public struct OSHHTPHost")+"}",encoding="utf-8")
 paths=[host,extra,stub,ROOT/"native-port/tests/grid_endpoint_reference/Program.cs",ROOT/"native-port/tests/endpoint_reference/Stubs.cs",work/"helpers/ServerUtils.cs",work/"helpers/Util.cs",*(ROOT/path for path in SOURCES)]
 paths.extend(ROOT/path for path in ["OpenSim/Framework/Servers/HttpServer/BaseStreamHandler.cs","OpenSim/Framework/ServiceAuth/IServiceAuth.cs","OpenSim/Framework/ServiceAuth/ServiceAuth.cs","OpenSim/Framework/ServiceAuth/BasicHttpAuthentication.cs","OpenSim/Framework/ServiceAuth/DisallowLlHttpRequest.cs","OpenSim/Framework/ServiceAuth/CompoundAuthentication.cs"])
 for path in paths:ET.SubElement(items,"Compile",Include=str(path))
 path=work/"GridEndpointReference.csproj";project.write(path,encoding="utf-8",xml_declaration=True)
 result=subprocess.run([dotnet,"build",str(path),"-c","Release","-o",str(work/"app"),"--nologo","-v","quiet"],capture_output=True,text=True)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 return [dotnet,str(work/"app/GridEndpointReference.dll")]
def main():
 p=argparse.ArgumentParser();p.add_argument("--native",required=True);p.add_argument("--dotnet",required=True);p.add_argument("--work",type=Path,required=True);args=p.parse_args();work=args.work.resolve();command=reference(args.dotnet,work)
 first="00000000-0000-0000-0000-000000000001"
 bodies=["","METHOD=unknown","METHOD[]=register","METHOD=register","METHOD=register&VERSIONMIN=1&VERSIONMAX=1","METHOD=register&VERSIONMIN=2&VERSIONMAX=2"]
 registration={"METHOD":"register","VERSIONMIN":"1","VERSIONMAX":"1","uuid":"00000000-0000-0000-0000-000000000003","locX":"30000","locY":"30000","sizeX":"256","sizeY":"512","serverPort":"9000","regionName":"Gamma <&>"}
 bodies.append(urllib.parse.urlencode(registration))
 for key,value in [("uuid","bad"),("regionName",""),("locY","0"),("locX","25600"),("VERSIONMAX","bad"),("VERSIONMIN","-1")]:bodies.append(urllib.parse.urlencode({**registration,key:value}))
 for method in ["deregister","get_neighbours","get_region_by_uuid","get_region_by_position","get_region_by_name","get_localregion_by_name","get_regions_by_name","get_region_range","get_default_regions","get_default_hypergrid_regions","get_fallback_regions","get_online_regions","get_hyperlinks","get_region_flags","get_grid_extra_features"]:
  for tail in ["","&REGIONID="+first+"&X=25601&Y=25600&NAME=Alpha&MAX=2&MC=2&XMIN=25500&XMAX=26000&YMIN=25500&YMAX=26000","&SCOPEID=bad&REGIONID=bad&NAME[]=Alpha&MAX=bad&MC=-1"]:bodies.append("METHOD="+method+tail)
 differences=[];count=0
 for index,(basic,auth,shard,allow) in enumerate([(False,"",False,False),(True,"",False,False),(True,"Basic dTpw",False,False),(True,"Basic dTpwOmV4dHJh",False,False),(True,"basic dTpw",False,False),(True,"Basic invalid!",False,False),(False,"",True,False),(False,"",True,True)]):
  config=work/f"config-{index}.ini";config.write_text("[GridService]\n"+("AuthType=BasicHttpAuthentication\nHttpAuthUsername=u\nHttpAuthPassword=p\n" if basic else "")+("AllowllHTTPRequestIn=true\n" if allow else ""),encoding="utf-8")
  data="\n".join(body.encode().hex()+"|"+auth.encode().hex()+"|"+str(int(shard)) for body in bodies)+"\n";outputs=[]
  for executable in [command,[args.native]]:
   run=subprocess.run(executable+[str(config)],input=data,capture_output=True,text=True,encoding="utf-8",timeout=60)
   if run.returncode:raise RuntimeError(run.stdout+run.stderr)
   outputs.append(run.stdout.splitlines())
  if any(len(rows)!=len(bodies) for rows in outputs):raise AssertionError("Grid endpoint probe length differs")
  differences.extend({"mode":index,"body":body,"reference":expected,"native":actual} for body,expected,actual in zip(bodies,*outputs) if expected!=actual);count+=len(bodies)
 feature_profiles=["[GridService]\n", "[GridService]\n[LoginService]\n", "[GridService]\nExportSupported=false\n[LoginService]\nSearchURL=http://search/\nMapTileURL=http://map\nDestinationGuide=http://guide/\n", "[GridService]\n[LoginService]\n[Const]\nGridName=Const\nGridNick=first\n[Hypergrid]\nGridName=Hyper\nGridNick=second\nGatekeeperURI=http://hg/\n[GridInfoService]\nGridStatus=http://status/\nGridStatusRSS=http://rss/\n", "[GridService]\nSuppressConsoleCommands=true\n[LoginService]\nSearchURL=http://ignored/\n", "[GridService]\n[LoginService]\nMapTileURL=\"   \"\n[GridInfo]\ngridname=first\ngridnick=first\n[GridInfoService]\ngridname=last\ngridnick=last\n"]
 for aliases in ["grid.example","HTTP://GRID.EXAMPLE/","https://grid.example/","localhost:9000","first.example,second.example","first.example, bad.example","bad/path,good.example","good.example,bad/path","bad/path,also/bad","https://grid.example:0","grid.example:65536","httpXxxgrid.example","grid.example:-1","grid.example:+80"]:
  feature_profiles.append("[GridService]\n[LoginService]\n[GatekeeperService]\nGatekeeperURIAlias="+aliases+"\n")
 feature_profiles.append("[GridService]\n[LoginService]\n[Const, Startup]\nGatekeeperURIAlias=first.example\n[Hypergrid]\nGatekeeperURIAlias=second.example\n[GatekeeperService]\nGatekeeperURIAlias=last.example\n")
 for index,profile in enumerate(feature_profiles):
  config=work/f"features-{index}.ini";config.write_text(profile,encoding="utf-8");outputs=[]
  for executable in [command,[args.native]]:
   run=subprocess.run(executable+[str(config),"--features"],capture_output=True,text=True,encoding="utf-8",timeout=30)
   if run.returncode:raise RuntimeError(run.stdout+run.stderr)
   outputs.append(run.stdout.strip())
  if outputs[0]!=outputs[1]:differences.append({"featureProfile":index,"reference":outputs[0],"native":outputs[1]})
 report={"passed":not differences,"scenarioCount":count,"featureProfiles":len(feature_profiles),"sourceHashes":{path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in sorted(set(SOURCES+GRID_SOURCES+AUTH_SOURCES+["OpenSim/Framework/Util.cs","OpenSim/Framework/GridInfo.cs","OpenSim/Server/Base/ServerUtils.cs"]))},"differences":differences};(work/"grid-endpoint-parity-results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
 if differences:raise AssertionError(json.dumps(differences[:3],indent=2))
 print(f"PASS: {count} grid endpoint dispatch/auth/XML observations")
if __name__=="__main__":main()
