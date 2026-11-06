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
SOURCES = ["OpenSim/Server/Handlers/Presence/PresenceServerPostHandler.cs", "OpenSim/Framework/Servers/HttpServer/BaseStreamHandler.cs", "OpenSim/Framework/ServiceAuth/IServiceAuth.cs", "OpenSim/Framework/ServiceAuth/ServiceAuth.cs", "OpenSim/Framework/ServiceAuth/BasicHttpAuthentication.cs", "OpenSim/Framework/ServiceAuth/DisallowLlHttpRequest.cs", "OpenSim/Framework/ServiceAuth/CompoundAuthentication.cs", "OpenSim/Services/PresenceService/PresenceService.cs", "OpenSim/Services/PresenceService/PresenceServiceBase.cs", "OpenSim/Services/Interfaces/IPresenceService.cs", "OpenSim/Data/IPresenceData.cs", "OpenSim/Data/Null/NullPresenceData.cs", "OpenSim/Framework/ExpiringCacheOS.cs"]


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
    paths = [*(ROOT / path for path in SOURCES), server, util, Path(__file__).parent / "presence_reference/Stubs.cs", *(Path(__file__).parent / "endpoint_reference" / name for name in ("Program.cs", "Stubs.cs"))]
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
    bodies = ["", "METHOD=login&UserID=remote&SessionID="+session, "METHOD=unknown", "METHOD[]=getagent", "METHOD=getagent&SessionID=bad", "METHOD=getagent&SessionID="+session, "METHOD=getagent&SessionID=00000000-0000-0000-0000-000000000002", "METHOD=getagent&SessionID=00000000-0000-0000-0000-000000000099", "METHOD=getagents&uuids=alpha", "METHOD=getagents&uuids[]=alpha&uuids[]=missing&uuids[]=alpha", "METHOD=getagents&uuids[]=missing", "METHOD=report&SessionID="+session+"&RegionID=00000000-0000-0000-0000-000000000099", "METHOD=logout&SessionID="+session, "METHOD=logoutregion&RegionID=00000000-0000-0000-0000-000000000003", "METHOD=getagent=ignored&METHOD=logout&SessionID="+session, "METHOD=getagents&uuids[]=alpha&uuids=ignored&uuids[]=alpha", "METHOD=getagents&uuids%5b%5d=%u0061lpha", "METHOD=getagents&uuids[]=%FF"]
    bodies.append("METHOD=getagents&uuids[]="+urllib.parse.quote("beta <&> \r\n試験"))
    modes = [("none", "", False), ("basic", "", False), ("basic", "Basic "+base64.b64encode(b"u:p").decode(), False), ("basic", " Basic "+base64.b64encode(b"u:p:ignored").decode()+" ", False), ("basic", "basic dTpw", False), ("basic", "Basic invalid!", False), ("none", "", True), ("allow", "Basic dTpw", True)]
    encoded_lookup = "METHOD=getagent&SessionID=" + session
    bodies = [body.encode() for body in bodies]
    bodies.extend(encoded_lookup.encode(encoding) for encoding in ("utf-8-sig", "utf-16", "utf-32"))
    differences = []
    count = 0
    for index, (mode, authorization, shard) in enumerate(modes):
        config = work / f"config-{index}.ini"
        config.write_text("[PresenceService]\n" + ("AuthType=BasicHttpAuthentication\nHttpAuthUsername=u\nHttpAuthPassword=p\n" if mode != "none" else "") + ("AllowllHTTPRequestIn=true\n" if mode == "allow" else ""))
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
    forms = ["", "&&", "=empty&%20=space", "x", "x=", "x=   ", "x=+", "x=%20", "x=one=two", "x=a&x=b", "x[]=a&x[]=b", "x=a&x[]=b", "x[]=a&x=b&x[]=c", "[]=ignored", "%5b%5d=ignored", "x=%u0061", "x=%uD83D%uDE00", "x=%uD800", "x=%FF", "x=%E2%82", "x=%ED%A0%80", "x=%F0%80%80%80", "x=%2B+%26%3D", "x=%bad%ZZ%", "x=試験&%u540D=%u5024", "x%5b%5d=%u0061&x[]=", "a=1&b[]=2&b[]=3& c =4"]
    lines = "\n".join(form.encode().hex() for form in forms)+"\n"
    results = []
    for executable in [command, [args.native]]:
        run = subprocess.run(executable+[str(config), "--forms"], input=lines, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        results.append(run.stdout.splitlines())
    if any(len(rows) != len(forms) for rows in results):
        raise AssertionError("Form probe length differs")
    differences.extend({"form": form, "reference": expected, "native": actual} for form, expected, actual in zip(forms, *results) if expected != actual)
    report = {"passed": not differences, "scenarioCount": count, "formScenarioCount": len(forms), "sourceHashes": {path: hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in SOURCES+["OpenSim/Server/Base/ServerUtils.cs", "OpenSim/Framework/Util.cs"]}, "differences": differences}
    (work / "presence-endpoint-parity-results.json").write_text(json.dumps(report, indent=2)+"\n")
    if differences:
        raise AssertionError(json.dumps(differences[:3], indent=2))
    print(f"PASS: {count} presence endpoint/auth/form/XML observations")


if __name__ == "__main__":
    main()
