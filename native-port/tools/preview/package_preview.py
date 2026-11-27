"""Assemble clean CMake install stages without copying test databases."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import tarfile
import zipfile


def clean_stage(stage):
    for path in stage.rglob('*'):
        relative = path.relative_to(stage)
        if relative.parts[0] == 'data' or path.name == 'preview.ini' or path.suffix in ['.db', '.sqlite']:
            raise ValueError(f'Test/configuration data in install stage: {relative}')


def render(source, release):
    return source.read_text(encoding='utf-8-sig').replace('%RELEASE%', release).replace(
        "'local-preview'", repr(release)).replace('\r\n', '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--release', required=True)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--image-tar', type=Path)
    args = parser.parse_args()
    if not args.release or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789._-' for c in args.release):
        raise ValueError('Release must be a Docker-compatible lowercase tag')
    root = args.root.resolve()
    tools = Path(__file__).resolve().parent
    windows, linux = root / 'windows-amd64', root / 'linux-amd64'
    for stage, binary in [(windows, 'opensim-native.exe'), (linux, 'opensim-native')]:
        if not (stage / 'bin' / binary).is_file():
            raise ValueError(f'Missing installed runtime: {stage}')
        clean_stage(stage)
        (stage / 'README.txt').write_text(render(tools / 'README.txt', args.release), encoding='utf-8', newline='\n')
        shutil.copy2(tools / 'preview.ini.template', stage / 'share/opensim-native/preview.ini.template')
    shutil.copy2(tools / 'Start-Preview.ps1', windows / 'Start-Preview.ps1')
    for name in ['start-preview.sh', 'Dockerfile', '.dockerignore']:
        (linux / name).write_text(render(tools / name, args.release), encoding='utf-8', newline='\n')
    (root / 'README.txt').write_text(render(tools / 'README.txt', args.release), encoding='utf-8', newline='\n')
    (root / 'Start-Linux-Docker.ps1').write_text(
        render(tools / 'Start-Linux-Docker.ps1', args.release), encoding='utf-8', newline='\n')
    if args.prepare_only:
        print('Prepared clean Windows/Linux stages and release-specific launchers')
        return
    verification = json.loads((root / 'verification.json').read_text(encoding='utf-8-sig'))
    if verification['release'] != args.release:
        raise ValueError('Verification belongs to a different release')
    shutil.copy2(root / 'verification.json', windows / 'verification.json')
    archives = []
    destination = root / 'opensim-native-preview-windows-amd64.zip'
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(windows.rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(windows).as_posix())
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise ValueError('Windows archive integrity failed')
    archives.append(destination)
    destination = root / 'opensim-native-preview-linux-amd64.tar.gz'
    with tarfile.open(root / 'linux-amd64.stage.tar') as stage, tarfile.open(destination, 'w:gz') as archive:
        for member in stage:
            parts = Path(member.name).parts
            if member.name.startswith('/') or '..' in parts or 'data' in parts:
                raise ValueError(f'Unsafe Linux stage member: {member.name}')
            archive.addfile(member, stage.extractfile(member) if member.isfile() else None)
        extras = {'./start-preview.sh': (linux / 'start-preview.sh', 0o755),
                  './Dockerfile': (linux / 'Dockerfile', 0o644),
                  './.dockerignore': (linux / '.dockerignore', 0o644),
                  './README.txt': (root / 'README.txt', 0o644),
                  './verification.json': (root / 'verification.json', 0o644),
                  './share/opensim-native/preview.ini.template': (tools / 'preview.ini.template', 0o644)}
        for name, (path, mode) in extras.items():
            content = path.read_bytes()
            member = tarfile.TarInfo(name)
            member.mode, member.size = mode, len(content)
            archive.addfile(member, io.BytesIO(content))
    with tarfile.open(destination) as archive:
        if not archive.getmember('./bin/opensim-native').mode & 0o111:
            raise ValueError('Linux executable mode was lost')
        if not archive.getmember('./.dockerignore').isfile():
            raise ValueError('Linux archive Docker ignore file is missing')
    archives.append(destination)
    if args.image_tar:
        image = root / 'opensim-native-preview-linux-amd64.image.tar.gz'
        with args.image_tar.open('rb') as source, gzip.open(image, 'wb', compresslevel=6) as target:
            shutil.copyfileobj(source, target)
        archives.append(image)
        destination = root / 'opensim-native-preview-linux-docker.zip'
        with zipfile.ZipFile(destination, 'w', zipfile.ZIP_STORED) as archive:
            for path in [image, root / 'Start-Linux-Docker.ps1', root / 'README.txt', root / 'verification.json']:
                archive.write(path, path.name)
            for path in sorted((root / 'evidence').rglob('*.json')):
                archive.write(path, 'evidence/' + path.relative_to(root / 'evidence').as_posix())
        with zipfile.ZipFile(destination) as archive:
            if archive.testzip() is not None:
                raise ValueError('Docker bundle integrity failed')
        archives.append(destination)
    sums = []
    for path in archives:
        with path.open('rb') as source:
            sums.append(f'{hashlib.file_digest(source, "sha256").hexdigest()}  {path.name}')
    (root / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({path.name: path.stat().st_size for path in archives}, indent=2))


if __name__ == '__main__':
    main()
