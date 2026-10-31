"""Compare complete original AvatarWearable state transitions and packed UUIDs."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]


def cases():
    yield "clear"
    yield "constants"
    yield "defaults"
    for i in range(8):
        yield f"add {i} {i % 3}"
    yield "add 1 9"  # Updating an existing item still works at the layer limit.
    yield "remove 1"
    yield "remove 3"
    yield "add 8 2"
    yield "add 9 2"
    yield "asset 2"  # Select by dictionary slot, not wearable presentation order.
    yield "asset 2"
    yield "wear 0 1"
    yield "wear-item 2 0"
    yield "ctor 3 4"
    yield "unpack-null"
    yield "limits 21 19"
    yield "constants"
    yield "defaults"
    uid = "00000000-0000-0000-0000-000000000003"
    atoms = ["<undef/>", "<integer>3</integer>", "<boolean>true</boolean>", "<real>3.5</real>", "<array/>", "<map/>", "<date>2026-10-29T00:00:00Z</date>", "<uri>http://example.test/</uri>", "<binary>AAAAAAAAAAAAAAAAAAAAAw==</binary>"]
    atoms += [f"<string>{escape(s)}</string>" for s in ["", uid, uid.upper(), uid.replace("-", ""), " " + uid + " ", "{" + uid + "}", "(" + uid + ")", "\t" + uid, uid + "x", "bad"]]
    atoms += [f"<uuid>{uid}</uuid>"]
    rows = ["<map/>", "<map><key>item</key><uuid>" + uid + "</uuid></map>"]
    for atom in atoms:
        rows.append("<map><key>item</key>" + atom + "<key>asset</key><uuid>" + uid + "</uuid></map>")
        rows.append("<map><key>item</key><uuid>" + uid + "</uuid><key>asset</key>" + atom + "</map>")
    for operation in ["unpack", "construct"]:
        for row in rows:
            yield "ctor 8 9"
            yield operation + " <llsd><array>" + row + "</array></llsd>"
        for invalid in ["<integer>1</integer>", "<string>wrong</string>", "<array/>", "<undef/>"]:
            yield "ctor 8 9"
            yield operation + " <llsd><array>" + rows[1] + invalid + "</array></llsd>"
        yield operation + " <llsd><map/></llsd>"
    generator = random.Random(0x5EED)
    for _ in range(1200):
        op = generator.choice(["add", "add", "add", "remove", "asset", "wear", "wear-item", "ctor", "clear"])
        item, asset = generator.randrange(12), generator.randrange(8)
        yield op if op == "clear" else f"{op} {asset}" if op == "asset" else f"{op} {item}" if op == "remove" else f"{op} {item} {asset}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    source = ROOT / "OpenSim/Framework/AvatarWearable.cs"
    for path in [source, ROOT / "native-port/tests/avatar_wearable_reference.cs"]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]:
        item = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(item, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "AvatarWearableReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    commands = list(cases())
    rows = []
    for executable in [[args.dotnet, str(work / "app/AvatarWearableReference.dll")], [args.native]]:
        result = subprocess.run(executable, input="\n".join(commands) + "\n", capture_output=True, text=True, encoding="utf-8", timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        rows.append(result.stdout.splitlines())
    if any(len(result) != len(commands) for result in rows):
        raise AssertionError((len(commands), [len(result) for result in rows]))
    differences = [{"index": index, "command": command, "source": expected, "native": actual} for index, (command, expected, actual) in enumerate(zip(commands, *rows)) if expected != actual]
    report = {"passed": not differences, "observations": len(commands), "sourceHash": hashlib.sha256(source.read_bytes()).hexdigest(), "libraryHashes": {name: hashlib.sha256((ROOT / "bin" / (name + ".dll")).read_bytes()).hexdigest() for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]}, "differences": differences}
    (work / "avatar-wearable-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    print(f"PASS: {len(commands)} source-backed avatar wearable state/pack observations")


if __name__ == "__main__":
    main()
