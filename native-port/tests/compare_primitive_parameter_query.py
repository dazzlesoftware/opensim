"""Compare query routing with the unchanged original API methods."""
import argparse
import itertools
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    begin = source.index(signature)
    brace = source.index("{", begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    lsl = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs").read_text(encoding="utf-8-sig")
    ossl = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/OSSL_Api.cs").read_text(encoding="utf-8-sig")
    types = (ROOT / "OpenSim/Region/ScriptEngine/Shared/LSL_Types.cs").read_text(encoding="utf-8-sig")
    source = Path(__file__).with_name("primitive_parameter_query_reference.cs").read_text()
    source = source.replace("/*INTEGER_METHOD*/", method(types, "public int GetIntegerItem(int itemIndex)"))
    source = source.replace("/*LSL_METHODS*/", "\n".join(method(lsl, signature) for signature in (
        "public LSL_List llGetPrimitiveParams(LSL_List rules)",
        "public LSL_List llGetLinkPrimitiveParams(int linknumber, LSL_List rules)",
        "public LSL_List GetPrimitiveParamsEx(LSL_Key prim, LSL_List rules)")))
    source = source.replace("/*OSSL_METHOD*/",
                            method(ossl, "public LSL_List osGetLinkPrimitiveParams(int linknumber, LSL_List rules)"))
    (work / "Program.cs").write_text(source, encoding="utf-8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    reference = ET.SubElement(ET.SubElement(project, "ItemGroup"), "Reference", Include="OpenMetaverseTypes")
    ET.SubElement(reference, "HintPath").text = str(ROOT / "bin/OpenMetaverseTypes.dll")
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    build = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    cases = list(itertools.product(range(4), (-4, -1, 0, 1, 2, 3, 4), range(4),
                                   range(4), range(9), range(2)))
    payload = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    outputs = []
    for command in ([args.native, "--probe"],
                    [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=60)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing query observations"
    for case, native, original in zip(cases, *outputs):
        assert native == original, f"Query {case}: native {native!r}, original {original!r}"
    print(f"PASS: {len(cases)} original primitive parameter query routing observations")


if __name__ == "__main__":
    main()
