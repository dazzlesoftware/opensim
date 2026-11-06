"""Check the legacy FSAsset spool and both on-disk layouts."""

import gzip
import hashlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import tempfile


def check_layout(probe, root, layout):
    spool = root / layout / "spool"
    base = root / layout / "store"
    data = b"\x00\xff" + bytes(range(256))
    digest = hashlib.sha256(data).hexdigest().upper()
    parts = (digest[:3], digest[3:6]) if layout == "osgrid" else (
        digest[:2], digest[2:4], digest[4:6], digest[6:10],
    )
    archive = base.joinpath(*parts, digest)

    def invoke(operation, argument):
        return subprocess.check_output(
            [probe, "--fs-files", operation, str(base), str(spool), layout, argument],
            text=True, timeout=15,
        ).strip()

    assert invoke("read", digest) == "missing"
    assert invoke("read", "../outside") == "missing"
    with ThreadPoolExecutor(max_workers=4) as writers:
        assert list(writers.map(lambda _: invoke("stage", data.hex()), range(8))) == [digest] * 8
    staged = spool / (digest + ".asset")
    assert staged.read_bytes() == data
    assert list((spool / "spool").iterdir()) == []
    assert invoke("read", digest) == data.hex()

    archive.parent.mkdir(parents=True)
    archive.write_bytes(b"older raw data")
    compressed = archive.with_suffix(".gz")
    compressed.write_bytes(gzip.compress(b"older compressed data"))
    assert invoke("read", digest) == data.hex()
    assert invoke("flush", "-") == "1"
    assert not staged.exists()
    assert gzip.decompress(compressed.read_bytes()) == data
    assert invoke("stage", data.hex()) == digest and not staged.exists()
    assert invoke("read", digest) == data.hex()

    compressed.write_bytes(gzip.compress(b"legacy compressed data"))
    assert invoke("read", digest) == b"legacy compressed data".hex()
    compressed.write_bytes(b"corrupt gzip")
    assert invoke("read", digest) == "missing"
    compressed.unlink()
    assert invoke("read", digest) == b"older raw data".hex()
    archive.unlink()
    assert invoke("read", digest) == "missing"

    assert invoke("stage", "") == hashlib.sha256(b"").hexdigest().upper()
    assert invoke("read", hashlib.sha256(b"").hexdigest().upper()) == ""
    assert invoke("flush", "-") == "1"
    assert invoke("read", hashlib.sha256(b"").hexdigest().upper()) == ""

    junkyard = base / "junkyard"
    junkyard.mkdir()
    (junkyard / "1234").write_bytes(b"legacy short digest")
    assert invoke("read", "1234") == b"legacy short digest".hex()

    collision = b"legacy directory obstruction fixture"
    collision_hash = hashlib.sha256(collision).hexdigest().upper()
    shard = base / collision_hash[:(3 if layout == "osgrid" else 2)]
    assert not shard.exists()
    shard.write_bytes(b"old file that blocked a shard")
    assert invoke("stage", collision.hex()) == collision_hash
    assert invoke("flush", "-") == "1"
    assert shard.is_dir()
    assert invoke("read", collision_hash) == collision.hex()
    saved = list(base.glob(shard.name + ".legacy-*"))
    assert len(saved) == 1 and saved[0].read_bytes() == b"old file that blocked a shard"


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="opensim-fs-assets-") as directory:
        for layout in ("core", "osgrid"):
            check_layout(str(Path(sys.argv[1]).resolve()), Path(directory), layout)
    print("PASS: filesystem asset staging, gzip, legacy reads, and both layouts")
