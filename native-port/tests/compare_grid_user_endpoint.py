"""Compare original handler, service auth, query parser and XML response methods."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.parse
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Server/Handlers/GridUser/GridUserServerPostHandler.cs", "OpenSim/Framework/Servers/HttpServer/BaseStreamHandler.cs", "OpenSim/Framework/ServiceAuth/IServiceAuth.cs", "OpenSim/Framework/ServiceAuth/ServiceAuth.cs", "OpenSim/Framework/ServiceAuth/BasicHttpAuthentication.cs", "OpenSim/Framework/ServiceAuth/DisallowLlHttpRequest.cs", "OpenSim/Framework/ServiceAuth/CompoundAuthentication.cs", "OpenSim/Services/UserAccountService/GridUserService.cs", "OpenSim/Services/UserAccountService/GridUserServiceBase.cs", "OpenSim/Services/Interfaces/IGridUserService.cs", "OpenSim/Data/IGridUserData.cs", "OpenSim/Framework/ExpiringCacheOS.cs"]


def extract(source, pattern):
    match = re.search(pattern, source)
    if not match:
        raise RuntimeError("Cannot find reference method: " + pattern)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "OpenSim/Server/Base/ServerUtils.cs").read_text(encoding="utf-8-sig")
    methods = [extract(source, r"(?:public|private) static [^\n{]+ " + name + r"\(") for name in ["ParseQueryString", "BuildXmlResponse", "BuildXmlData"]]
    server = work / "ServerUtils.cs"
    server.write_text("using System;using System.Collections.Generic;using System.Xml;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base {public static class ServerUtils {" + "\n".join(methods) + "}}")
    source = (ROOT / "OpenSim/Framework/Util.cs").read_text(encoding="utf-8-sig")
    methods = [extract(source, r"public static (?:byte\[\]|string) " + name + r"\(") for name in ["DocToBytes", "Base64ToString"]]
    methods.append(extract(source, r"public static T GetConfigVarFromSections<T>\(IConfigSource config, string varname, string\[\] sections, object val\)"))
    util = work / "Util.cs"
    util.write_text("using System;using System.IO;using System.Text;using System.Xml;using Nini.Config;namespace OpenSim.Framework {public static partial class Util {static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(Util));public static readonly char[] SplitColonArray={':' };public static readonly Encoding UTF8=Encoding.UTF8;public static readonly Encoding UTF8NoBomEncoding=new UTF8Encoding(false);" + "\n".join(methods) + "}}")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    paths = [Path(__file__).parent / "endpoint_reference/Stubs.cs", *(ROOT / path for path in SOURCES), server, util, *(Path(__file__).parent / "grid_user_endpoint_reference" / name for name in ("Program.cs", "Stubs.cs"))]
    for path in paths:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes", "Nini"]:
        dependency = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(dependency, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "EndpointReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/EndpointReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    session = "00000000-0000-0000-0000-000000000001"
    user="00000000-0000-0000-0000-000000000001"
    region="00000000-0000-0000-0000-000000000003"
    bodies=["", "METHOD=unknown", "METHOD[]=loggedin", "METHOD=loggedin", "METHOD=loggedin&UserID="+user, "METHOD=loggedin&UserID=new", "METHOD=loggedin&UserID[]=list", "METHOD=getgriduserinfo", "METHOD=getgriduserinfo&UserID="+user, "METHOD=getgriduserinfo&UserID=missing", "METHOD=getgriduserinfos", "METHOD=getgriduserinfos&AgentIDs=bad", "METHOD=getgriduserinfos&AgentIDs[]=missing", "METHOD=getgriduserinfos&AgentIDs[]="+user+"&AgentIDs[]=missing&AgentIDs[]="+user]
    bodies.extend(["METHOD=getgriduserinfo&UserID[]=list", "METHOD=loggedin&UserID=", "METHOD=getgriduserinfos&AgentIDs[]=", "METHOD=getgriduserinfo&UserID="+user+";http://foreign.invalid/;name"])
    bodies.extend(["METHOD=getgriduserinfo&UserID=00000000-0000-0000-0000-000000000002", "METHOD=getgriduserinfos&AgentIDs[]=00000000-0000-0000-0000-000000000002&AgentIDs[]="+user])
    for method in ["loggedout", "sethome", "setposition"]:
        for suffix in ["", "&UserID="+user, "&UserID="+user+"&RegionID=bad", "&UserID="+user+"&RegionID="+region, "&UserID="+user+"&RegionID="+region+"&Position=<1,2,3>&LookAt=bad", "&UserID="+user+"&RegionID="+region+"&Position=bad&LookAt=<4,5,6>", "&UserID[]=list&RegionID="+region]:
            bodies.append("METHOD="+method+suffix)
        for value in (" " + region + " ", region.replace("-", ""), "{" + region + "}", region + " extra"):
            bodies.append(urllib.parse.urlencode({"METHOD": method, "UserID": user, "RegionID": value}))
    modes = [("none", "", False), ("basic", "", False), ("basic", "Basic "+base64.b64encode(b"u:p").decode(), False), ("basic", " Basic "+base64.b64encode(b"u:p:ignored").decode()+" ", False), ("basic", "basic dTpw", False), ("basic", "Basic invalid!", False), ("none", "", True), ("allow", "Basic dTpw", True)]
    encoded_lookup = "METHOD=getgriduserinfo&UserID=" + user
    bodies = [body.encode() for body in bodies]
    bodies.extend(encoded_lookup.encode(encoding) for encoding in ("utf-8-sig", "utf-16", "utf-32"))
    differences = []
    count = 0
    for index, (mode, authorization, shard) in enumerate(modes):
        config = work / f"config-{index}.ini"
        config.write_text("[GridUserService]\n" + ("AuthType=BasicHttpAuthentication\nHttpAuthUsername=u\nHttpAuthPassword=p\n" if mode != "none" else "") + ("AllowllHTTPRequestIn=true\n" if mode == "allow" else ""))
        lines = [body.hex()+"|"+authorization.encode().hex()+"|"+str(int(shard)) for body in bodies]
        data = "\n".join(lines)+"\n"
        results = []
        for executable in [command, [args.native]]:
            run = subprocess.run(executable+[str(config)], input=data, capture_output=True, text=True, encoding="utf-8", timeout=60)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            results.append(run.stdout.splitlines())
        if any(len(rows) != len(lines) for rows in results):
            raise AssertionError("Endpoint output length differs")
        differences.extend({"mode": index, "body": repr(body), "reference": expected, "native": actual} for body, expected, actual in zip(bodies, *results) if expected != actual)
        count += len(lines)
    report = {"passed": not differences, "scenarioCount": count, "sourceHashes": {path: hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in SOURCES+["OpenSim/Server/Base/ServerUtils.cs", "OpenSim/Framework/Util.cs"]}, "differences": differences}
    (work / "grid-user-endpoint-parity-results.json").write_text(json.dumps(report, indent=2)+"\n")
    if differences:
        raise AssertionError(json.dumps(differences[:3], indent=2))
    print(f"PASS: {count} grid-user endpoint/auth/form/XML observations")


if __name__ == "__main__":
    main()
