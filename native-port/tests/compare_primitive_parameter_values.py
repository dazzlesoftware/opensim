"""Compare part parameter values with the unchanged original getter and helpers."""
import argparse
import itertools
from pathlib import Path
import random
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
    api = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs").read_text(encoding="utf-8-sig")
    part = (ROOT / "OpenSim/Region/Framework/Scenes/SceneObjectPart.cs").read_text(encoding="utf-8-sig")
    shape = (ROOT / "OpenSim/Framework/PrimitiveBaseShape.cs").read_text(encoding="utf-8-sig")
    constants = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Runtime/LSL_Constants.cs").read_text(encoding="utf-8-sig")
    declarations = re.findall(r"public const int PRIM_\w+\s*=\s*[^;]+;", constants)
    declarations += ['public const int ALL_SIDES = -1;', 'public const string NULL_KEY = "00000000-0000-0000-0000-000000000000";']
    source = Path(__file__).with_name("primitive_parameter_values_reference.cs").read_text()
    source = source.replace("/*CONSTANTS*/", "\n".join(declarations))
    source = source.replace("/*ENUMS*/", method(part, "public enum PrimType : int") + "\n" +
                            "\n".join(method(shape, "public enum " + name + " : byte")
                                      for name in ("ProfileShape", "HollowShape", "Extrusion")))
    source = source.replace("/*GEOMETRY_METHODS*/", "\n".join(method(part, signature) for signature in (
        "public int GetNumberOfSides()", "public PrimType GetPrimType(bool ignoreSculpt = false)")))
    source = source.replace("/*GETTER_METHODS*/", "\n".join(method(api, signature) for signature in (
        "public LSL_List GetPrimParams(SceneObjectPart part",
        "private string GetMaterialTextureUUIDbyRights(",
        "private void getLSLFaceMaterial(")))
    (work / "Program.cs").write_text(source, encoding="utf-8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    references = ET.SubElement(project, "ItemGroup")
    for name in ("OpenMetaverseTypes", "OpenMetaverse"):
        reference = ET.SubElement(references, "Reference", Include=name)
        ET.SubElement(reference, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    codes = (2,3,4,5,6,7,8,9,17,18,19,20,21,22,23,25,26,27,28,29,30,31,32,33,34,35,36,37,38,41,42,44)
    cases = list(itertools.product(range(9), (0, 1, 8, 17, 127, 128, 255),
                                   (0, 0x20000401), codes, (-2, -1, 0, 2, 44, 45), (0, 2, 3, 4, 5, 6)))
    cases += [(4, seed, 0, 9, 0, 0) for seed in range(256)]
    cases += [(0, seed, 0, 26, 0, 0) for seed in range(256)]
    rng = random.Random(41791)
    cases += [(rng.randrange(9), rng.randrange(256), rng.getrandbits(32), 0,
               rng.choice((-1, 0, 2)), rng.randrange(7)) for _ in range(128)]
    payload = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    outputs = []
    for command in ([args.native, "--probe"], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise RuntimeError(run.stdout[-3000:] + run.stderr[-3000:])
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing value observations"
    for index, (case, native, original) in enumerate(zip(cases, *outputs)):
        assert native == original, f"Case {index} {case}: native {native!r}, original {original!r}"
    print(f"PASS: {len(cases)} original primitive parameter value observations")


if __name__ == "__main__":
    main()
