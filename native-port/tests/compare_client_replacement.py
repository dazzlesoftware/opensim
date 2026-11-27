"""Check native replacement ordering against the unchanged original AddClient.

The scene and client lifecycle are controlled fixtures. This checks the server's
replacement boundary, not complete scene, presence, or capability teardown.
"""

import argparse
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
    original = (ROOT / "OpenSim/Region/ClientStack/Linden/UDP/LLUDPServer.cs").read_text(encoding="utf-8-sig")
    add_client = method(original, "protected virtual IClientAPI AddClient(")
    template = Path(__file__).with_name("client_replacement_reference.cs").read_text()
    (work / "Program.cs").write_text(template.replace("/*ADD_CLIENT*/", add_client))
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    project_path = work / "Reference.csproj"
    ET.ElementTree(project).write(project_path)
    built = subprocess.run(
        [args.dotnet, "build", str(project_path), "-c", "Release", "-v", "quiet", "--nologo"],
        capture_output=True, text=True, timeout=120,
    )
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    results = []
    for command in (
        [args.native, "--replacement-probe"],
        [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")],
    ):
        run = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        results.append(run.stdout.splitlines())
    assert len(results[0]) == len(results[1]) == 4, "Missing replacement observations"
    assert results[0] == results[1], f"Native: {results[0]!r}; original: {results[1]!r}"
    print("PASS: original AddClient replacement order, startup failure and removed-circuit observations")


if __name__ == "__main__":
    main()
