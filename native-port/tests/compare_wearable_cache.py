"""Compare original baked-cache methods, asset models and instrumented cache I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from compare_asset_model import reference as asset_reference, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    asset_reference(args.dotnet, work / "assets")
    project = ET.parse(work / "assets/AssetModelReference.csproj")
    group = project.getroot().find("ItemGroup")
    for item in list(group):
        if item.tag == "Compile" and Path(item.get("Include")).name == "asset_model_reference.cs":
            group.remove(item)
    source = (ROOT / "OpenSim/Framework/AvatarAppearance.cs").read_text(encoding="utf-8-sig")
    declarations = [re.search(pattern, source).group() for pattern in [r"public readonly static int TEXTURE_COUNT = [^;]+;", r"public readonly static byte\[\] BAKE_INDICES = [^;]+;"]]
    constants = work / "AvatarAppearance.cs"
    constants.write_text("namespace OpenSim.Framework {public class AvatarAppearance {" + "\n".join(declarations) + "}}", encoding="utf-8")
    sources = ["OpenSim/Framework/WearableCacheItem.cs", "OpenSim/Framework/IAssetCache.cs"]
    for path in [*(ROOT / source for source in sources), ROOT / "native-port/tests/wearable_cache_reference.cs", constants]:
        ET.SubElement(group, "Compile", Include=str(path))
    item = ET.SubElement(group, "Reference", Include="OpenMetaverse.StructuredData")
    ET.SubElement(item, "HintPath").text = str(ROOT / "bin/OpenMetaverse.StructuredData.dll")
    path = work / "WearableCacheReference.csproj"
    project.write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    differences = []
    observations = 0
    for seed in range(64):
        rows = []
        for executable in [[args.dotnet, str(work / "app/WearableCacheReference.dll")], [args.native]]:
            result = subprocess.run(executable + [str(seed)], capture_output=True, text=True, encoding="utf-8", timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            rows.append(result.stdout.splitlines())
        if any(len(result) != 37 for result in rows):
            raise AssertionError((seed, [len(result) for result in rows]))
        for index, (expected, actual) in enumerate(zip(*rows)):
            observations += 1
            if expected != actual:
                differences.append({"seed": seed, "index": index, "source": expected, "native": actual})
    sources += ["OpenSim/Framework/AssetBase.cs", "OpenSim/Framework/SLUtil.cs", "OpenSim/Framework/AvatarAppearance.cs"]
    report = {"passed": not differences, "profiles": 64, "observations": observations, "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources}, "libraryHashes": {name: hashlib.sha256((ROOT / "bin" / (name + ".dll")).read_bytes()).hexdigest() for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]}, "differences": differences}
    (work / "wearable-cache-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    print(f"PASS: {observations} wearable cache/asset callback observations")


if __name__ == "__main__":
    main()
