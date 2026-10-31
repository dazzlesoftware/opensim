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
"""Compare actual GridInfo configuration, identity and property state."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import extract
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument("--native",required=True);p.add_argument("--dotnet",required=True);p.add_argument("--work",required=True,type=Path);args=p.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
 source="OpenSim/Framework/GridInfo.cs";util_source=(ROOT/"OpenSim/Framework/Util.cs").read_text(encoding="utf-8-sig");methods=[extract(util_source,r"public static T GetConfigVarFromSections<T>\(IConfigSource config, string varname, string\[\] sections"+suffix) for suffix in [r"\)",r", object val\)"]];util=work/"Util.cs";util.write_text("using System;using Nini.Config;namespace OpenSim.Framework {public static partial class Util {"+"\n".join(methods)+"}}",encoding="utf-8")
 project=ET.Element("Project",Sdk="Microsoft.NET.Sdk");props=ET.SubElement(project,"PropertyGroup")
 for name,value in {"TargetFramework":"net8.0","OutputType":"Exe","EnableDefaultCompileItems":"false","Nullable":"disable"}.items():ET.SubElement(props,name).text=value
 items=ET.SubElement(project,"ItemGroup")
 for path in [ROOT/source,util,ROOT/"native-port/tests/grid_identity_reference/Program.cs"]:ET.SubElement(items,"Compile",Include=str(path))
 ref=ET.SubElement(items,"Reference",Include="Nini");ET.SubElement(ref,"HintPath").text=str(ROOT/"bin/Nini.dll")
 path=work/"GridIdentityReference.csproj";ET.ElementTree(project).write(path,encoding="utf-8",xml_declaration=True);build=subprocess.run([args.dotnet,"build",str(path),"-c","Release","-o",str(work/"app"),"--nologo","-v","quiet"],capture_output=True,text=True)
 if build.returncode:raise RuntimeError(build.stdout+build.stderr)
 profiles=[("",None),("", ""),("","https://default.example:9000"),("[Const]\nGatekeeperURI=http://gate.example\n",None),("[GatekeeperService]\nExternalName=http://gate.example\n",None),("[GridService]\nGatekeeper=http://gate.example\n",None),("[Const]\nHomeURI=http://home.example\n",None),("[Const]\nHomeURI=http://bad.invalid\n",None),("[Const]\nGatekeeperURI=bad/path\n",None),("[Const]\nGatekeeperURI=http://bad.invalid\n",None),("[Const]\nGatekeeperURI=http://gate.example\nHomeURI=http://bad.invalid\n",None),("[Const]\nGatekeeperURI=http://gate.example\nHomeURI=https://home.example:8002\nGatekeeperURIAlias=alias.example,https://alias.example:80, bad/path\nHomeURIAlias=alias.example\nGridName=Const\nGridNick=CN\n[GridInfo]\nGridName=Info\nGridNick=IN\n[SimulatorFeatures]\nGridName=Features\nGridNick=FN\nStunServers=stun.example:3478,stun.example:3478,bad/path\n",None)]
 for key,value in [("SearchServerURI","http://search.example/find?q=1"),("SearchServerURI","bad/path"),("SearchServerURI","http://bad.invalid"),("DestinationGuideURI","https://guide.example/path"),("DestinationGuide","http://guide.example/"),("economy","http://economy.example/pay")]:profiles.append(("[GridInfo]\n"+key+"="+value+"\n",None))
 profiles.append(("[Const]\nGatekeeperURI=first.example\n[Startup]\nGatekeeperURI=second.example\n[Hypergrid]\nGatekeeperURI=gate.example\nHomeURI=home.example\n",None))
 differences=[];count=0
 for index,(profile,fallback) in enumerate(profiles):
  config=work/f"profile-{index}.ini";config.write_text(profile,encoding="utf-8")
  for action in range(4):
   outputs=[]
   for executable in [[args.dotnet,str(work/"app/GridIdentityReference.dll")],[args.native]]:
    run=subprocess.run(executable+[str(config),"N" if fallback is None else fallback.encode().hex(),str(action)],capture_output=True,text=True,encoding="utf-8",timeout=30)
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    outputs.append(run.stdout.strip())
   count+=1
   if outputs[0]!=outputs[1]:differences.append({"profile":index,"action":action,"reference":outputs[0],"native":outputs[1]})
 (work/"grid-identity-parity-results.json").write_text(json.dumps({"passed":not differences,"scenarioCount":count,"sourceHash":hashlib.sha256((ROOT/source).read_bytes()).hexdigest(),"utilityHash":hashlib.sha256((ROOT/"OpenSim/Framework/Util.cs").read_bytes()).hexdigest(),"configLibraryHash":hashlib.sha256((ROOT/"bin/Nini.dll").read_bytes()).hexdigest(),"differences":differences},indent=2)+"\n",encoding="utf-8")
 if differences:raise AssertionError(json.dumps(differences[:3],indent=2))
 print(f"PASS: {count} grid identity configuration/state observations")
if __name__=="__main__":main()
