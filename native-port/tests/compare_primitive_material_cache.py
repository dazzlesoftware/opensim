"""Compare cache projections with the original FaceMaterial OSD constructor."""
import argparse
import itertools
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

from compare_primitive_parameter_query import ROOT, method


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    material = (ROOT / "OpenSim/Region/Framework/Scenes/SOPMaterial.cs").read_text(encoding="utf-8-sig")
    fields = material[material.index("public class FaceMaterial"):material.index("public FaceMaterial()")]
    source = Path(__file__).with_name("primitive_material_cache_reference.cs").read_text()
    source = source.replace("/*FIELDS*/", "\n".join(re.findall(r"public (?:float|byte|UUID) \w+\s*=\s*[^;]+;", fields)))
    source = source.replace("/*CONSTRUCTOR*/", method(material, "public FaceMaterial(OSDMap mat)"))
    (work / "Program.cs").write_text(source)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    references = ET.SubElement(project, "ItemGroup")
    for name in ("OpenMetaverseTypes", "OpenMetaverse.StructuredData"):
        reference = ET.SubElement(references, "Reference", Include=name)
        ET.SubElement(reference, "HintPath").text = str(ROOT / "bin" / f"{name}.dll")
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    cases = list(itertools.product(range(256), range(3)))
    payload = "".join(f"{seed} {mode}\n" for seed, mode in cases)
    outputs = []
    for command in ([args.native, "--materials"], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=30)
        assert run.returncode == 0, run.stdout + run.stderr
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing material cache observations"
    for case, native, original in zip(cases, *outputs):
        assert native == original, f"Material cache {case}: native {native!r}, original {original!r}"
    print(f"PASS: {len(cases)} original material constructor/cache projections")


if __name__ == "__main__":
    main()
