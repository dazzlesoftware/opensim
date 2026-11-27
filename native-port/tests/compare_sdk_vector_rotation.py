"""Compare the actual bundled SDK vector/quaternion operator with native arithmetic."""
import argparse
import itertools
from pathlib import Path
import random
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def bits(number):
    return struct.unpack("<I", struct.pack("<f", number))[0]


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
    for key, value in {"TargetFramework": "net8.0", "OutputType": "Exe", "EnableDefaultItems": "false"}.items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(project, "ItemGroup")
    ET.SubElement(items, "Compile", Include=str(Path(__file__).with_name("sdk_vector_rotation_reference.cs")))
    reference = ET.SubElement(items, "Reference", Include="OpenMetaverseTypes")
    ET.SubElement(reference, "HintPath").text = str(ROOT / "bin/OpenMetaverseTypes.dll")
    path = work / "Reference.csproj"
    ET.ElementTree(project).write(path)
    built = subprocess.run([args.dotnet, "build", str(path), "-c", "Release", "-v", "quiet", "--nologo"],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    vectors = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (3.1, -2.3, 7.75), (0, -0.0, 0),
               (1e-20, -1e-20, 2e-20), (1e20, -1e20, 3e20)]
    rotations = [(0,0,0,0), (0,0,0,1), (0,0,.7071,.7071), (1,2,3,4), (.125,-.25,.5,.75)]
    cases = [tuple(map(bits, v + q)) for v, q in itertools.product(vectors, rotations)]
    edge = (0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
            0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000)
    for value, component in itertools.product(edge, range(7)):
        row = list(map(bits, (3.1, -2.3, 7.75, .125, -.25, .5, .75)))
        row[component] = value
        cases.append(tuple(row))
    rng = random.Random(89941)
    cases += [tuple(bits(rng.uniform(-20, 20)) for _ in range(7)) for _ in range(20000)]
    # Include finite values across the full exponent range, where matrix
    # intermediates can overflow or underflow even though every input is finite.
    def finite_bits():
        value = rng.getrandbits(32)
        return value ^ 0x00800000 if value & 0x7f800000 == 0x7f800000 else value

    cases += [tuple(finite_bits() for _ in range(7)) for _ in range(20000)]
    # Mixed NaN signs expose operand exchanges hidden by single-NaN probes.
    cases += list(itertools.product((0, 0x80000000, 0x3f800000, 0x7fc00000, 0xffc00000), repeat=7))
    special = edge + (0x7f800001, 0xff800001, 0x7fc12345, 0xffc54321)
    cases += [tuple(rng.choice(special) for _ in range(7)) for _ in range(20000)]
    payload = "".join(" ".join(f"{value:08x}" for value in row) + "\n" for row in cases)
    outputs = []
    for command in ([args.native, "--probe"], [args.dotnet, str(work / "bin/Release/net8.0/Reference.dll")]):
        run = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, run.stdout[-2000:] + run.stderr[-2000:]
        outputs.append(run.stdout.splitlines())
    assert len(outputs[0]) == len(outputs[1]) == len(cases), "Missing SDK operator observations"
    for index, (case, native, original) in enumerate(zip(cases, *outputs)):
        assert native == original, f"SDK rotation {index} {case}: native {native}, SDK {original}"
    print(f"PASS: {len(cases)} bundled SDK vector/quaternion rotation observations")


if __name__ == "__main__":
    main()
