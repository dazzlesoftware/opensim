"""Compare pending admission with original handler, packet mapping, and cache.

Packet decoding and scene admission are controlled. Timer delivery and time are
explicit; the original ExpiringCacheOS and extracted handler/mapping are unchanged.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from compare_primitive_parameter_query import ROOT, method


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    server_path = ROOT / "OpenSim/Region/ClientStack/Linden/UDP/LLUDPServer.cs"
    cache_path = ROOT / "OpenSim/Framework/ExpiringCacheOS.cs"
    fixture_path = Path(__file__).with_name("pending_admission_reference.cs")
    original = server_path.read_text(encoding="utf-8-sig")
    handler = method(original, "protected void HandleUseCircuitCode(object o)")
    mapping = original.split("#region Packet to Client Mapping", 1)[1].split(
        "#endregion Packet to Client Mapping", 1
    )[0]
    template = fixture_path.read_text()
    source = template.replace("/*HANDLER*/", handler).replace("/*PACKET_MAPPING*/", mapping).replace(
        "/*ADD_CLIENT*/", method(original, "protected virtual IClientAPI AddClient(")
    )
    (work / "Program.cs").write_text(source)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {
        "TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable", "NoWarn": "0436",
    }.items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    ET.SubElement(items, "Compile", Include=str(cache_path))
    project_path = work / "Reference.csproj"
    ET.ElementTree(project).write(project_path)
    built = subprocess.run(
        [args.dotnet, "build", str(project_path), "-c", "Release", "-v", "quiet", "--nologo"],
        capture_output=True, text=True, timeout=120,
    )
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    flows = [
        "u p u ok u p",
        "u p u t59999 f ok u t60000 f u t60001 u p f u p",
        "u p ok t60001 u p t70000 f u p",
        "u p t60001 f u p ok u t120002 f u p",
        "t10000 u p ok t70000 f u t70001 f u p",
        "u p u t60000 f u t60000.9 f u t60001 f ok u p",
        "d u p t60001 f ok r p",
        "d u p t60001 f u p ok r p",
    ]
    cases = [f"{scenario} {flow}" for scenario in range(4) for flow in flows]
    # Only retry after an unsuccessful first job here. Reuse of an already
    # admitted client belongs to the separate AddClient identity comparison.
    cases.extend(f"{scenario} d u p t60001 f u p r ok r p" for scenario in (1, 2, 3))
    for change in ("", "e", "k", "n"):
        cases.append(f"0 d u p t60001 f u p r {change} r p")
    cases.append("0 d u p t60001 f e u p r r p")
    payload = "\n".join(cases) + "\n"
    outputs = []
    for command in ([args.native], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing pending admission observations"
    observations = [
        {"case": case, "native": native, "original": original_states}
        for case, native, original_states in zip(cases, *outputs)
    ]
    differences = [row for row in observations if row["native"] != row["original"]]
    report = {
        "passed": not differences,
        "flows": len(cases),
        "stateObservations": sum(len(row["original"].split(";")) for row in observations),
        "differenceCount": len(differences),
        "nativeImageSHA256": hashlib.sha256(Path(args.native).read_bytes()).hexdigest(),
        "applicationParityVerified": False,
        "scope": "Unchanged original AddClient, admission handler, packet mapping and complete cache; controlled "
                 "scene, clock and timer. Includes delayed jobs, queue expiry, current-queue replay, failed-job "
                 "retry, cached endpoint reference reuse, changed secure session and missing scene presence. "
                 "Does not prove actual endpoint-cache/socket integration, full scene teardown or "
                 "whole-server/platform parity.",
        "sourceHashes": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                server_path, cache_path, fixture_path,
                ROOT / "native-port/src/circuit_transport.cpp",
                ROOT / "native-port/include/opensim/framework/expiring_cache.hpp",
                ROOT / "native-port/tests/pending_admission_probe.cpp",
                ROOT / "native-port/tests/pending_cache_clock.hpp",
            )
        },
        "observations": observations,
    }
    (work / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    assert not differences, f"Pending admission differences: {differences[:3]!r}; see {work / 'results.json'}"
    print(f"PASS: {len(cases)} original handler/mapping/cache flows with controlled scene admission and timer delivery")


if __name__ == "__main__":
    main()
