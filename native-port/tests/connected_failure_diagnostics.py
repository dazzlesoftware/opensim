"""Preserve a failed fixture's own server stacks before process cleanup."""

import os
import hashlib
from pathlib import Path
import shutil
import subprocess


def capture_server_threads(process, native, work, stack_probe=None):
    if os.name != "nt" or process.poll() is not None:
        return ""
    binary = Path(native).resolve()
    probe = Path(stack_probe) if stack_probe else binary.with_name("windows_thread_stack_probe.exe")
    if not probe.is_file():
        return "\nNative thread stack probe was not built.\n"
    destination = Path(work).resolve() / f"failure-server-{process.pid}-threads.txt"
    archived = ""
    try:
        with binary.open("rb") as source:
            hasher = hashlib.sha256()
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                hasher.update(chunk)
            digest = hasher.hexdigest()
        archive = Path(work).resolve() / f"failure-binary-{digest}"
        archive.mkdir(exist_ok=True)
        # Real copies preserve the old image even when the linker replaces or
        # truncates the build output. Reuse them for repeated failures of a build.
        for source in (binary, binary.with_suffix(".map")):
            target = archive / source.name
            if source.is_file() and not target.is_file():
                temporary = target.with_name(target.name + f".{process.pid}.tmp")
                try:
                    shutil.copy2(source, temporary)
                    os.replace(temporary, target)
                finally:
                    if temporary.exists():
                        temporary.unlink()
        archived = f"Native image SHA256 {digest}; preserved binary/map in {archive}\n"
    except OSError as error:
        archived = f"Could not preserve native binary/map: {error}\n"
    try:
        result = subprocess.run(
            [str(probe), str(process.pid)], capture_output=True, text=True,
            errors="replace", timeout=15,
        )
        output = archived + f"Server PID {process.pid}; stack probe exit {result.returncode}\n"
        output += result.stdout + result.stderr
        destination.write_text(output, encoding="utf-8")
        return f"\nNative thread stacks: {destination}\n" + output
    except subprocess.TimeoutExpired as error:
        output = archived + "Native thread capture timed out\n"
        for partial in (error.stdout, error.stderr):
            if partial:
                output += partial.decode(errors="replace") if isinstance(partial, bytes) else partial
        try:
            destination.write_text(output, encoding="utf-8")
        except OSError:
            pass
        return f"\nPartial native thread stacks: {destination}\n" + output
    except OSError as error:
        # Diagnostics must preserve the original viewer assertion as the failure.
        return f"\nNative thread capture failed: {error}\n"
