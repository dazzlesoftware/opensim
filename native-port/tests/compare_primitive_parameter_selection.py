"""Compare live native World selection with unchanged original selection methods."""
import argparse
import itertools
from pathlib import Path
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
    lsl = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs").read_text(encoding="utf-8-sig")
    group = (ROOT / "OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs").read_text(encoding="utf-8-sig")
    source = Path(__file__).with_name("primitive_parameter_selection_reference.cs").read_text()
    source = source.replace("/*METHODS*/", "\n".join(method(lsl, signature) for signature in (
        "public static List<SceneObjectPart> GetLinkParts(SceneObjectPart part, int linkType)",
        "public static List<ScenePresence> GetLinkAvatars(int linkType, SceneObjectGroup sog)",
        "public static List<ISceneEntity> GetLinkEntities(SceneObjectPart part, int linkType)",
        "public static ISceneEntity GetLinkEntity(SceneObjectPart part, int linknum)")))
    source = source.replace("/*PART_LOOKUP*/", method(group, "public SceneObjectPart GetLinkNumPart(int linknum)"))
    source = source.replace("/*AVATAR_LOOKUP*/", method(group, "public ScenePresence GetLinkSitingAvatar(int linknumber)")
                            .replace(" GetLinkSitingAvatar(", " OriginalGetLinkSitingAvatar(", 1))
    (work / "Program.cs").write_text(source)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    cases = list(itertools.product(range(7), range(5), range(8), range(8),
                                  (-2147483648, -9, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 100, 2147483647),
                                  range(3)))
    payload = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    outputs = []
    for command in ([args.native, "--probe"], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise RuntimeError(run.stdout[-2000:] + run.stderr[-2000:])
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing live selection observations"
    for index, (case, native, original) in enumerate(zip(cases, *outputs)):
        assert native == original, f"Selection {index} {case}: native {native!r}, original {original!r}"
    print(f"PASS: {len(cases)} live World and original primitive link selection observations")


if __name__ == "__main__":
    main()
