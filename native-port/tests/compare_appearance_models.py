"""Compile actual attachment/context models and compare mutation/pack behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_osd_numeric import cases as numeric_cases, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, "ItemGroup")
    sources = ["OpenSim/Framework/AvatarAttachment.cs", "OpenSim/Framework/EntityTransferContext.cs"]
    version_path = ROOT / "OpenSim/Framework/VersionInfo.cs"
    version = version_path.read_text(encoding="utf-8-sig")
    declarations = [re.search(r"public readonly static float " + name + r" = [^;]+;", version).group() for name in ["SimulationServiceVersionAcceptedMax", "SimulationServiceVersionSupportedMax"]]
    stub = work / "VersionInfo.cs"
    stub.write_text("namespace OpenSim.Framework {public class VersionInfo {" + "\n".join(declarations) + "}}", encoding="utf-8")
    for path in [*(ROOT / source for source in sources), stub, ROOT / "native-port/tests/appearance_models_reference.cs", ROOT / "native-port/tests/osd_fixture_value.cs"]:
        ET.SubElement(group, "Compile", Include=str(path))
    for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]:
        item = ET.SubElement(group, "Reference", Include=name)
        ET.SubElement(item, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "AppearanceModelsReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    commands = ["reset", "empty", "copy", "missing", "null", "wrong"]
    for case in numeric_cases():
        for command in ["set", "partial", "ctor"]:
            commands.append(command + " " + case)
        commands.extend(["missing", "empty", "copy"])
    for text in ["00000000-0000-0000-0000-000000000005", " 00000000000000000000000000000005 ", "{00000000-0000-0000-0000-000000000005}"]:
        commands.append("set string " + text.encode().hex())
    results = []
    for executable in [[args.dotnet, str(work / "app/AppearanceModelsReference.dll")], [args.native]]:
        result = subprocess.run(executable, input="\n".join(commands) + "\n", capture_output=True, text=True, encoding="utf-8", timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        results.append(result.stdout.splitlines())
    if any(len(rows) != len(commands) for rows in results):
        raise AssertionError((len(commands), [len(rows) for rows in results]))
    differences = [{"index": i, "input": command, "source": expected, "native": actual} for i, (command, expected, actual) in enumerate(zip(commands, *results)) if expected != actual]
    report = {"passed": not differences, "observations": len(commands), "sourceHashes": {source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest() for source in sources + ["OpenSim/Framework/VersionInfo.cs"]}, "libraryHashes": {name: hashlib.sha256((ROOT / "bin" / (name + ".dll")).read_bytes()).hexdigest() for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]}, "differences": differences}
    (work / "appearance-models-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    print(f"PASS: {len(commands)} attachment/transfer-context state and pack observations")


if __name__ == "__main__":
    main()
