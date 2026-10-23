"""Import owned Wii U install packages to a separate local extracted cache."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

TITLES = {'game': 0x00050000101C9400, 'update': 0x0005000E101C9400}

def extracted(folder):
    return (folder/'code/U-King.rpx').is_file() and (folder/'content').is_dir()

def inspect_package(folder, kind):
    folder = Path(folder).resolve()
    if extracted(folder):
        return None
    if not (folder/'title.tmd').is_file() or not (folder/'title.tik').is_file():
        raise ValueError(f'Select an extracted {kind} (code/ and content/) or an install package with title.tmd, title.tik and .app files.')
    tmd = (folder/'title.tmd').read_bytes()
    ticket = (folder/'title.tik').read_bytes()
    if len(tmd) < 0xB04 or tmd[:4] != b'\x00\x01\x00\x04':
        raise ValueError(f'Invalid or unsupported {kind} title.tmd.')
    title = int.from_bytes(tmd[0x18C:0x194], 'big')
    version, count = struct.unpack_from('>HH', tmd, 0x1DC)
    if title != TITLES[kind] or (kind == 'update' and version != 208):
        raise ValueError(f'Wrong {kind} package: requires USA {TITLES[kind]:016X}' + (' v208.' if kind == 'update' else '.'))
    if len(ticket) < 0x2A4 or ticket[:4] != b'\x00\x01\x00\x04' or ticket[0x1DC:0x1E4] != tmd[0x18C:0x194]:
        raise ValueError(f'The {kind} ticket is invalid or belongs to a different title.')
    if not count or len(tmd) < 0xB04 + count*48:
        raise ValueError(f'Truncated {kind} content table.')
    fingerprint = hashlib.sha256(str(folder).encode('utf-8') + tmd + ticket)
    total = 0
    for i in range(count):
        content, index, flags, size = struct.unpack_from('>IHHQ', tmd, 0xB04+i*48)
        app = folder/f'{content:08x}.app'
        if not app.is_file() or app.stat().st_size < size:
            raise ValueError(f'Missing or truncated {kind} content: {app.name}. Re-dump the complete title.')
        stat = app.stat()
        fingerprint.update(f'{content}:{stat.st_size}:{stat.st_mtime_ns}'.encode())
        total += stat.st_size
    return {'fingerprint': fingerprint.hexdigest(), 'bytes': total, 'title': f'{title:016X}', 'version': version}

def import_dump(folder, kind, info, user, tool, report):
    folder = Path(folder).resolve()
    if info is None:
        return folder
    target = Path(user)/'imported-dumps'/info['fingerprint']
    marker = target/'import-complete.json'
    if marker.is_file() and extracted(target):
        try:
            if json.loads(marker.read_text()) == info:
                report(f'Using previously prepared {kind} install package.')
                return target
        except (ValueError, OSError):
            pass
    tool = Path(tool)
    if not tool.is_file():
        raise RuntimeError('Bundled install-package extraction tool is missing. Extract the complete portable ZIP again.')
    target.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(target.parent).free < info['bytes'] + 4*1024**3:
        raise RuntimeError(f'Not enough space to extract the {kind}. Allow its package size plus 4 GB for preparation.')
    staging = target.with_name(target.name+'.partial')
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()
    report(f'Extracting your {kind} install package to local storage. The source folder is preserved.')
    # CDecrypt receives two directory arguments, so it never extracts into the source.
    # Its verbose output goes to preparation.log. Each import is committed only on success.
    subprocess.run([str(tool.resolve()), str(folder), str(staging.resolve())], check=True)
    if not extracted(staging):
        raise RuntimeError(f'The {kind} package did not produce code/U-King.rpx and content/. See preparation.log.')
    (staging/'import-complete.json').write_text(json.dumps(info), encoding='utf-8')
    if target.exists():
        shutil.rmtree(target)
    staging.replace(target)
    return target

def resolve_dumps(settings, user, support, report):
    if not settings.get('game'):
        raise ValueError('Select your base game folder.')
    sources = {'game': Path(settings['game']), 'update': Path(settings['update']) if settings.get('update') else None}
    # Validate both inputs before spending time extracting either one.
    inspected = {kind: inspect_package(folder, kind) for kind, folder in sources.items() if folder is not None}
    if inspected['game'] is not None and sources['update'] is None:
        raise ValueError('An install-package base game needs a separate USA v208 update folder.')
    resolved = {kind: import_dump(folder, kind, inspected[kind], user, Path(support)/'cdecrypt/cdecrypt.exe', report)
                for kind, folder in sources.items() if folder is not None}
    return {**settings, 'game': str(resolved['game']), 'update': str(resolved.get('update', ''))}
