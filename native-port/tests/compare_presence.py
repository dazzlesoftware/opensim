"""Compare actual presence service, Null provider and cache with native code."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Services/PresenceService/PresenceService.cs", "OpenSim/Services/PresenceService/PresenceServiceBase.cs", "OpenSim/Services/Interfaces/IPresenceService.cs", "OpenSim/Data/IPresenceData.cs", "OpenSim/Data/Null/NullPresenceData.cs", "OpenSim/Framework/ExpiringCacheOS.cs"]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    for path in [*(ROOT / path for path in SOURCES), *(Path(__file__).parent / "presence_reference" / name for name in ("Program.cs", "Stubs.cs"))]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes", "Nini"]:
        dependency = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(dependency, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "PresenceReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/PresenceReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    differences = []
    count = 16
    for seed in range(count):
        observations = []
        for executable in [command, [args.native]]:
            run = subprocess.run(executable + [str(seed)], capture_output=True, text=True, timeout=30)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            observations.append(run.stdout.strip())
        if observations[0] != observations[1]:
            differences.append({"seed": seed, "reference": observations[0], "native": observations[1]})
    report = {"passed": not differences, "scenarioCount": count, "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES}, "differences": differences}
    (work / "presence-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(json.dumps(differences[:3], indent=2))
    print(f"PASS: {count} presence lifecycle/cache/reference scenarios")


if __name__ == "__main__":
    main()
