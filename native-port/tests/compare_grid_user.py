"""Compare the original grid-user service/cache with a native SQLite provider.

The C# provider is a fixture; SQLite provider behavior has separate native tests.
"""
import argparse
from contextlib import closing
import hashlib
import json
import math
from pathlib import Path
import subprocess
import random
import struct
import sqlite3
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["OpenSim/Services/UserAccountService/GridUserService.cs", "OpenSim/Services/UserAccountService/GridUserServiceBase.cs", "OpenSim/Services/Interfaces/IGridUserService.cs", "OpenSim/Data/IGridUserData.cs", "OpenSim/Framework/ExpiringCacheOS.cs"]


def reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultCompileItems": "false", "Nullable": "disable"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    for path in [*(ROOT / path for path in SOURCES), *(Path(__file__).parent / "grid_user_reference" / name for name in ("Program.cs", "Stubs.cs"))]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes", "Nini"]:
        dependency = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(dependency, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "GridUserReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(path), "-c", "Release", "-o", str(work / "app"), "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/GridUserReference.dll")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    command = reference(args.dotnet, work)
    differences = []
    for seed in range(16):
        results = []
        for executable in [command, [args.native]]:
            run = subprocess.run(executable + [str(seed)], capture_output=True, text=True, encoding="utf-8", timeout=30)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            results.append(run.stdout.strip())
        if results[0] != results[1]:
            differences.append({"seed": seed, "reference": results[0], "native": results[1]})
    vectors = ["<1,2,3>", "1,2,3", "123,456,789", "1.0,2.0,3.0", "<1, 2, 3>", "(1,2,3)", "<1,2,3,4>", "prefix<1,2,3>", "<1e2,NaN,Infinity>", "<1;2;3>", "<1,2>", "<.5,-.1,0>", "<1,2,3>suffix", "<NaN,2,3>", "<1,NaN,3>", "<1,2,NaN>", "<∞,2,3>", "<1,2, 3>", "<1,2,3 4>", "<1,2,3", "<1E999,2,3>", "<1E-999,2,3>", "<0.0001,1e-5,1e8>", "<1e9,-0,1.2345678>", "<+NaN,-NaN,NaN>"]
    random_values = random.Random(97139)
    def component():
        value = struct.unpack("<f", struct.pack("<I", random_values.getrandbits(32)))[0]
        if math.isnan(value):
            return "NaN"
        if math.isinf(value):
            return "-∞" if value < 0 else "∞"
        return repr(value)
    vectors.extend("<" + ",".join(component() for _ in range(3)) + ">" for _ in range(512))
    data = "\n".join(vectors) + "\n"
    results = []
    for executable in [command, [args.native]]:
        run = subprocess.run(executable + ["--vectors"], input=data, capture_output=True, text=True, encoding="utf-8", timeout=30)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        results.append(run.stdout.splitlines())
    if any(len(result) != len(vectors) for result in results):
        raise AssertionError("Vector probe length differs")
    differences.extend({"vector": value, "reference": expected, "native": actual} for value, expected, actual in zip(vectors, *results) if expected != actual)
    migration = "OpenSim/Data/PGSQL/Resources/GridUserStore.migrations"
    sql = (ROOT / migration).read_text().split(":VERSION 1", 1)[1].split(":VERSION 2", 1)[0]
    sql = sql[sql.index("BEGIN TRANSACTION;"):]
    with closing(sqlite3.connect(":memory:")) as database:
        database.executescript(sql)
        expected_schema = database.execute("PRAGMA table_info(GridUser)").fetchall()
    with tempfile.TemporaryDirectory(prefix="schema-", dir=work) as directory:
        path = Path(directory) / "grid-user.db"
        run = subprocess.run([args.native, "--initialize", str(path)], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        with closing(sqlite3.connect(path)) as database:
            actual_schema = database.execute("PRAGMA table_info(GridUser)").fetchall()
    if expected_schema != actual_schema:
        differences.append({"schemaExpected": expected_schema, "schemaNative": actual_schema})
    report = {"passed": not differences, "serviceScenarioCount": 16, "vectorScenarioCount": len(vectors), "sourceHashes": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES + [migration]}, "typesAssemblySha256": hashlib.sha256((ROOT / "bin/OpenMetaverseTypes.dll").read_bytes()).hexdigest(), "providerScope": "Original service with fixture provider versus native SQLite", "schemaMatchesPostgresV1Columns": expected_schema == actual_schema, "differences": differences}
    (work / "grid-user-parity-results.json").write_text(json.dumps(report, indent=2) + "\n")
    if differences:
        raise AssertionError(json.dumps(differences[:3], indent=2))
    print(f"PASS: 16 grid-user scenarios and {len(vectors)} vector-text cases")


if __name__ == "__main__":
    main()
