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
"""Compare native HTTP URI normalization with original OSHTTPURI/System.Uri."""
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
 source="OpenSim/Framework/GridInfo.cs";original=(ROOT/source).read_text(encoding="utf-8-sig");generated=work/"HttpUri.cs";generated.write_text("using System;using System.Net;namespace OpenSim.Framework {"+extract(original,r"public enum OSHTTPURIFlags")+extract(original,r"public struct OSHTTPURI:")+"}",encoding="utf-8")
 project=ET.Element("Project",Sdk="Microsoft.NET.Sdk");props=ET.SubElement(project,"PropertyGroup")
 for name,value in {"TargetFramework":"net8.0","OutputType":"Exe","EnableDefaultCompileItems":"false","Nullable":"disable"}.items():ET.SubElement(props,name).text=value
 items=ET.SubElement(project,"ItemGroup")
 for path in [generated,ROOT/"native-port/tests/http_uri_reference/Program.cs"]:ET.SubElement(items,"Compile",Include=str(path))
 path=work/"HttpUriReference.csproj";ET.ElementTree(project).write(path,encoding="utf-8",xml_declaration=True)
 build=subprocess.run([args.dotnet,"build",str(path),"-c","Release","-o",str(work/"app"),"--nologo","-v","quiet"],capture_output=True,text=True)
 if build.returncode:raise RuntimeError(build.stdout+build.stderr)
 values=[None,""," ","http://host","HTTP://HOST/","https://host","http://host:80/a","https://host:443/a","http://host:0/a","http://host:65535/a","http://host:65536/a","http://host:/a","http://host:+80/a","http://host:-0/a","http://host/a/b/../c/./","http://host/a//b/","http://host/../a","http://host/a/.","http://host/a/..","http://host/%2e%2e/a","http://host/%41%7e%2f%FF","http://host/a b?x=a b#more text","http://user:pass@host/a?x=1#fragment","http://u@v@host/","http://127.1/a","http://0x7f000001/a","http://[::1]/a","http://[2001:0db8::1]:9000/a","http://[::ffff:127.0.0.1]/a","http://_host/a","http://bad-.example/a","http://-bad.example/a","http://host./a","http://foo..bar/a","http://bad.invalid/a","ftp://host/a","host/a","/a","https:host/a","http://host/a%bad%ZZ%","http://host/a\\b","http://host/?q=%41%7e%2f%FF","http://host/a[]{}|^`<>","http://host/\u8a66\u9a13","http://\u00e9.example/\u8a66\u9a13"," http://host/a ","http://host/?","http://host/#"]
 cases=[(mode,value,"") for mode in [0,1,2] for value in values]
 cases.extend((3,left,right) for left,right in [("http://host:443/a","https://host/a"),("http://host/a?x=1","http://host/a?x=2"),("http://host/A","http://host/a"),("http://host/a","http://other/a"),(None,None)])
 encode=lambda value:"N" if value is None else value.encode().hex()
 data="\n".join(str(mode)+"|"+encode(value)+"|"+encode(other) for mode,value,other in cases)+"\n";outputs=[]
 for executable in [[args.dotnet,str(work/"app/HttpUriReference.dll")],[args.native]]:
  run=subprocess.run(executable,input=data,capture_output=True,text=True,encoding="utf-8",timeout=30)
  if run.returncode:raise RuntimeError(run.stdout+run.stderr)
  outputs.append(run.stdout.splitlines())
 if any(len(output)!=len(cases) for output in outputs):raise AssertionError("HTTP URI probe length differs")
 differences=[{"input":case,"reference":bytes.fromhex(expected).decode(),"native":bytes.fromhex(actual).decode()} for case,expected,actual in zip(cases,*outputs) if expected!=actual]
 (work/"http-uri-parity-results.json").write_text(json.dumps({"passed":not differences,"scenarioCount":len(cases),"sourceHash":hashlib.sha256((ROOT/source).read_bytes()).hexdigest(),"differences":differences},indent=2)+"\n",encoding="utf-8")
 if differences:raise AssertionError(json.dumps(differences[:8],indent=2))
 print(f"PASS: {len(cases)} HTTP URI normalization/state observations")
if __name__=="__main__":main()
