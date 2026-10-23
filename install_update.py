"""Verified portable-bundle update; game folders, config and saves are unmanaged."""
import argparse,hashlib,json,os,re,shutil,stat,subprocess,sys,time,zipfile
from pathlib import Path,PurePosixPath

ROOT='BotWRecomp-Windows/'
ROOT_FILES={'BotWLauncher.exe','Play Breath of the Wild.exe','amd_fidelityfx_vk.dll','README.md','LICENSE','Source.zip','package-manifest.json'}

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def safe_name(name):
    path=PurePosixPath(name)
    if not name or '\\' in name or ':' in name or path.is_absolute() or any(p in ('','.','..') or p.endswith(('.', ' ')) for p in name.split('/')):
        raise ValueError('Unsafe update path: '+name)
    if any(re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?',p,re.I) for p in path.parts):raise ValueError('Reserved Windows path')
    if path.parts[0] not in ('support','licenses') and name not in ROOT_FILES:raise ValueError('Unmanaged update path: '+name)
    if path.suffix.lower() in ('.rpx','.rpl','.wud','.wux','.key','.tik','.tmd'):raise ValueError('Game files cannot be in an update')
    return path

def unpack(archive,output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        infos=[x for x in z.infolist() if not x.is_dir()]
        if len(infos)>40000 or sum(x.file_size for x in infos)>3*1024**3:raise ValueError('Update exceeds extraction limits')
        names={};folded=set()
        for info in infos:
            if not info.filename.startswith(ROOT):raise ValueError('Unexpected ZIP root')
            name=info.filename[len(ROOT):];safe_name(name)
            if stat.S_ISLNK(info.external_attr>>16) or name.casefold() in folded:raise ValueError('Symlink or duplicate ZIP member')
            folded.add(name.casefold());names[name]=info
        manifest=json.loads(z.read(names['package-manifest.json']))
        files=manifest['files']
        if not isinstance(files,dict) or set(files)!=set(names)-{'package-manifest.json'}:raise ValueError('Package manifest differs from archive')
        for name,expected in files.items():
            if not isinstance(expected,str) or not re.fullmatch('[0-9a-f]{64}',expected):raise ValueError('Invalid file checksum')
            target=output.joinpath(*safe_name(name).parts);target.parent.mkdir(parents=True,exist_ok=True)
            with z.open(names[name]) as source,target.open('wb') as destination:shutil.copyfileobj(source,destination)
            if sha(target)!=expected:raise ValueError('Package file checksum mismatch: '+name)
        (output/'package-manifest.json').write_text(json.dumps(manifest,indent=2))
        return files

def install(archive,base,expected,parent_pid=None):
    archive=Path(archive).resolve();base=Path(base).resolve()
    if sha(archive)!=expected:raise ValueError('Downloaded bundle checksum changed')
    staging=archive.parent/'verified';files=unpack(archive,staging)
    if parent_pid:
        import _winapi
        try: parent=_winapi.OpenProcess(0x00100000,False,parent_pid)
        except OSError as error:
            if error.winerror!=87:raise
        else:
            try:
                if _winapi.WaitForSingleObject(parent,60000)!=0:raise RuntimeError('Launcher did not close; update cancelled')
            finally:_winapi.CloseHandle(parent)
    backup=archive.parent/'previous';backup.mkdir(exist_ok=True)
    moved=[]
    try:
        for name in [*files,'package-manifest.json']:
            rel=safe_name(name);target=base.joinpath(*rel.parts)
            current=target
            while current!=base:
                if current.is_symlink() or current.is_junction():raise ValueError('Update destination is a link')
                current=current.parent
            old=backup.joinpath(*rel.parts);old.parent.mkdir(parents=True,exist_ok=True);target.parent.mkdir(parents=True,exist_ok=True)
            existed=target.exists()
            if existed and not target.is_file():raise ValueError('Update target is not a regular file')
            if existed:os.replace(target,old)
            moved.append((target,old,existed))
            os.replace(staging.joinpath(*rel.parts),target)
        return moved
    except Exception:
        rollback(moved);raise

def rollback(moved):
    for target,old,existed in reversed(moved):
        if existed:os.replace(old,target)
        elif target.exists():target.unlink()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--archive',required=True);parser.add_argument('--base',required=True);parser.add_argument('--sha256',required=True);parser.add_argument('--wait-pid',type=int)
    args=parser.parse_args();log=Path(args.archive).parent/'install.log'
    try:
        moved=install(args.archive,args.base,args.sha256,args.wait_pid)
        try:subprocess.Popen([str(Path(args.base)/'BotWLauncher.exe')],cwd=args.base,close_fds=True)
        except Exception:rollback(moved);raise
        log.write_text('Portable package updated. Settings and saves were preserved.\n');return 0
    except Exception as error:
        log.write_text('Update failed; previous files were restored where possible.\n'+str(error)+'\n')
        if os.name=='nt':subprocess.Popen(['notepad.exe',str(log)])
        return 1

if __name__=='__main__':sys.exit(main())
