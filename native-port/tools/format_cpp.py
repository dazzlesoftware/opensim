"""Check first-party C++ formatting, or apply it with --write.

Requires clang-format 22. Pass --formatter when it is not on PATH.
Vendored sources, build outputs, and managed reference fixtures are excluded.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--formatter", default="clang-format")
    parser.add_argument("--write", action="store_true", help="Apply formatting instead of checking")
    args = parser.parse_args()
    formatter = shutil.which(args.formatter)
    if not formatter:
        parser.error("clang-format was not found; provide its path using --formatter")

    root = Path(__file__).resolve().parents[1]
    files = sorted(
        path
        for folder in ("src", "include", "tests")
        for path in (root / folder).rglob("*")
        if path.suffix in (".cpp", ".hpp", ".h")
    )
    options = ["-i"] if args.write else ["--dry-run", "--Werror"]

    def process(path):
        return subprocess.run(
            [formatter, *options, str(path)], capture_output=True, text=True
        )

    failures = 0
    with ThreadPoolExecutor(max_workers=6) as pool:
        for path, result in zip(files, pool.map(process, files)):
            if result.returncode:
                failures += 1
                print(path.relative_to(root))
                print(result.stderr or result.stdout, end="")

    action = "Formatted" if args.write else "Checked"
    print(f"{action} {len(files)} C++ files; {failures} failures")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
