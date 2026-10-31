"""Byte-for-byte checks against the checkout's original protocol assembly."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native-port/tools"))
from generate_lludp_schema import parse_template


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    group = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(group, key).text = value
    group = ET.SubElement(project, "ItemGroup")
    ET.SubElement(group, "Compile", Include=str(Path(__file__).parent / "udp_reference/PacketProbe.cs"))
    for name in ["OpenMetaverse", "OpenMetaverseTypes", "OpenMetaverse.StructuredData"]:
        reference = ET.SubElement(group, "Reference", Include=name)
        ET.SubElement(reference, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "PacketReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    protocol = ROOT / "native-port/protocol"
    schema = parse_template((protocol / "message_template.msg").read_text())
    aliases = json.loads((protocol / "block-renames.json").read_text())
    for name, message in schema.items():
        for block in message["blocks"]:
            block["name"] = aliases.get(name, {}).get(block["name"], block["name"])
    schema_path = work / "schema.json"
    schema_path.write_text(json.dumps(schema))
    return [dotnet, str(work / "app/PacketReference.dll"), str(schema_path)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    catalog = json.loads((ROOT / "native-port/protocol/packet-catalog.json").read_text())
    scenarios = [f"{packet['name']} {seed}" for packet in catalog for seed in range(8)]
    data = "\n".join(scenarios) + "\n"
    results = []
    for probe in [command, [args.native]]:
        process = subprocess.run(probe, input=data, capture_output=True, text=True, timeout=90)
        if process.returncode:
            raise RuntimeError(process.stderr)
        results.append(process.stdout.splitlines())
    if any(len(rows) != len(scenarios) for rows in results):
        raise AssertionError(f"Expected {len(scenarios)} rows, got {[len(rows) for rows in results]}")
    differences = []
    for scenario, expected, actual in zip(scenarios, *results):
        if expected != actual or expected.startswith("ERROR:"):
            differences.append({"scenario": scenario, "reference": expected, "native": actual})
    report = {"passed": not differences, "packetTypes": len(catalog), "scenarioCount": len(scenarios),
              "packetCodecCases": len(catalog) * 4, "packetSplittingCases": len(catalog) * 4,
              "assemblySha256": hashlib.sha256((ROOT / "bin/OpenMetaverse.dll").read_bytes()).hexdigest(),
              "differences": differences}
    (work / "lludp-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(f"{len(differences)} LLUDP differences:\n" + json.dumps(differences[:6], indent=2))
    print(f"PASS: {len(scenarios)} LLUDP byte comparisons across all {len(catalog)} compiled message types")


if __name__ == "__main__":
    main()
