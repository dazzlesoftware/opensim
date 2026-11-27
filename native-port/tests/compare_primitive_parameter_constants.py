"""Compile every original PRIM integer constant through the native LSL parser."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / "OpenSim/Region/ScriptEngine/Shared/Api/Runtime/LSL_Constants.cs").read_text(encoding="utf-8-sig")
    rows = re.findall(r"public const int (PRIM_\w+)\s*=\s*([^;]+);", source)
    expected = [f"{name}={int(value.strip(), 0)}" for name, value in rows]
    run = subprocess.run([args.native, "--constants"], input="\n".join(name for name, _ in rows) + "\n",
                         capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert run.stdout.splitlines() == expected, "Native primitive constant values differ from original constants"
    print(f"PASS: {len(rows)} original primitive constants compile and execute")


if __name__ == "__main__":
    main()
