"""Compare actual asset classes and original SLUtil type conversions.

NRBF reader is compiled to satisfy the original class dependency. This fixture
does not establish native NRBF or XML asset serialization compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def token(value):
    return "-" if value is None else value.encode().hex() if value else "."


def cases():
    yield "limits"
    yield "default"
    yield "metadata-default"
    yield "uuid 3 0 . -"
    values = [None, "", "bad", "http://remote.example/asset", "00000000-0000-0000-0000-000000000004", "ABCDEF01-2345-6789-ABCD-EF0123456789", " abcdef0123456789abcdef0123456789 ", "{abcdef01-2345-6789-abcd-ef0123456789}", "\tabcdef01-2345-6789-abcd-ef0123456789", "name\0tail", "試験/asset", "x" * 100]
    for value in values:
        yield "id " + token(value)
        yield "full 5"
        yield "id " + token(value)
        yield "string " + token(value) + " 13 " + token(value) + " " + token(value)
        for key in ["name", "description", "creator"]:
            yield key + " " + token(value)
    for ticks in [0, 1, 621355968000000000, 3155378975999999999, -1, 3155378976000000000]:
        for kind in [0, 1, 2, -1, 3]:
            yield f"date {ticks} {kind}"
    yield "data 010203ff"
    yield "sha 020304"
    yield "share"
    yield "upload -2147483648"
    yield "flags -2147483648"
    yield "local 1"
    yield "temporary 1"
    yield "metadata-default"
    yield "share"
    yield "metadata-null"
    for command in ["name .", "id .", "content .", "type 0", "full 1", "share", "data ff", "upload 2147483647"]:
        yield command
    yield "default"
    for value in range(-128, 128):
        yield f"type {value}"
    for value in [*range(-130, 260), -2147483648, 2147483647, -65536, 65536]:
        yield f"code {value}"
    source = (ROOT / "OpenSim/Framework/SLUtil.cs").read_text(encoding="utf-8-sig")
    mappings = source[source.index("private static TypeMapping[] MAPPINGS"):source.index("private static readonly FrozenDictionary")]
    content_types = sorted(set(re.findall(r'"([^"]+)"', mappings)))
    for value in [None, "", *content_types, "IMAGE/X-J2C", "image/x-j2c ", " image/x-j2c", "image/x-j2c; charset=UTF-8", "unknown/type"]:
        yield "lookup " + token(value)
        yield "default"
        yield "type 0"
        yield "content " + token(value)
        yield "type 13"
        yield "content ."
    names = re.findall(r'\{"([^"]+)"', source[source.index("name2Asset"):source.index("static SLUtil()")])
    for value in [None, "", *sorted(set(names)), "TEXTURE", "texture ", "category", "root", "suitcase", "gltf"]:
        yield "names " + token(value)
    generator = random.Random(8174)
    for _ in range(250):
        kind = generator.randrange(8)
        if kind == 0:
            yield f"type {generator.randrange(-1024, 1024)}"
        elif kind == 1:
            yield f"flags {generator.randrange(-2147483648, 2147483648)}"
        elif kind == 2:
            yield "id " + token(generator.choice(values))
        elif kind == 3:
            yield "content " + token(generator.choice(content_types + [None, ""]))
        elif kind == 4:
            yield "data " + (generator.randbytes(generator.randrange(8)).hex() or ".")
        elif kind == 5:
            yield "sha " + (generator.randbytes(generator.randrange(8)).hex() or ".")
        elif kind == 6:
            yield "share"
        else:
            yield "default"


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "OpenSim/Framework/SLUtil.cs").read_text(encoding="utf-8-sig")
    prefix = source[:source.index("static readonly char[] seps")]
    conversions = work / "SLUtil.cs"
    conversions.write_text(prefix + "\n}}\n", encoding="utf-8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable", "AllowUnsafeBlocks": "true"}.items():
        ET.SubElement(props, key).text = value
    group = ET.SubElement(project, "ItemGroup")
    for path in [ROOT / "OpenSim/Framework/AssetBase.cs", ROOT / "OpenSim/Framework/NrbfReader.cs", ROOT / "native-port/tests/asset_model_reference.cs", conversions]:
        ET.SubElement(group, "Compile", Include=str(path))
    item = ET.SubElement(group, "Reference", Include="OpenMetaverseTypes")
    ET.SubElement(item, "HintPath").text = str(ROOT / "bin/OpenMetaverseTypes.dll")
    path = work / "AssetModelReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return [dotnet, str(work / "app/AssetModelReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    source = reference(args.dotnet, work)
    commands = list(cases())
    rows = []
    for executable in [source, [args.native]]:
        result = subprocess.run(executable, input="\n".join(commands) + "\n", capture_output=True, text=True, encoding="utf-8", timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        rows.append(result.stdout.splitlines())
    if any(len(result) != len(commands) for result in rows):
        raise AssertionError((len(commands), [len(result) for result in rows]))
    differences = [{"index": i, "input": command, "source": expected, "native": actual} for i, (command, expected, actual) in enumerate(zip(commands, *rows)) if expected != actual]
    sources = ["OpenSim/Framework/AssetBase.cs", "OpenSim/Framework/SLUtil.cs"]
    report = {"passed": not differences, "observations": len(commands), "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources}, "typesLibraryHash": hashlib.sha256((ROOT / "bin/OpenMetaverseTypes.dll").read_bytes()).hexdigest(), "differences": differences}
    (work / "asset-model-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    print(f"PASS: {len(commands)} asset metadata/type conversion observations")


if __name__ == "__main__":
    main()
