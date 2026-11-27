"""Compare endpoint identity with original GetEndPoint, cache, and actual SDK.

Each mutable address slot models the receiver's reused SocketAddress. Hash masks
force collisions; the real SDK hash seed is supplied to the native comparison.
"""

import argparse
import hashlib
import ipaddress
import json
import re
from pathlib import Path
import socket
import subprocess
import sys
import xml.etree.ElementTree as ET

from compare_primitive_parameter_query import ROOT, method


def address(index, ipv6=False):
    port = (1000 + index) % 65536
    if ipv6:
        value = bytearray(28)
        prefix = "fe80::" if ipv6 == "link" else "2001:db8::"
        value[8:24] = ipaddress.IPv6Address(int(ipaddress.IPv6Address(prefix)) + index).packed
        value[24:28] = (index % 8).to_bytes(4, sys.byteorder)
    else:
        value = bytearray(16)
        value[4:8] = ipaddress.IPv4Address(int(ipaddress.IPv4Address("127.0.0.1")) + index).packed
    family = socket.AF_INET6 if ipv6 else socket.AF_INET
    if sys.platform == "darwin":
        value[0:2] = bytes((len(value), family))
    else:
        value[0:2] = family.to_bytes(2, sys.byteorder)
    value[2:4] = port.to_bytes(2, "big")
    return value.hex(), len(value)


def assign(slot, index, ipv6=False, query=True):
    raw, size = address(index, ipv6)
    return f"{'g' if query else 'a'}{slot},{raw},{size}"


def cases():
    flows = []
    for ipv6 in (False, True, "link"):
        a, b, c = (assign(0, i, ipv6) for i in range(3))
        other = assign(1, 0, ipv6)
        for time in (299999, 300000, 300000.9, 300001, 600000, 600001):
            flows.extend([
                f"{a} q0 {other} q1 t{time} f q0 q1",
                f"{a} {b} {c} {a} t{time} f q0",
                f"{a} t10000 q0 t{time} f q0",
                f"{a} {assign(1, 1, ipv6)} t10000 q1 {assign(0, 1, ipv6, False)} t{time} f q0 q1",
            ])
        for length in (3, 7, 17, 37, 89, 197):
            initial = [assign(i, i, ipv6) for i in range(length)]
            refill = [assign(1000 + i, 1000 + i, ipv6) for i in range(length)]
            alias = assign(1000, 1000 + length - 2, ipv6, False)
            flows.append(" ".join(initial + ["t10000", "q0", "t300001", "f"] + refill + [alias, "q1000", "f", "q1000"]))
        if ipv6:
            raw, capacity = address(1, ipv6)
            # The dummy IPv4 endpoint accepts active sizes >=16, while the
            # backing IPv6 buffer remains 28 bytes. Exercise SDK hash tails.
            flows.append(" ".join(f"g0,{raw},{size} q0" for size in range(16, capacity + 1)))
    return [f"{mask} {flow}" for mask in (0, 255, 0xffffffff) for flow in flows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True)
    parser.add_argument("--dotnet", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    util_path = ROOT / "OpenSim/Framework/Util.cs"
    cache_path = ROOT / "OpenSim/Framework/ExpiringCacheOS.cs"
    fixture_path = Path(__file__).with_name("udp_endpoint_cache_reference.cs")
    original = util_path.read_text(encoding="utf-8-sig")
    dummy = re.search(r"private static readonly IPEndPoint dummyIPEndPoint\s*=.*?;", original).group()
    interval = original.split("EndpointsCache = new(", 1)[1].split(");", 1)[0]
    source = fixture_path.read_text().replace("/*GET_ENDPOINT*/", method(original, "public static EndPoint GetEndPoint(SocketAddress sckaddr)"))
    source = source.replace("/*DUMMY_ENDPOINT*/", dummy).replace("/*CACHE_INTERVAL*/", interval)
    (work / "Program.cs").write_text(source)
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    properties = ET.SubElement(project, "PropertyGroup")
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "Nullable": "disable", "NoWarn": "0436"}.items():
        ET.SubElement(properties, key).text = value
    ET.SubElement(ET.SubElement(project, "ItemGroup"), "Compile", Include=str(cache_path))
    project_path = work / "Reference.csproj"
    ET.ElementTree(project).write(project_path)
    built = subprocess.run([args.dotnet, "build", str(project_path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True, timeout=120)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    scenarios = cases()
    reference = subprocess.run([args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")],
                               input="\n".join(scenarios) + "\n", capture_output=True, text=True, timeout=60)
    if reference.returncode:
        raise RuntimeError(reference.stdout[-2000:] + reference.stderr)
    lines = reference.stdout.splitlines()
    assert lines[0].startswith("SEED "), "Missing actual SDK process hash seed"
    seed = int(lines.pop(0).split()[1])
    runtime = lines.pop(0).removeprefix("RUNTIME ")
    socket_assembly = Path(lines.pop(0).removeprefix("SOCKET_ASSEMBLY "))
    hash_assembly = Path(lines.pop(0).removeprefix("HASH_ASSEMBLY "))
    native = subprocess.run([args.native], input="\n".join(f"{seed} {case}" for case in scenarios) + "\n",
                            capture_output=True, text=True, timeout=60)
    if native.returncode:
        raise RuntimeError(native.stdout[-2000:] + native.stderr)
    actual = native.stdout.splitlines()
    assert len(actual) == len(lines) == len(scenarios), "Missing endpoint cache observations"
    differences = [dict(case=case, native=a, original=b) for case, a, b in zip(scenarios, actual, lines) if a != b]
    report = {
        "passed": not differences, "flows": len(scenarios), "differenceCount": len(differences),
        "stateObservations": sum(len(line.split(";")) for line in lines), "sdkHashSeed": seed,
        "referenceRuntime": runtime,
        "sdkAssemblyHashes": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in (socket_assembly, hash_assembly)},
        "applicationParityVerified": False,
        "scope": "Unchanged original GetEndPoint and complete cache with actual SocketAddress/Dictionary/HashCode; "
                 "controlled clock and timer. Mutable keys, IPv4/IPv6, hash collisions, slot reuse, resizing, "
                 "sliding expiry and cached endpoint reference identity. Live socket/admission integration and "
                 "whole-server/platform parity remain separate requirements.",
        "nativeImageSHA256": hashlib.sha256(Path(args.native).read_bytes()).hexdigest(),
        "sourceHashes": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in (util_path, cache_path, fixture_path,
                                      ROOT / "native-port/src/udp_endpoint_cache.cpp",
                                      ROOT / "native-port/include/opensim/network/udp_endpoint_cache.hpp",
                                      ROOT / "native-port/src/udp_socket.cpp",
                                      ROOT / "native-port/tests/udp_endpoint_cache_probe.cpp")},
        "differences": differences[:3],
    }
    (work / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if differences:
        row = differences[0]
        index = next(i for i, (a, b) in enumerate(zip(row["native"].split(";"), row["original"].split(";"))) if a != b)
        raise AssertionError(
            f"Endpoint cache flow {scenarios.index(row['case'])}, state {index}: "
            f"native={row['native'].split(';')[index]!r}; original={row['original'].split(';')[index]!r}; "
            f"see {work / 'results.json'}"
        )
    print(f"PASS: {len(scenarios)} original endpoint cache flows; {report['stateObservations']} identity/hash/cache observations")


if __name__ == "__main__":
    main()
