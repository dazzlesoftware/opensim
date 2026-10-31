# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Compare original login destination methods and native region selection.

Only destination selection is extracted from LLLoginService. This fixture does
not establish complete login, presence rollback or simulator admission parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_grid import reference as grid_reference, ROOT, SOURCES
from compare_presence_endpoint import extract


def reference(dotnet, work):
    grid_reference(dotnet, work / "core")
    source = (ROOT / "OpenSim/Services/LLLoginService/LLLoginService.cs").read_text(encoding="utf-8-sig")
    methods = [extract(source, r"(?:protected|private) GridRegion " + name + r"\(") for name in ["FindDestination", "FindAlternativeRegion"]]
    regex = re.search(r"private static readonly Regex URIRegex = [^\n]+;", source).group()
    wrapper = work / "DestinationReference.cs"
    wrapper.write_text("""using System;using System.Collections.Generic;using System.Text.RegularExpressions;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;using OpenSim.Services.GridService;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
public class DestinationReference {
readonly GridService m_GridService;readonly bool m_allowLoginFallbackToAnyRegion;
const string LogHeader="[LLOGIN SERVICE]";
static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(DestinationReference));
public DestinationReference(GridService grid,bool allow){m_GridService=grid;m_allowLoginFallbackToAnyRegion=allow;}
public GridRegion Find(UUID scope,GridUserInfo info,string start,GridRegion home,out GridRegion gate,out string where,out Vector3 position,out Vector3 look,out TeleportFlags flags)=>FindDestination(new UserAccount(),scope,info,UUID.Zero,start,home,out gate,out where,out position,out look,out flags);
""" + regex + "\n" + "\n".join(methods) + "}", encoding="utf-8")
    project = ET.parse(work / "core/GridReference.csproj")
    items = project.getroot().find("ItemGroup")
    for item in list(items):
        if item.tag == "Compile" and Path(item.get("Include")).name in ["Program.cs", "Stubs.cs"]:
            items.remove(item)
    stub = work / "Stubs.cs"
    text = (ROOT / "native-port/tests/grid_reference/Stubs.cs").read_text(encoding="utf-8")
    text = text.replace("void Warn(string value);", "void Warn(string value);void Info(string value);void InfoFormat(string value,params object[] args);")
    text = text.replace("public void Warn(string value){}", "public void Warn(string value){}public void Info(string value){}public void InfoFormat(string value,params object[] args){}")
    stub.write_text(text, encoding="utf-8")
    for path in [stub, wrapper, ROOT / "native-port/tests/login_destination_reference/Program.cs", ROOT / "OpenSim/Framework/Culture.cs", ROOT / "OpenSim/Services/Interfaces/IGridUserService.cs"]:
        ET.SubElement(items, "Compile", Include=str(path))
    path = work / "LoginDestinationReference.csproj"
    project.write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return [dotnet, str(work / "app/LoginDestinationReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    differences = []
    count = 0
    for seed in range(32):
        results = []
        for executable in [command, [args.native]]:
            result = subprocess.run(executable + [str(seed)], capture_output=True, text=True, encoding="utf-8", timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            results.append(result.stdout.splitlines())
        if any(len(rows) != 25 for rows in results):
            raise AssertionError(("Destination probe count", seed, [len(rows) for rows in results]))
        for index, (expected, actual) in enumerate(zip(*results)):
            count += 1
            if expected != actual:
                differences.append({"seed": seed, "observation": index, "reference": expected, "native": actual})
    evidence = sorted(set(SOURCES + ["OpenSim/Services/LLLoginService/LLLoginService.cs", "OpenSim/Services/Interfaces/IGridUserService.cs", "OpenSim/Framework/Culture.cs"]))
    report = {"passed": not differences, "scenarioCount": 32, "observationCount": count, "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in evidence}, "typesLibraryHash": hashlib.sha256((ROOT / "bin/OpenMetaverse.dll").read_bytes()).hexdigest(), "differences": differences}
    (work / "login-destination-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    print(f"PASS: {count} login destination/position/flag observations")


if __name__ == "__main__":
    main()
