"""Capture a test-owned process and prove that its threads resume afterward."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

from connected_failure_diagnostics import capture_server_threads


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    child = subprocess.Popen(
        [sys.executable, "-u", "-c",
         "import sys; print('ready', flush=True); sys.stdin.readline(); print('resumed', flush=True)"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    ready = threading.Event()
    lines = []

    def read_ready():
        lines.append(child.stdout.readline())
        ready.set()

    threading.Thread(target=read_ready, daemon=True).start()
    try:
        assert ready.wait(5) and lines == ["ready\n"], "Diagnostic child did not start"
        with tempfile.TemporaryDirectory() as work:
            diagnostic = capture_server_threads(child, sys.executable, work, stack_probe=args.probe)
            artifact = Path(work) / f"failure-server-{child.pid}-threads.txt"
            assert artifact.is_file(), diagnostic
            output = artifact.read_text(encoding="utf-8")
            assert "stack probe exit 0" in output and "THREAD " in output and "module=" in output, output
            assert str(artifact) in diagnostic
            archives = list(Path(work).glob("failure-binary-*"))
            assert len(archives) == 1 and (archives[0] / Path(sys.executable).name).is_file(), diagnostic
        stdout, stderr = child.communicate("continue\n", timeout=5)
        assert child.returncode == 0 and stdout == "resumed\n", stdout + stderr
        invalid = subprocess.run([args.probe, "0"], capture_output=True, timeout=5)
        assert invalid.returncode != 0, "Stack probe accepted the idle process"
        print("PASS: captured live child threads, preserved diagnostics and resumed execution")
    finally:
        if child.poll() is None:
            child.kill()
        child.communicate()


if __name__ == "__main__":
    main()
