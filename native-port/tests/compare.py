"""Compile the actual reference sources, run identical inputs, compare observations.

.NET is used only for this optional migration test, never by the native library.
No NuGet dependencies or copies of the reference algorithms are used.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import random
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    "OpenSim/Framework/Crc32.cs",
    "OpenSim/Framework/CircularBuffer.cs",
    "OpenSim/Framework/BasicDOSProtector.cs",
    "OpenSim/Framework/PrimeNumberHelper.cs",
    "OpenSim/Framework/PermissionsUtil.cs",
    "OpenSim/Framework/ThrottleOutPacketType.cs",
    "OpenSim/Region/ClientStack/Linden/UDP/TokenBucket.cs",
    "OpenSim/Region/ClientStack/Linden/UDP/ThrottleRates.cs",
]


def build_reference(dotnet, work):
    work.mkdir(parents=True, exist_ok=True)
    sdks = subprocess.check_output([dotnet, "--list-sdks"], text=True)
    majors = [int(m) for m in re.findall(r"^(\d+)\.", sdks, re.MULTILINE)]
    if not majors or max(majors) < 8:
        raise RuntimeError("The reference comparison requires a .NET SDK >= 8")
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    # prebuild.xml targets net8_0. Do not silently use the newest runtime:
    # unchecked float-to-int behavior differs between .NET 8 and later releases.
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe",
                       "EnableDefaultCompileItems": "false", "Nullable": "disable",
                       "NoWarn": "SYSLIB0021;CS0436"}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    for source in SOURCES:
        ET.SubElement(items, "Compile", Include=str(ROOT / source))
    for source in ("Program.cs", "Stubs.cs"):
        ET.SubElement(items, "Compile", Include=str(Path(__file__).parent / "reference" / source))
    # PermissionMask is declared within the large Util.cs. Extract that exact
    # declaration rather than reimplementing it in the reference adapter.
    util = (ROOT / "OpenSim/Framework/Util.cs").read_text(encoding="utf-8-sig")
    match = re.search(r"public enum PermissionMask\s*:\s*uint\s*\{[^}]+\}", util)
    if not match:
        raise RuntimeError("Cannot locate PermissionMask in the original Util.cs")
    (work / "PermissionMask.cs").write_text(
        "namespace OpenSim.Framework {\n" + match.group() + "\n}\n", encoding="utf-8")
    ET.SubElement(items, "Compile", Include=str(work / "PermissionMask.cs"))
    def method(name):
        declaration = re.search(r"public static Int32 " + name + r"\(Int32[^)]*\)\s*\{", util)
        if not declaration:
            raise RuntimeError(f"Cannot locate {name} in original Util.cs")
        end = declaration.end()
        depth = 1
        while depth:
            if util[end] == "{":
                depth += 1
            elif util[end] == "}":
                depth -= 1
            end += 1
        return util[declaration.start():end]
    arithmetic = "using System; namespace OpenSim.Framework { public static partial class Util {\n"
    arithmetic += re.search(r"const Int32 EnvironmentTickCountMask = [^;]+;", util).group()
    arithmetic += method("EnvironmentTickCountSubtract") + method("EnvironmentTickCountCompare") + "\n}}\n"
    (work / "TickArithmetic.cs").write_text(arithmetic, encoding="utf-8")
    ET.SubElement(items, "Compile", Include=str(work / "TickArithmetic.cs"))
    project_path = work / "Reference.csproj"
    ET.ElementTree(project).write(project_path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run([dotnet, "build", str(project_path), "-c", "Release", "-o", str(work / "app"),
                            "--nologo", "-v", "quiet"], capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    return [dotnet, str(work / "app/Reference.dll")]


def scenarios():
    yield from (f"prime {n}" for n in range(-100, 20001))
    for n in (2147483647, 2147483646, 2147483629, -2147483648, -2147483647,
              7199368, 7199369, 7199370, 7199380, 10000000):
        yield f"prime {n}"
    rng = random.Random(82563)
    for source in range(16):
        for target in range(16):
            yield f"perm {source} {target << 13}"
    for _ in range(4096):
        yield f"perm {rng.getrandbits(32)} {rng.getrandbits(32)}"
    for _ in range(32):
        yield f"log {rng.getrandbits(32)} {rng.getrandbits(32)} {rng.getrandbits(32)}"
    for size in list(range(65)) + [127, 128, 255, 256, 257, 1024, 8192]:
        data = bytes(rng.randrange(256) for _ in range(size)).hex() or "-"
        for poly, seed in ((0xedb88320, 0xffffffff), (0x82f63b78, 0xffffffff),
                           (0xedb88320, 0), (0, 0xffffffff), (0xa833982b, 0x12345678)):
            for start, length in ((0, size), (size // 3, size // 2), (size, 0)):
                yield f"crc {poly} {seed} {data} {start} {length}"
    for mode in range(5):
        for client in (-2147483648, -1, 0, 6249, 6250, 6251, 640000, 999999, 1000000, 1000001, 2147483647):
            for minimum in (-1, 0, 32000, 2147483647):
                yield f"throttle {mode} {client} {minimum}"
    yield from ["time 100", "new 0 -1 6000 15000 -1", "new 1 0 3000 3000 -1",
                "new 2 0 5000 5000 -1", "state 0", "state 1", "state 2",
                "remove 1 100", "time 101", "remove 1 3000", "check 1 0",
                "time 101.25", "remove 1 1", "time 101.5", "remove 1 1",
                "max 0 1", "state 1", "rate 0 1000", "state 1", "state 2",
                "rate 1 -100", "state 0", "state 1", "burst 1 -100",
                "unregister 0 2", "state 0", "state 1", "rate 2 7000", "state 0",
                "new 3 1 2000 9000 -1", "state 0", "state 1", "state 3",
                "time 1100", "remove 3 1", "time 1101", "remove 3 10000",
                "time 1099", "remove 3 -100", "time 1102", "remove 3 -100",
                "bytes 3 0", "bytes 3 20", "bytes 3 2147483647"]
    yield "new 4 -1 200000 7500 1"
    yield "new 5 4 100000 7500 -1"
    yield "new 6 4 200000 7500 -1"
    for op in ("state 4", "state 5", "ack 4 1000", "expire 4 1", "expire 4 100",
               "adjust 4 300000", "max 4 40000", "adjust 4 -1", "enabled 4 0", "ack 4 7"):
        yield op
    yield "new 7 -1 200000 7500 0"
    yield "enabled 7 1"
    yield "expire 7 2"
    yield "ack 7 1000"
    yield "bytes 5 2147483647"
    yield "bytes 5 -2147483648"
    yield "expire 4 -1000"
    yield from [
        "buffer_new 0 3 0", "buffer_single 0 1", "buffer_single 0 2", "buffer_single 0 3",
        "buffer_single 0 4", "buffer_get 0", "buffer_single 0 4", "buffer_contains 0 4",
        "buffer_copy 0 2 0 2 3", "buffer_remove 0 999", "buffer_take 0 5",
        "buffer_get 0", "buffer_clear 0", "buffer_bulk 0 1 3 9 1 2 3 4",
        "buffer_get 0", "buffer_resize 0 5", "buffer_single 0 6", "buffer_skip 0 1",
        "buffer_clear 0", "buffer_resize 0 0", "buffer_single 0 1", "buffer_new 1 2 1",
        "buffer_single 1 1", "buffer_single 1 2", "buffer_single 1 3", "buffer_take 1 5",
        "buffer_clear 1", "buffer_bulk 1 0 3 1 2 3", "buffer_skip 1 1",
        "buffer_read 1 1 1 4", "buffer_raw 1 1 99", "buffer_resize 1 -1",
        "buffer_bulk 1 -1 1 1", "buffer_copy 1 0 5 1 1", "buffer_skip 1 100",
        "buffer_get 1", "buffer_clear 1", "buffer_new 2 0 1", "buffer_single 2 1",
        "buffer_new 3 -1 0", "buffer_new 4 1 0", "buffer_bulk 4 0 -1 1",
        "buffer_read 4 0 -1 0", "buffer_clear 4",
    ]
    for step in range(1000):
        if step % 23 == 0:
            yield "buffer_clear 0"
            yield f"buffer_resize 0 {rng.randrange(0, 12)}"
        op = rng.choice(["single", "bulk", "get", "take", "skip", "contains", "remove", "resize", "copy"])
        if op in ("single", "contains", "remove"):
            yield f"buffer_{op} 0 {rng.randrange(-10, 10)}"
        elif op == "bulk":
            yield f"buffer_bulk 0 {rng.randrange(-1, 3)} {rng.randrange(-1, 6)} 1 2 3 4 5"
        elif op == "get":
            yield "buffer_get 0"
        elif op in ("take", "skip", "resize"):
            yield f"buffer_{op} 0 {rng.randrange(-1, 10)}"
        else:
            yield f"buffer_copy 0 1 {rng.randrange(-1, 3)} {rng.randrange(-1, 4)} 4"
    # Long deterministic trace exercises repeated refill, debt, burst caps,
    # parent oversubscription, rate changes, and adaptive feedback.
    now = 2000.0
    for step in range(1000):
        now += rng.choice([0, .001, .02, .25, 1, 10])
        yield f"time {now:.6f}"
        leaf = rng.choice([2, 3, 5, 6])
        yield f"remove {leaf} {rng.randrange(1, 16000)}"
        if step % 7 == 0:
            yield f"rate {leaf} {rng.randrange(0, 400000)}"
        if step % 11 == 0:
            yield f"burst {leaf} {rng.randrange(-1000, 20000)}"
        if step % 13 == 0:
            yield f"ack 4 {rng.randrange(0, 5000)}"
        if step % 17 == 0:
            yield "expire 4 1"
        yield f"bytes {leaf} {rng.randrange(1, 1000)}"
    for now in (0, 1, 10000, 0x3fffffff - 10, 0x3fffffff):
        for a in (0, 1, 10000, 0x3fffffff - 10, 0x3fffffff):
            for b in (0, 1, 10000, 0x3fffffff - 10, 0x3fffffff):
                yield f"ticks {now} {a} {b}"
    yield from [
        "ticktime 1000", "dos_new 0 3 1000 5000 0 0",
        "dos_process 0 A", "dos_process 0 A", "dos_process 0 A", "dos_process 0 A",
        "dos_process 0 A", "dos_process 0 A", "dos_process 0 A", "dos_process 0 B",
        "dos_process 0 A", "ticktime 6000", "dos_cleanup 0", "dos_process 0 A",
        "ticktime 6001", "dos_cleanup 0", "dos_process 0 A",
        "dos_new 1 100 1000 1000 1 0", "dos_process 1 A", "dos_process 1 A",
        "dos_process 1 A", "dos_process 1 A", "dos_end 1 A", "dos_end 1 unknown",
        "dos_cleanup 1", "dos_process 1 A",
        "dos_new 2 1 1000 1000 0 1", "dos_process 2 A", "dos_process 2 A",
        "dos_process 2 A", "dos_process 2 A", "dos_cleanup 2", "dos_process 2 A",
        "dos_new 3 0 1000 1000 1 0", "dos_process 3 A", "dos_process 3 A",
        "dos_new 4 1 0 1000 1 0", "dos_process 4 A", "dos_process 4 A",
        "ticktime 1073741820", "dos_new 5 2 10 100 0 0", "dos_process 5 A",
        "dos_process 5 A", "ticktime 2", "dos_process 5 A", "dos_process 5 A",
        "dos_process 5 A", "dos_cleanup 5", "dos_process 5 A",
    ]
    for instance in range(6, 12):
        yield f"dos_new {instance} {rng.randrange(1, 6)} {rng.choice([1, 100, 1000])} 1000 {rng.randrange(0, 3)} {instance % 2}"
    tick = 10000
    for step in range(2000):
        tick = (tick + rng.choice([0, 0, 0, 1, 10, 100, 1000])) & 0x3fffffff
        yield f"ticktime {tick}"
        instance = rng.randrange(6, 12)
        key = rng.choice(["A", "A", "A", "B", "C"])
        yield f"dos_process {instance} {key}"
        if step % 3 == 0:
            yield f"dos_end {instance} {key}"
        if step % 11 == 0:
            yield f"dos_cleanup {instance}"


def equal_value(left, right, float_column):
    if left == right:
        return True
    if not float_column:
        return False
    try:
        a, b = float(left), float(right)
        return (math.isnan(a) and math.isnan(b)) or math.isclose(a, b, rel_tol=2e-6, abs_tol=2e-4)
    except ValueError:
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    reference = build_reference(args.dotnet, work)
    reference_runtime = json.loads(subprocess.check_output(reference + ["--runtime"], text=True))
    commands = list(scenarios())
    data = "\n".join(commands) + "\n"
    outputs = []
    for command in (reference, [args.native]):
        result = subprocess.run(command, input=data, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"Probe failed ({command[0]}):\n{result.stderr}\n{result.stdout[-2000:]}")
        outputs.append(result.stdout.splitlines())
    if any(len(output) != len(commands) for output in outputs):
        raise AssertionError(f"Expected {len(commands)} rows, got {[len(o) for o in outputs]}")
    differences = []
    for row, (expected, actual) in enumerate(zip(*outputs)):
        left, right = expected.split("|"), actual.split("|")
        float_columns = set(range(2, 9)) if left[0] == "bucket" else ({5} if left[0] == "throttle" else set())
        if len(left) != len(right) or any(not equal_value(a, b, col in float_columns)
            for col, (a,b) in enumerate(zip(left,right))):
            differences.append({"command": commands[row], "reference": expected, "native": actual})
    report = {
        "passed": not differences,
        "referenceTargetFramework": "net8.0",
        "referenceRuntime": reference_runtime,
        "scenarioCount": len(commands),
        "operations": dict(Counter(c.split()[0] for c in commands)),
        "referenceSha256": {source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
                            for source in SOURCES + ["OpenSim/Framework/Util.cs"]},
        "floatTolerance": {"relative": 2e-6, "absolute": 2e-4},
        "differences": differences[:20],
    }
    (work / "parity-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if differences:
        raise AssertionError(f"{len(differences)} differences:\n" + json.dumps(differences[:10], indent=2))
    print(f"PASS: {len(commands)} observations match the original C# source implementations")
    print(f"Report: {work / 'parity-results.json'}")


if __name__ == "__main__":
    main()
