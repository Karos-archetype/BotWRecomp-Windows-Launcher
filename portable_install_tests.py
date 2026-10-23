import importlib.util,json,hashlib,stat,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parent/'install_update.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
scratch=Path.cwd()/'portable-installer-test-temp';scratch.mkdir(exist_ok=True)
def temporary():return tempfile.TemporaryDirectory(dir=scratch)

def archive(path,files,extra=None):
    with zipfile.ZipFile(path,'w') as z:
        for name,data in files.items():z.writestr(installer.ROOT+name,data)
        z.writestr(installer.ROOT+'package-manifest.json',json.dumps({'files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}))
        if extra:extra(z)
    return installer.sha(path)

class PortableUpdateTests(unittest.TestCase):
    def test_install_preserves_settings_saves_and_backup(self):
        with temporary() as d:
            root=Path(d);base=root/'installed';base.mkdir();(base/'BotWLauncher.exe').write_bytes(b'old');(base/'windows-launch.ini').write_text('custom settings');(base/'save.dat').write_bytes(b'save')
            z=root/'update.zip';digest=archive(z,{'BotWLauncher.exe':b'new','support/build.json':b'{}'})
            installer.install(z,base,digest)
            self.assertEqual((base/'BotWLauncher.exe').read_bytes(),b'new');self.assertEqual((base/'windows-launch.ini').read_text(),'custom settings');self.assertEqual((base/'save.dat').read_bytes(),b'save');self.assertEqual((root/'previous/BotWLauncher.exe').read_bytes(),b'old')
    def test_rollback_on_partial_install_failure(self):
        with temporary() as d:
            root=Path(d);base=root/'installed';base.mkdir();(base/'BotWLauncher.exe').write_bytes(b'old');z=root/'update.zip';digest=archive(z,{'BotWLauncher.exe':b'new','support/build.json':b'{}'})
            replace=installer.os.replace
            def fail(source,target):
                if str(source).endswith('verified\\support\\build.json') or str(source).endswith('verified/support/build.json'):raise OSError('simulated disk failure')
                return replace(source,target)
            with patch.object(installer.os,'replace',side_effect=fail):
                with self.assertRaises(OSError):installer.install(z,base,digest)
            self.assertEqual((base/'BotWLauncher.exe').read_bytes(),b'old')
    def test_reject_paths_before_write(self):
        for name in ['../outside','support/../outside','/absolute','support/CON.txt','support/a:stream','support/file.','windows-launch.ini','game/U-King.rpx','support/U-King.rpx','support\\escape']:
            with self.subTest(name=name),temporary() as d:
                root=Path(d);z=root/'update.zip';archive(z,{name:b'bad'})
                with self.assertRaises(ValueError):installer.unpack(z,root/'output')
    def test_reject_case_collision_and_symlink(self):
        with temporary() as d:
            root=Path(d);z=root/'update.zip';archive(z,{'support/a':b'a','support/A':b'b'})
            with self.assertRaises(ValueError):installer.unpack(z,root/'case')
            def symlink(z):
                info=zipfile.ZipInfo(installer.ROOT+'support/link');info.external_attr=(stat.S_IFLNK|0o777)<<16;z.writestr(info,'outside')
            archive(z,{'BotWLauncher.exe':b'a'},symlink)
            with self.assertRaises(ValueError):installer.unpack(z,root/'link')
    def test_reject_corrupt_hash_and_unlisted_file(self):
        with temporary() as d:
            root=Path(d);z=root/'update.zip';archive(z,{'BotWLauncher.exe':b'a'})
            with self.assertRaises(ValueError):installer.install(z,root/'installed','0'*64)
            archive(z,{'BotWLauncher.exe':b'a'},lambda z:z.writestr(installer.ROOT+'support/unlisted',b'bad'))
            with self.assertRaises(ValueError):installer.unpack(z,root/'output')

if __name__=='__main__':unittest.main()
