"""Compare actual launch/retry methods with native callback admission.

MakeAgent and simulation/user-agent transports are instrumented dependencies.
The only edit inside the extracted methods replaces Random.Shared.Next with a
deterministic supplier; circuit allocation/order and every retry remain intact.
This fixture does not establish scene, circuit serialization or viewer readiness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_grid import reference, ROOT, SOURCES
from compare_presence_endpoint import extract


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    reference(args.dotnet, work / "core")
    source = (ROOT / "OpenSim/Services/LLLoginService/LLLoginService.cs").read_text(encoding="utf-8-sig")
    names = ["LaunchAgentAtGrid", "LaunchAgentDirectly", "LaunchAgentIndirectly", "SetHostAndPort"]
    methods = [extract(source, r"(?:protected|private) [^\n{]+ " + name + r"\(") for name in names]
    wrapper = work / "LaunchReference.cs"
    wrapper.write_text("""using System;using System.Collections.Generic;using System.Net;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;using OpenSim.Services.GridService;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
public partial class LaunchReference {
GridService m_GridService;ISimulationService m_LocalSimulationService,m_RemoteSimulationService;IUserAgentService m_UserAgentService;string m_GatekeeperURL,hostName=string.Empty;int port=0;
static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(LaunchReference));
""" + "\n".join(methods).replace("(uint)Random.Shared.Next()", "Code()") + "}", encoding="utf-8")
    stubs = work / "Stubs.cs"
    text = (ROOT / "native-port/tests/grid_reference/Stubs.cs").read_text(encoding="utf-8")
    text = text.replace("void Warn(string value);", "void Warn(string value);void Debug(string value);")
    text = text.replace("public void Warn(string value){}", "public void Warn(string value){}public void Debug(string value){}")
    version_source = (ROOT / "OpenSim/Framework/VersionInfo.cs").read_text(encoding="utf-8-sig")
    versions = [re.search(r"public readonly static float " + name + r" = [^;]+;", version_source).group() for name in ["SimulationServiceVersionAcceptedMax", "SimulationServiceVersionSupportedMax"]]
    stubs.write_text(text + "\nnamespace OpenSim.Framework {public class VersionInfo {" + "\n".join(versions) + "}}", encoding="utf-8")
    project = ET.parse(work / "core/GridReference.csproj")
    items = project.getroot().find("ItemGroup")
    for item in list(items):
        if item.tag == "Compile" and Path(item.get("Include")).name in ["Program.cs", "Stubs.cs"]:
            items.remove(item)
    for path in [wrapper, stubs, ROOT / "native-port/tests/agent_launch_reference.cs", ROOT / "OpenSim/Framework/Culture.cs", ROOT / "OpenSim/Framework/EntityTransferContext.cs"]:
        ET.SubElement(items, "Compile", Include=str(path))
    if not any(item.tag == "Reference" and item.get("Include") == "OpenMetaverse.StructuredData" for item in items):
        item = ET.SubElement(items, "Reference", Include="OpenMetaverse.StructuredData")
        ET.SubElement(item, "HintPath").text = str(ROOT / "bin/OpenMetaverse.StructuredData.dll")
    path = work / "AgentLaunchReference.csproj"
    project.write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    differences = []
    coverage = {name: 0 for name in ["directFallbackSuccess", "indirectFallbackSuccess", "retryExhaustion", "missingConnector", "localPreferred", "dependencyExceptions", "providedGatekeeper", "ipv6Gatekeeper", "endpointAliasMutation"]}
    for seed in range(160):
        rows = []
        for command in [[args.dotnet, str(work / "app/AgentLaunchReference.dll"), str(seed)], [args.native, str(seed)]]:
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            rows.append(result.stdout.strip())
        if rows[0] != rows[1]:
            differences.append({"seed": seed, "source": rows[0], "native": rows[1]})
        expected = rows[0]
        if expected.startswith("OK|") and "|safe|" in expected:
            coverage["indirectFallbackSuccess" if ";hg:" in expected else "directFallbackSuccess"] += 1
        attempts = expected.count("-query:") + expected.count(";hg:")
        if expected.startswith("FAIL|") and attempts > 1:
            coverage["retryExhaustion"] += 1
        if seed % 5 == 0 and expected.endswith("|0|"):
            coverage["missingConnector"] += 1
        if seed % 5 == 3 and "local-query:" in expected and "remote-query:" not in expected:
            coverage["localPreferred"] += 1
        if expected.startswith("EX|"):
            coverage["dependencyExceptions"] += 1
        if "gate-endpoint:" in expected:
            coverage["endpointAliasMutation"] += 1
        for name, token in [("providedGatekeeper", "provided.example:9001:"), ("ipv6Gatekeeper", "[::1]:8443:")]:
            if token in expected:
                coverage[name] += 1
    if any(value == 0 for value in coverage.values()):
        raise AssertionError(("Missing launch branch evidence", coverage))
    paths = SOURCES + ["OpenSim/Services/LLLoginService/LLLoginService.cs", "OpenSim/Framework/VersionInfo.cs", "OpenSim/Framework/EntityTransferContext.cs"]
    report = {"passed": not differences, "scenarios": 160, "coverage": coverage, "sourceHashes": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths}, "extractedMethodHashes": {name: hashlib.sha256(method.encode()).hexdigest() for name, method in zip(names, methods)}, "deterministicRandomReplacement": "(uint)Random.Shared.Next() -> Code()", "differences": differences}
    (work / "agent-launch-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:8], indent=2))
    print("PASS: 160 source-backed agent launch and fallback traces")


if __name__ == "__main__":
    main()
