"""Check getter dispatch coverage and argument consumption against source cases."""
import argparse
from pathlib import Path
import re
import subprocess
from compare_primitive_parameter_query import ROOT, method


def fields(source, signature, constants):
    body = method(source, signature)
    cases = list(re.finditer(r"^ {20}case ScriptBaseClass\.(\w+):", body, re.M))
    result = {}
    for index, case in enumerate(cases):
        name = case[1]
        if name == "PRIM_LINK_TARGET":
            continue
        last = index
        while True:
            end = cases[last + 1].start() if last + 1 < len(cases) else len(body)
            block = body[cases[last].end():end]
            if block.strip() or last + 1 == len(cases):
                break
            last += 1
        result[constants[name]] = bool(re.search(r"rules\.GetIntegerItem\(idx\+\+\)", block))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    args = parser.parse_args()
    source = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs").read_text(encoding="utf-8-sig")
    definitions = (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Runtime/LSL_Constants.cs").read_text(encoding="utf-8-sig")
    constants = {name: int(value) for name, value in re.findall(
        r"public const int (\w+)\s*=\s*(-?\d+)\s*;", definitions)}
    cases = []
    expected = []
    for avatar, parameter in enumerate(("SceneObjectPart part", "ScenePresence avatar")):
        known = fields(source, "public LSL_List GetPrimParams(" + parameter, constants)
        assert known, "Original getter switch was not found"
        for code in range(71):
            cases.append(f"{avatar} 2 {code} 27\n")
            if code == constants["PRIM_LINK_TARGET"]:
                expected.append("|")
            elif code in known and known[code]:
                expected.append(f"{code}:27;|")
            elif code in known:
                expected.append(f"{code}:-;27:-;|")
            else:
                expected.append("27:-;|")
    run = subprocess.run([args.native, "--probe"], input="".join(cases),
                         capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    actual = run.stdout.splitlines()
    assert len(actual) == len(expected), "Missing rule dispatch observations"
    for case, native, original in zip(cases, actual, expected):
        assert native == original, f"{case.strip()}: native {native!r}, source cases {original!r}"
    print(f"PASS: {len(cases)} original prim/avatar rule dispatch and face-argument observations")


if __name__ == "__main__":
    main()
