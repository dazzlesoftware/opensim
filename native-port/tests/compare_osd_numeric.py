"""Check numeric conversion against the exact bundled structured-data SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    ET.SubElement(items, "Compile", Include=str(ROOT / "native-port/tests/osd_numeric_reference.cs"))
    ET.SubElement(items, "Compile", Include=str(ROOT / "native-port/tests/osd_fixture_value.cs"))
    for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]:
        node = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(node, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "OsdNumericReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return [dotnet, str(work / "app/OsdNumericReference.dll")]


def cases():
    yield from ["undefined", "map", "uuid", "uri", "bool 0", "bool 1"]
    for value in [0, -1, 2147483647, -2147483648]:
        yield f"integer {value}"
    for value in [0, -1, 2147483647, -2147483648, 2147483648, -2147483649, 9223372036854775807, -9223372036854775808]:
        yield f"long {value}"
    for value in [0, 1, 255, 256, 2147483647, 2147483648, 4294967295]:
        yield f"unsigned {value}"
    for value in [0., -0., .5, 1.5, 2.5, -.5, -1.5, -2.5, 2147483646.5, 2147483647., 2147483648., -2147483649., float("inf"), float("-inf"), float("nan")]:
        yield "real " + struct.pack(">d", value).hex()
    strings = ["", " ", "0", "-0", "2.5", "-2.5", "2147483648", "-2147483649", "NaN", "-NaN", "+NaN", "nan", "Infinity", "infinity", "-Infinity", "∞", "-∞", "1e9999", "1e-9999", "1,234.5", "1,,2", ",12", "12,", "12,e2", "12.3,4", "1e+2", "1e", ".5", "5.", "\t1\r\n", "\u00a01\u00a0", "\u20031\u2003", "1\0", "1\0\0", "1\0 ", "NaN\0", "true", "0x10", "١٢", "3.4028234663852886e+38", "1.000000059604644775390625", "-0.000000000000000000001"]
    for value in strings:
        yield "string " + value.encode().hex()
    for value in ["", "1,2,3", "1,2,3,4", "-1,256,257,-258,7"]:
        yield "array " + value
    # OSDBinary.AsInteger performs an unchecked four-byte read for short
    # arrays. Such undefined source reads are excluded and rejected natively.
    for value in ["00000000", "ffffffff", "80000000", "1234567890"]:
        yield "binary " + value
    for value in ["1970-01-01T00:00:00Z", "1969-12-31T23:59:59Z", "1969-12-31T23:59:59.5Z", "2038-01-19T03:14:08Z", "2106-02-07T06:28:16Z", "0001-01-01T00:00:00Z", "9999-12-31T23:59:59Z", "2026-10-29T12:34:56.1234567Z", "2026-10-29T14:34:56+02:00"]:
        yield "date " + value
    generator = random.Random(5131)
    for _ in range(256):
        yield f"real {generator.getrandbits(64):016x}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    source = reference(args.dotnet, work)
    commands = list(cases())
    results = []
    for executable in [source, [args.native]]:
        result = subprocess.run(executable, input="\n".join(commands) + "\n", capture_output=True, text=True, encoding="utf-8", timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        results.append(result.stdout.splitlines())
    if any(len(rows) != len(commands) for rows in results):
        raise AssertionError((len(commands), [len(rows) for rows in results]))
    differences = [{"input": command, "source": expected, "native": actual} for command, expected, actual in zip(commands, *results) if expected != actual]
    report = {"passed": not differences, "observations": len(commands), "libraryHashes": {name: hashlib.sha256((ROOT / "bin" / (name + ".dll")).read_bytes()).hexdigest() for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData"]}, "excluded": "Undefined short OSDBinary integer reads", "differences": differences}
    (work / "osd-numeric-parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(json.dumps(differences[:10], indent=2))
    print(f"PASS: {len(commands)} SDK numeric conversion observations")


if __name__ == "__main__":
    main()
