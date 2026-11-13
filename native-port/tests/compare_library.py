"""Compare catalog snapshots using original LibraryService, folder tree and Nini."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Services/InventoryService/LibraryService.cs", "OpenSim/Framework/InventoryFolderImpl.cs", "OpenSim/Framework/InventoryFolderBase.cs", "OpenSim/Framework/InventoryNodeBase.cs", "OpenSim/Services/Interfaces/ILibraryService.cs"]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    util = (ROOT / "OpenSim/Framework/Util.cs").read_text(encoding="utf-8-sig")
    enum = re.search(r"public enum PermissionMask\s*:\s*uint\s*\{[^}]+\}", util).group()
    declaration = work / "PermissionMask.cs"
    declaration.write_text("namespace OpenSim.Framework {" + enum + "}")
    response = (ROOT / "OpenSim/Services/LLLoginService/LLLoginResponse.cs").read_text(encoding="utf-8-sig")
    def extract(pattern):
        match = re.search(pattern, response)
        if not match:
            raise RuntimeError(f"Cannot find inventory login source: {pattern}")
        end = response.index("{", match.start()) + 1
        depth = 1
        while depth:
            if response[end] == "{":
                depth += 1
            elif response[end] == "}":
                depth -= 1
            end += 1
        return response[match.start():end]
    methods = [extract(r"(?:private|protected virtual) [^\n{]+ " + name + r"\(") for name in ["FillOutInventoryData", "FillOutActiveGestures", "GetInventorySkeleton", "GetInventoryLibrary", "GetLibraryOwner"]]
    methods.append(extract(r"public class InventoryData\s*\{"))
    login_source = work / "LoginFields.cs"
    wrapper = """using System;using System.Collections;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;
public class LoginFields {
    static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(LoginFields));UUID agentID=UUID.Zero;
    public ArrayList InventoryRoot=new(),InventorySkeleton=new(),InventoryLibRoot,InventoryLibraryOwner=new(),InventoryLibrary=new(),ActiveGestures=new();
    public object Build(List<InventoryFolderBase> folders,ILibraryService library,List<InventoryItemBase> gestures){FillOutInventoryData(folders,library);FillOutActiveGestures(gestures);return new Dictionary<string,object>{{"inventory-root",InventoryRoot},{"inventory-skeleton",InventorySkeleton},{"inventory-lib-root",InventoryLibRoot},{"inventory-lib-owner",InventoryLibraryOwner},{"inventory-skel-lib",InventoryLibrary},{"gestures",ActiveGestures}};}
"""
    login_source.write_text(wrapper + "\n".join(methods) + "\n}")
    for path in [*(ROOT / path for path in SOURCES), declaration, login_source, *(Path(__file__).parent / "library_reference" / name for name in ("Program.cs", "Stubs.cs"))]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes", "Nini"]:
        dependency = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(dependency, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "LibraryReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/LibraryReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    def write_config(path, sections):
        document = ET.Element("Nini")
        for index, values in enumerate(sections):
            section = ET.SubElement(document, "Section", Name=str(index))
            for key, value in values.items():
                ET.SubElement(section, "Key", Name=key, Value=str(value))
        ET.ElementTree(document).write(path, encoding="utf-8", xml_declaration=True)
    fixtures = work / "fixtures"
    fixtures.mkdir(exist_ok=True)
    root_id = "00000112-000f-0000-0000-000100bba000"
    folder_a = "00000000-0000-0000-0000-000000000001"
    folder_b = "00000000-0000-0000-0000-000000000002"
    item_id = "00000000-0000-0000-0000-000000000003"
    write_config(fixtures / "folders.xml", [{"folderID": folder_a, "name": "A", "type": 65535, "version": 65536}, {"folderID": "00000000-0000-0000-0000-000000000004", "parentFolderID": folder_b, "name": "Too early"}, {"folderID": folder_b, "name": "B"}])
    write_config(fixtures / "items.xml", [{"inventoryID": item_id, "folderID": folder_a, "name": "First", "basePermissions": 4294967295, "currentPermissions": -1, "flags": -2147483648}, {"inventoryID": item_id, "folderID": folder_a, "name": "Ignored duplicate"}, {"inventoryID": item_id, "folderID": folder_b, "name": "Global replacement", "groupPermissions": 42}])
    write_config(fixtures / "control.xml", [{"RootVersion": -1}, {"foldersFile": "folders.xml", "itemsFile": "items.xml"}])
    write_config(fixtures / "case-items.xml", [{
        "inventoryID": f" {item_id} ",
        "folderID": f" {root_id} ",
        "assetID": item_id.replace("-", ""),
        "NAME": "Uppercase",
        "Description": "Case",
        "inventoryType": " +7 ",
        "basePermissions": " +4294967295 ",
    }])
    write_config(fixtures / "case.xml", [{"foldersFile": "absent.xml", "itemsFile": "case-items.xml"}])
    write_config(fixtures / "directory-input.xml", [{"itemsFile": "case-items.xml"}])
    (fixtures / "世界").mkdir(exist_ok=True)
    (fixtures / "世界/items.xml").write_bytes((fixtures / "case-items.xml").read_bytes())
    write_config(fixtures / "unicode-path.xml", [
        {"foldersFile": "世界/missing.xml", "itemsFile": "世界/items.xml"},
    ])
    (fixtures / "whitespace-items.xml").write_text(
        f'<Nini><Section Name="Item"><Key Name="inventoryID" Value="{item_id}"/>'
        '<Key Name="name" Value="Literal\tTab"/><Key Name="description" Value="Escaped&#x9;Tab"/>'
        '</Section></Nini>', encoding="utf-8",
    )
    write_config(fixtures / "whitespace.xml", [{"itemsFile": "whitespace-items.xml"}])
    (fixtures / "linebreak-folders.xml").write_bytes(
        f'<Nini><Section Name="Folder"><Key Name="folderID" Value="{folder_a}"/>'
        '<Key Name="name" Value="Line\r\nBreak"/></Section></Nini>'.encode("utf-8")
    )
    write_config(fixtures / "linebreak.xml", [{"foldersFile": "linebreak-folders.xml"}])
    (fixtures / "entity-folders.xml").write_text(
        '<!DOCTYPE Nini [<!ENTITY label "Library &amp; Catalog">]>'
        f'<Nini><Section Name="Folder"><Key Name="folderID" Value="{folder_a}"/>'
        '<Key Name="name" Value="&label;"/></Section></Nini>', encoding="utf-8",
    )
    write_config(fixtures / "entity.xml", [{"foldersFile": "entity-folders.xml"}])
    (fixtures / "external-entity.xml").write_text(
        '<!DOCTYPE Nini [<!ENTITY % external SYSTEM "unavailable.dtd">%external;]>'
        '<Nini><Section Name="Root"><Key Name="RootVersion" Value="3"/></Section></Nini>',
        encoding="utf-8",
    )
    (fixtures / "malformed.xml").write_text("<Nini><Section>")
    (fixtures / "multiple-roots.xml").write_text(
        '<Nini><Section Name="Root"><Key Name="RootVersion" Value="99"/></Section></Nini><Nini/>',
        encoding="utf-8",
    )
    (fixtures / "duplicate-folders.xml").write_text(f'<Nini><Section Name="Same"><Key Name="folderID" Value="{folder_a}"/><Key Name="name" Value="First"/></Section><Section Name="Same"><Key Name="folderID" Value="{folder_b}"/><Key Name="name" Value="Second"/></Section></Nini>')
    write_config(fixtures / "duplicate-sections.xml", [{"foldersFile": "duplicate-folders.xml", "itemsFile": "absent.xml"}])
    (fixtures / "duplicate-keys.xml").write_text('<Nini><Section Name="Root"><Key Name="RootVersion" Value="1"/><Key Name="RootVersion" Value="2"/></Section></Nini>')
    differences = []
    scenarios = []
    for name, content, managed_error, native_error in [
        ("wrong-root", "<Library/>", "System.ArgumentException", "Did not find Nini XML root node"),
        ("unnamed-section", "<Nini><Section/></Nini>", "System.NullReferenceException", "Object reference"),
        ("missing-key-value", '<Nini><Section Name="Root"><Key Name="RootVersion"/></Section></Nini>',
         "System.NullReferenceException", "Object reference"),
    ]:
        path = fixtures / (name + ".xml")
        path.write_text(content, encoding="utf-8")
        for executable, error in [(command, managed_error), ([args.native], native_error)]:
            result = subprocess.run(executable + [str(path)], capture_output=True, text=True, timeout=60)
            if not result.returncode or error not in result.stdout + result.stderr:
                raise AssertionError(f"{name}: {result.stdout}{result.stderr}")
        scenarios.append({"name": name, "rejected": True})
    controls = [
        "control.xml", "case.xml", "directory-input.xml", "malformed.xml",
        "multiple-roots.xml", "absent.xml", "duplicate-sections.xml", "duplicate-keys.xml",
        "whitespace.xml", "linebreak.xml", "entity.xml", "external-entity.xml", "unicode-path.xml",
    ]
    for control in [ROOT / "bin/inventory/Libraries.xml", *(fixtures / name for name in controls), Path("--tree")]:
        snapshots = []
        for executable in [command, [args.native]]:
            result = subprocess.run(executable + [str(control)], capture_output=True, text=True, encoding="utf-8", timeout=60)
            if control.name == "duplicate-keys.xml":
                if not result.returncode or not ("System.ArgumentException" in result.stderr or "ERROR:" in result.stdout):
                    raise AssertionError("Duplicate-key rejection differs: " + result.stdout + result.stderr)
                snapshots.append(["DUPLICATE_KEY_REJECTED"])
                continue
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            snapshots.append(result.stdout.splitlines())
        expected, actual = snapshots
        differences.extend({"scenario": control.name, "line": i, "reference": a, "native": b} for i, (a, b) in enumerate(zip(expected, actual)) if a != b)
        if len(expected) != len(actual):
            differences.append({"scenario": control.name, "expectedLines": len(expected), "actualLines": len(actual)})
        scenarios.append({"name": control.name, "folderCount": sum(row.startswith("F|") for row in expected), "itemCount": sum(row.startswith("I|") for row in expected)})
    report = {"passed": not differences, "scenarios": scenarios, "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES}, "differences": differences}
    for seed in range(4):
        snapshots = []
        for executable in [command, [args.native]]:
            run = subprocess.run(executable + ["--login", str(seed), str(ROOT / "bin/inventory/Libraries.xml")], capture_output=True, text=True, encoding="utf-8", timeout=60)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            snapshots.append(json.loads(run.stdout))
        if snapshots[0] != snapshots[1]:
            differences.append({"loginScenario": seed, "reference": snapshots[0], "native": snapshots[1]})
    report["passed"] = not differences
    report["loginInventoryScenarios"] = 4
    report["sourceHashes"]["OpenSim/Services/LLLoginService/LLLoginResponse.cs"] = hashlib.sha256((ROOT / "OpenSim/Services/LLLoginService/LLLoginResponse.cs").read_bytes()).hexdigest()
    (work / "library-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(json.dumps(differences[:5], indent=2))
    shipped = next(scenario for scenario in scenarios if scenario["name"] == "Libraries.xml")
    print(f"PASS: {len(scenarios)} catalog scenarios; {shipped['folderCount']} shipped folders and {shipped['itemCount']} shipped items match the original service")


if __name__ == "__main__":
    main()
