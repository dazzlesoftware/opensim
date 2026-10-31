"""Compare original post-authentication control flow with native preparation.

Inventory/presence/recovery failures use instrumented dependencies. Actual
destination and grid methods are compiled from source. This is preparation
evidence; IM transport, simulator admission and successful login are excluded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_login_destination import reference, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    reference(args.dotnet, work)
    wrapper = work / "DestinationReference.cs"
    wrapper.write_text(wrapper.read_text(encoding="utf-8").replace("public class DestinationReference", "public partial class DestinationReference"), encoding="utf-8")
    source_path = ROOT / "OpenSim/Services/LLLoginService/LLLoginService.cs"
    source = source_path.read_text(encoding="utf-8-sig")
    start = source.index("GridUserInfo guinfo = m_GridUserService.GetGridUserInfo(PrincipalIDstr);")
    end = source.index("string reason = string.Empty;", start)
    block = source[start:end]
    generated = work / "Preparation.cs"
    generated.write_text("""using System;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;using System.Collections.Generic;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
public partial class DestinationReference {
public string Prepare(UserAccount account,string startLocation){
UUID scopeID=Id(8),session=Id(10),secureSession=Id(11);bool success=false;
string PrincipalIDstr=account.PrincipalID.ToString(),firstName=account.FirstName,lastName=account.LastName;
try {
""" + block + """
return "READY|"+destination.RegionID+"|"+(uint)flags+"|"+inventorySkel.Count+"|"+(avatar==null?0:1);
}catch{m_PresenceService?.LogoutAgent(session);return "InternalError";}
}}
""", encoding="utf-8")
    project = ET.parse(work / "LoginDestinationReference.csproj")
    items = project.getroot().find("ItemGroup")
    for item in list(items):
        if item.tag == "Compile" and Path(item.get("Include")).name == "Program.cs":
            items.remove(item)
    for path in [generated, ROOT / "native-port/tests/login_orchestration_reference.cs"]:
        ET.SubElement(items, "Compile", Include=str(path))
    path = work / "PreparationReference.csproj"
    project.write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-o", str(work / "prepared"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    differences = []
    for seed in range(512):
        config = work / "case.ini"
        config.write_text(f"[LoginService]\nRequireInventory={'false' if seed & 1 else 'true'}\nAllowLoginFallbackToAnyRegion={'false' if seed & 32 else 'true'}\n[PresenceService]\nAllowDuplicatePresences={'true' if seed & 16 else 'false'}\n", encoding="utf-8")
        commands = [[args.dotnet, str(work / "prepared/PreparationReference.dll"), str(seed)], [args.native, str(seed), str(config)]]
        rows = []
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            rows.append(result.stdout.strip())
        if rows[0] != rows[1]:
            differences.append({"seed": seed, "source": rows[0], "native": rows[1]})
    report = {"passed": not differences, "scenarios": 512, "sourceHash": hashlib.sha256(source_path.read_bytes()).hexdigest(), "extractedBlockHash": hashlib.sha256(block.encode()).hexdigest(), "differences": differences}
    (work / "login-orchestration-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:8], indent=2))
    print("PASS: 512 source-backed login preparation and cleanup traces")


if __name__ == "__main__":
    main()
