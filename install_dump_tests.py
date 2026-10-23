import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import install_dump as importer

class InstallDumpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def package(self, kind='game', version=None):
        folder = self.root/kind; folder.mkdir(exist_ok=True)
        tmd = bytearray(0xB04+48)
        tmd[:4] = b'\x00\x01\x00\x04'
        title = importer.TITLES[kind].to_bytes(8, 'big')
        tmd[0x18C:0x194] = title
        struct.pack_into('>HH', tmd, 0x1DC, version if version is not None else (208 if kind=='update' else 0), 1)
        struct.pack_into('>IHHQ', tmd, 0xB04, 3, 0, 1, 16)
        ticket = bytearray(0x2A4); ticket[:4] = tmd[:4]; ticket[0x1DC:0x1E4] = title
        (folder/'title.tmd').write_bytes(tmd); (folder/'title.tik').write_bytes(ticket)
        (folder/'00000003.app').write_bytes(bytes(16))
        return folder

    def test_valid_game_and_update_and_fingerprint(self):
        for kind in importer.TITLES:
            folder = self.package(kind)
            info = importer.inspect_package(folder, kind)
            self.assertEqual(info['title'], f'{importer.TITLES[kind]:016X}')
            self.assertEqual(info['bytes'],16)
            (folder/'00000003.app').write_bytes(bytes(32))
            self.assertNotEqual(info['fingerprint'], importer.inspect_package(folder,kind)['fingerprint'])

    def test_reject_swapped_title_old_update_and_ticket_mismatch(self):
        game = self.package()
        with self.assertRaises(ValueError): importer.inspect_package(game,'update')
        with self.assertRaises(ValueError): importer.inspect_package(self.package('update',192),'update')
        ticket = bytearray((game/'title.tik').read_bytes()); ticket[0x1DC] ^= 1
        (game/'title.tik').write_bytes(ticket)
        with self.assertRaises(ValueError): importer.inspect_package(game,'game')

    def test_reject_incomplete_contents_and_metadata(self):
        game = self.package(); (game/'00000003.app').write_bytes(b'')
        with self.assertRaises(ValueError): importer.inspect_package(game,'game')
        (game/'title.tmd').write_bytes(b'bad')
        with self.assertRaises(ValueError): importer.inspect_package(game,'game')

    def test_extracted_and_merged_dump_preserved(self):
        game = self.root/'extracted'; (game/'code').mkdir(parents=True); (game/'content').mkdir()
        (game/'code/U-King.rpx').write_bytes(b'test')
        result = importer.resolve_dumps({'game':str(game),'update':''},self.root/'user',self.root,lambda s:None)
        self.assertEqual(result['game'],str(game.resolve())); self.assertEqual(result['update'],'')

    def test_package_base_requires_update_before_extraction(self):
        game = self.package()
        with patch.object(importer.subprocess,'run') as run:
            with self.assertRaisesRegex(ValueError,'separate USA v208'):
                importer.resolve_dumps({'game':str(game)},self.root/'user',self.root,lambda s:None)
            run.assert_not_called()

    def test_cache_reuse_and_cancelled_import_retry(self):
        game = self.package(); info = importer.inspect_package(game,'game')
        tool = self.root/'cdecrypt.exe'; tool.write_bytes(b'test')
        user = self.root/'user'; calls=[]
        def extract(args,check):
            calls.append(args)
            target=Path(args[2]); (target/'code').mkdir(); (target/'content').mkdir()
            (target/'code/U-King.rpx').write_bytes(b'test')
            if len(calls)==1: raise RuntimeError('cancelled')
        with patch.object(importer.subprocess,'run',side_effect=extract):
            with self.assertRaisesRegex(RuntimeError,'cancelled'): importer.import_dump(game,'game',info,user,tool,lambda s:None)
            result=importer.import_dump(game,'game',info,user,tool,lambda s:None)
            self.assertEqual(importer.import_dump(game,'game',info,user,tool,lambda s:None),result)
        self.assertEqual(len(calls),2)
        self.assertNotEqual(Path(calls[0][2]),game)
        self.assertFalse((game/'code').exists())
        self.assertEqual(json.loads((result/'import-complete.json').read_text()),info)

    def test_failed_extractor_cannot_mark_cache_complete(self):
        game=self.package(); info=importer.inspect_package(game,'game'); tool=self.root/'tool.exe'; tool.touch()
        with patch.object(importer.subprocess,'run'):
            with self.assertRaisesRegex(RuntimeError,'did not produce'): importer.import_dump(game,'game',info,self.root/'user',tool,lambda s:None)
        self.assertFalse(list((self.root/'user').rglob('import-complete.json')))

if __name__=='__main__': unittest.main()
