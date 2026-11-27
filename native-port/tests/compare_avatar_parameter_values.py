"""Execute the unchanged avatar getter and Zrot with real SDK vector arithmetic."""
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
    constants = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Runtime/LSL_Constants.cs").read_text(encoding="utf-8-sig")
    material = (ROOT / "OpenSim/Region/Framework/Scenes/SOPMaterial.cs").read_text(encoding="utf-8-sig")
    part_reference = Path(__file__).with_name("primitive_parameter_values_reference.cs").read_text()
    source = Path(__file__).with_name("avatar_parameter_values_reference.cs").read_text()
    source = source.replace("/*TYPES*/", part_reference[:part_reference.index("static class ScriptBaseClass")])
    declarations = re.findall(r"public const int PRIM_\w+\s*=\s*[^;]+;", constants)
    declarations += ["public const int ALL_SIDES = -1;"]
    source = source.replace("/*CONSTANTS*/", "\n".join(declarations))
    source = source.replace("/*MATERIAL_ENUM*/", method(material, "public enum SopMaterial : int"))
    source = source.replace("/*ZROT*/", method(api, "private static Vector3 Zrot(Quaternion r)"))
    source = source.replace("/*GETTER*/", method(api, "public LSL_List GetPrimParams(ScenePresence avatar"))
    source = source.replace("/*REAL*/", re.search(
        r"static string Real\(double value\)[^;]+;", part_reference).group())
    source = source.replace("/*CANONICAL*/", method(part_reference, "static string Canonical(LSLList list)"))
    (work / "Program.cs").write_text(source, encoding="utf-8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    reference = ET.SubElement(ET.SubElement(project, "ItemGroup"), "Reference", Include="OpenMetaverseTypes")
    ET.SubElement(reference, "HintPath").text = str(ROOT / "bin/OpenMetaverseTypes.dll")
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    codes = (-1,0,2,3,4,5,6,7,8,9,17,18,19,20,21,22,23,25,26,27,28,29,30,32,33,34,35,36,37,38,41,42,44)
    cases = []
    for legacy, seat, appearance, quaternion, code, face in itertools.product(
            range(2), range(5), range(3), range(12), codes, (-2, -1, 0, 20, 21, 45)):
        seed = (quaternion * 31 + seat * 17 + appearance * 13) % 256
        cases.append((seed, legacy, seat, appearance, quaternion, code, face, 3))
    cases += [(seed, legacy, seat, 0, quaternion, code, 0, 3)
              for seed, legacy, seat, quaternion, code in itertools.product(
                  range(256), range(2), (0, 3), range(12), (6, 33))]
    cases += [(0, 0, 0, 0, 0, 34, face, length)
              for face, length in itertools.product((-4, 1, 2, 21), range(5))]
    rng = random.Random(66103)
    cases += [(rng.randrange(256), rng.randrange(2), rng.randrange(5), rng.randrange(3),
               rng.randrange(12), 0, rng.choice((-1, 0, 20, 21, 45)), 3) for _ in range(128)]
    payload = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    outputs = []
    for command in ([args.native, "--probe"], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise RuntimeError(run.stdout[-2000:] + run.stderr[-2000:])
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing avatar observations"
    for index, (case, native, original) in enumerate(zip(cases, *outputs)):
        assert native == original, f"Case {index} {case}: native {native!r}, original {original!r}"
    print(f"PASS: {len(cases)} original avatar parameter value and provider-order observations")


if __name__ == "__main__":
    main()
