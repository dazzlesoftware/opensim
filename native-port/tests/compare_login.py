"""Compare native login handlers with original C# handlers and dependency DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import xmlrpc.client

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Server/Handlers/Login/LLLoginHandlers.cs", "OpenSim/Services/Interfaces/ILoginService.cs"]


def original_class(text, name):
    start = text.index("public class " + name)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for name, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, name).text = value
    group = ET.SubElement(project, "ItemGroup")
    for source in SOURCES:
        ET.SubElement(group, "Compile", Include=str(ROOT / source))
    for name in ["Program.cs", "Stubs.cs"]:
        ET.SubElement(group, "Compile", Include=str(Path(__file__).parent / "login_reference" / name))
    response_file = ROOT / "OpenSim/Services/LLLoginService/LLLoginResponse.cs"
    extracted = original_class(response_file.read_text(encoding="utf-8-sig"), "LLFailedLoginResponse")
    generated = work / "FailedResponse.cs"
    generated.write_text("using System.Collections; using OpenMetaverse.StructuredData; namespace OpenSim.Services.LLLoginService {" + extracted + "}\n")
    ET.SubElement(group, "Compile", Include=str(generated))
    for name in ["OpenMetaverseTypes", "OpenMetaverse.StructuredData", "XMLRPC", "Nini", "log4net"]:
        item = ET.SubElement(group, "Reference", Include=name)
        ET.SubElement(item, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "LoginReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/LoginReference.dll")]


def cases():
    for name in ["UserProblem", "GridProblem", "InventoryProblem", "DeadRegionProblem", "LoginBlockedProblem", "UnverifiedAccountProblem", "AlreadyLoggedInProblem", "InternalError"]:
        yield "failure\t" + name
    base = {"first": "Test", "last": "User", "passwd": "secret"}
    variants = [{}, base, {"first": "A"}, {"first": "A", "last": "B"},
                {"first": "", "last": "", "passwd": ""},
                {"first": "A", "last": "B", "web_login_key": "12345678-1234-1234-1234-123456789012"},
                {"first": "A", "last": "B", "web_login_key": "00000000-0000-0000-0000-000000000000"},
                {**base, "scope_id": "ABCDEF01-2345-6789-ABCD-EF0123456789"},
                {**base, "scope_id": "bad"}, {**base, "start": "uri:Welcome&128&128&25"},
                {**base, "version": "6.6.3", "channel": "Firestorm", "mac": "hash", "id0": "other"},
                {**base, "first": "O'Connor & 試験", "passwd": "<&>\"'"},
                {**base, "options": ["inventory-root", "inventory-skeleton"]}]
    for key in base:
        variants += [{**base, key: None}]
    for field in ["first", "last", "passwd", "version", "channel", "mac", "id0", "start", "scope_id"]:
        for value in ["", "string", None, 42, True]:
            variants.append({**base, field: value})
    for data in variants:
        xml = xmlrpc.client.dumps((data,), methodname="login_to_simulator", allow_none=True).replace("\n", "")
        yield "xmlrpc\t" + xml
        root = ET.Element("llsd")
        mapping = ET.SubElement(root, "map")
        for key, value in data.items():
            ET.SubElement(mapping, "key").text = key
            if value is None:
                ET.SubElement(mapping, "undef")
            elif isinstance(value, bool):
                ET.SubElement(mapping, "boolean").text = "1" if value else "0"
            elif isinstance(value, int):
                ET.SubElement(mapping, "integer").text = str(value)
            elif isinstance(value, list):
                array = ET.SubElement(mapping, "array")
                for item in value:
                    ET.SubElement(array, "string").text = item
            else:
                ET.SubElement(mapping, "string").text = value
        yield "llsd\t" + ET.tostring(root, encoding="unicode")
    for level in [0, 200, "-100", "invalid", None, 2147483647, -2147483648, " +1 "]:
        yield "level\t" + xmlrpc.client.dumps(({**base, "level": level},), methodname="set_login_level", allow_none=True).replace("\n", "")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    ref = reference(args.dotnet, work)
    scenarios = list(cases())
    data = "\n".join(scenarios) + "\n"
    outputs = []
    for command in [ref, [args.native]]:
        result = subprocess.run(command, input=data, capture_output=True, text=True, encoding="utf-8", timeout=30)
        if result.returncode:
            raise RuntimeError(result.stderr)
        outputs.append(result.stdout.splitlines())
    if any(len(output) != len(scenarios) for output in outputs):
        raise AssertionError(f"Expected {len(scenarios)} rows, got {[len(output) for output in outputs]}")
    differences = []
    for index, (left, right) in enumerate(zip(*outputs)):
        expected = "EXCEPTION" if left == "EXCEPTION" else json.loads(left)
        actual = "EXCEPTION" if right == "EXCEPTION" else xmlrpc.client.loads(right)[0][0]
        if expected != actual:
            differences.append({"scenario": scenarios[index], "reference": expected, "native": actual})
    report = {"passed": not differences, "scenarioCount": len(scenarios), "differences": differences,
              "referenceTargetFramework": "net8.0",
              "sources": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                          for path in SOURCES + ["OpenSim/Services/LLLoginService/LLLoginResponse.cs"]}}
    (work / "login-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(json.dumps(differences[:20], indent=2))
    print(f"PASS: {len(scenarios)} observations match original C# login handlers and failure responses")


if __name__ == "__main__":
    main()
