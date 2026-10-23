"""Offline first-run preparation. Public package contains no generated game code."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_dump import resolve_dumps

SUPPORTED_RPX = 'ba58da5b95ce929e005d058ceb08b9b2788d1ab2bbc8a6c189bbadca0bb34d30'

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def read_settings(path):
    return dict(line.split('=', 1) for line in Path(path).read_text(encoding='utf-8').splitlines() if '=' in line)

def validate_dump(settings):
    game = Path(settings.get('game', ''))
    update = Path(settings['update']) if settings.get('update') else game
    for name, folder in [('base game', game), ('update', update)]:
        if not (folder/'content').is_dir() or not (folder/'code/U-King.rpx').is_file():
            raise ValueError(f'Select the extracted {name} folder containing code/U-King.rpx and content/.')
    rpx = update/'code/U-King.rpx'
    if digest(rpx) != SUPPORTED_RPX:
        raise ValueError('Unsupported game executable. This preview requires your own Wii U USA v208 update (see README).')
    return rpx

def atomic_text(path, text):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name+'.tmp')
    temporary.write_text(text, encoding='utf-8'); temporary.replace(path)

def rsp_argument(value):
    # LLVM response-file quoting, not shell quoting. No shell is used.
    return '"'+str(value).replace('\\', '/').replace('"', '\\"')+'"'

def prepare(package, config, jobs=None):
    package = Path(package).resolve(); config = Path(config).resolve()
    settings = read_settings(config)
    if not settings.get('user'): raise ValueError('Save-data folder is not configured.')
    user = Path(settings['user']).resolve(); user.mkdir(parents=True, exist_ok=True)
    support = package/'support'; manifest = json.loads((support/'build.json').read_text())
    destination = user/'runtime'/manifest['host_id']; executable = destination/'BotWWindowsHeadless.exe'
    status = user/'preparation-status.txt'
    def report(message):
        print(message, flush=True); atomic_text(status, message)
    # Serialize imports as well as game-code compilation across launcher instances.
    import msvcrt
    import_lock = (user/'dump-import.lock').open('a+b')
    if import_lock.tell() == 0: import_lock.write(b'0'); import_lock.flush()
    import_lock.seek(0)
    try:
        try: msvcrt.locking(import_lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError: raise RuntimeError('Game preparation is already running in another launcher.')
        resolved = resolve_dumps(settings, user, support, report)
        rpx = validate_dump(resolved)
        atomic_text(user/'prepared-dumps.ini', '\n'.join(f'{k}={v}' for k, v in {
            'source_game': settings.get('game', ''), 'source_update': settings.get('update', ''),
            'game': resolved['game'], 'update': resolved['update']}.items())+'\n')
    finally:
        import_lock.close()
    if executable.is_file() and (destination/'ready.json').is_file():
        ready = json.loads((destination/'ready.json').read_text())
        if ready.get('host_id') == manifest['host_id'] and ready.get('rpx') == SUPPORTED_RPX and ready.get('exe_sha256') == digest(executable):
            report('Ready to play.'); return executable
    if shutil.disk_usage(user).free < 4*1024**3:
        raise RuntimeError('First-time preparation needs at least 4 GB of free space in the save-data drive.')
    work = user/'preparation'/SUPPORTED_RPX
    work.mkdir(parents=True, exist_ok=True)
    # A kernel-held lock prevents simultaneous launchers compiling the same files.
    import msvcrt
    with (work/'preparation.lock').open('a+b') as lock:
        if lock.tell() == 0: lock.write(b'0'); lock.flush()
        lock.seek(0)
        try: msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError: raise RuntimeError('Preparation is already running. Wait for the other launcher.')
        generated = work/'generated'
        generation_id = manifest['generator_id']
        marker = work/'generation-id.txt'
        if not marker.exists() or marker.read_text() != generation_id or not (generated/'table.cpp').is_file():
            report('Preparing your dump: analyzing and translating the game. This runs once.')
            subprocess.run([sys.executable, '-B', '-u', str(support/'recompiler/tools/recompile.py'), str(rpx), str(generated)], check=True)
            atomic_text(marker, generation_id)
        sources = sorted(generated.glob('*.cpp'))
        if not sources or not (generated/'table.cpp').is_file(): raise RuntimeError('The recompiler did not produce game code.')
        compiler = support/'compiler/bin/clang++.exe'
        includes = support/'recompiler/runtime/include'
        compile_id = hashlib.sha256((manifest['compiler_id']+digest(includes/'ppc.h')+'O2-fwrapv-noalias-no-contract').encode()).hexdigest()[:24]
        objects = work/'objects'/compile_id; objects.mkdir(parents=True, exist_ok=True)
        flags = ['-std=c++20', '-O2', '-w', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-I', str(includes), '-I', str(generated)]
        def compile_source(source):
            key = hashlib.sha256((digest(source)+digest(generated/'hle_decls.h')).encode()).hexdigest()[:24]
            output = objects/(source.stem+'-'+key+'.obj')
            if output.is_file(): return output
            temporary = output.with_suffix('.tmp.obj')
            result = subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(temporary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                print(result.stdout, flush=True)
                raise RuntimeError('Compilation failed for '+source.name+'. See preparation.log.')
            temporary.replace(output); return output
        report(f'Preparing game code: 0 / {len(sources)} files. You can cancel and resume later.')
        completed = []
        workers = jobs or min(4, max(1, (os.cpu_count() or 2)//2))
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [executor.submit(compile_source, source) for source in sources]
            for future in concurrent.futures.as_completed(futures):
                completed.append(future.result())
                report(f'Preparing game code: {len(completed)} / {len(sources)} files.')
        report('Finishing installation: linking the local game runtime.')
        destination.mkdir(parents=True, exist_ok=True)
        temporary_exe = destination/'BotWWindowsHeadless.new.exe'
        libraries = [str(support/'host'/name) if not name.startswith('-') else name for name in manifest['link_libraries']]
        args = ['-static', *map(str, sorted(completed)), '-Wl,--whole-archive', str(support/'host/HostRuntime.a'), '-Wl,--no-whole-archive', *libraries, '-o', str(temporary_exe)]
        response = work/'link.rsp'; atomic_text(response, '\n'.join(map(rsp_argument, args)))
        subprocess.run([str(compiler), '@'+str(response)], check=True)
        shutil.copy2(package/'amd_fidelityfx_vk.dll', destination/'amd_fidelityfx_vk.dll')
        temporary_exe.replace(executable)
        atomic_text(destination/'ready.json', json.dumps({'host_id':manifest['host_id'], 'rpx':SUPPORTED_RPX, 'exe_sha256':digest(executable)}, indent=2))
        report('Preparation complete. Starting the game with your saved settings.')
        return executable

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--package', required=True); parser.add_argument('--config', required=True); parser.add_argument('--jobs', type=int)
    args=parser.parse_args()
    try: prepare(args.package, args.config, args.jobs)
    except Exception as error:
        print('Preparation failed:', error, flush=True)
        try:
            user=read_settings(args.config).get('user')
            if user: atomic_text(Path(user)/'preparation-status.txt', 'Preparation failed: '+str(error))
        except Exception: pass
        return 1
    return 0

if __name__ == '__main__': sys.exit(main())
