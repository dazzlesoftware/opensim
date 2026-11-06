"""Portable source-derived Darwin policy oracle; does not execute Darwin syscalls."""

import argparse, hashlib, json, re, subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet')
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    meta = json.loads((HERE / 'darwin_file_lock_policy_source.json').read_text(encoding='utf-8'))
    body = (HERE / 'darwin_file_lock_policy_pal_reference.txt').read_bytes()
    assert hashlib.sha256(body).hexdigest() == meta['body_sha256']
    table = {
        name: int(value, 16)
        for name, value in re.findall(
            r'strcmp\(fileSystemName, "([^"]+)"\) == 0\) result = (0x[0-9A-Fa-f]+);',
            body.decode('utf-8'),
        )
    }
    assert len(table) == 125
    names = list(table) + ['smbfs', 'afpfs', 'unknown', 'NFS', 'SMBFS', '', 'apfs ', 'fuse.sshfs']
    expected = [
        str(int(bool(table.get(n, 0)) and table[n] not in [0x6969, 0x517B, 0xFE534D42, 0xFF534D42]))
        for n in names
    ]
    native = subprocess.run(
        [a.native, '--map'],
        input='\n'.join(names) + '\n',
        text=True,
        capture_output=True,
        check=True,
    ).stdout.splitlines()
    assert native == expected
    subprocess.run([a.native], check=True)
    report = dict(
        scope='Parsed original PAL literal decision table plus SafeFileHandle exclusions; portable source policy only, no macOS execution',
        cases=len(names),
        source=meta,
        nativeSha256=hashlib.sha256(Path(a.native).read_bytes()).hexdigest(),
    )
    (a.work / 'darwin-file-lock-policy-results.json').write_text(
        json.dumps(report, indent=2), encoding='utf-8'
    )
    print(json.dumps(report))


if __name__ == '__main__':
    main()
