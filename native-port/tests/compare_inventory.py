"""Run the actual XInventoryService with fixture dependencies against native SQLite.

The fixture provider isolates service rules. This does not establish parity for
the original System.Data.SQLite provider or full inventory serialization.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import subprocess
import sqlite3
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Services/InventoryService/XInventoryService.cs", "OpenSim/Data/IXInventoryData.cs",
           "OpenSim/Framework/InventoryFolderBase.cs", "OpenSim/Framework/InventoryNodeBase.cs",
           "OpenSim/Framework/InventoryCollection.cs"]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "OpenSim/Framework/InventoryItemBase.cs").read_text(encoding="utf-8-sig")
    start = source.index("public string CreatorIdentification")
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    extracted = work / "Creator.cs"
    extracted.write_text("using System;namespace OpenSim.Framework { public partial class InventoryItemBase {" + source[start:end] + "}}")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    for path in [*(ROOT / path for path in SOURCES), extracted, *(Path(__file__).parent / "inventory_reference" / name for name in ("Program.cs", "Stubs.cs"))]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes"]:
        dependency = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(dependency, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "InventoryReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/InventoryReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    seeds = list(range(64)) + [32767, 65534, 65535, 65536]
    data = "".join(f"{seed}\n" for seed in seeds)
    rows = []
    for executable in [command, [args.native]]:
        run = subprocess.run(executable, input=data, text=True, capture_output=True, timeout=90)
        if run.returncode:
            raise RuntimeError(run.stderr)
        rows.append(run.stdout.splitlines())
    if any(len(values) != len(seeds) for values in rows):
        raise AssertionError("Inventory probe output length differs")
    differences = [{"seed": seed, "reference": expected, "native": actual} for seed, expected, actual in zip(seeds, *rows) if expected != actual or expected.startswith("ERROR:")]
    migration_path = "OpenSim/Data/SQLite/Resources/XInventoryStore.migrations"
    sql = (ROOT / migration_path).read_text().split(":VERSION 1", 1)[1].split(":VERSION 2", 1)[0]
    def schema(connection):
        result = {}
        for table in ("inventoryfolders", "inventoryitems"):
            columns = connection.execute(f"PRAGMA table_info({table})").fetchall()
            indexes = sorted((name, connection.execute(f"PRAGMA index_info('{name}')").fetchall()) for _, name, *_ in connection.execute(f"PRAGMA index_list({table})").fetchall())
            result[table] = {"columns": columns, "indexes": indexes}
        return result
    with closing(sqlite3.connect(":memory:")) as original:
        original.executescript(sql)
        expected_schema = schema(original)
    with tempfile.TemporaryDirectory(prefix="schema-", dir=work) as directory:
        database = Path(directory) / "inventory.db"
        create = subprocess.run([args.native, "--initialize", str(database)], capture_output=True, text=True)
        if create.returncode:
            raise RuntimeError(create.stderr)
        with closing(sqlite3.connect(database)) as native:
            actual_schema = schema(native)
    if expected_schema != actual_schema:
        differences.append({"schemaExpected": expected_schema, "schemaNative": actual_schema})
    report = {"passed": not differences, "scenarioCount": len(seeds), "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES + [migration_path, "OpenSim/Framework/InventoryItemBase.cs"]}, "providerScope": "Original service with in-memory reference fixture; native SQLite provider", "initialSchemaMatchesMigration": expected_schema == actual_schema, "differences": differences}
    (work / "inventory-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(json.dumps(differences[:3], indent=2))
    print(f"PASS: {len(seeds)} inventory service scenarios against original source")


if __name__ == "__main__":
    main()
