"""Factory upgrade must not destroy edited, deleted or custom presets."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from factory_presets import build_bank
from install_factory_presets import may_replace

class FactoryBankTests(unittest.TestCase):
    def test_upgrade_accepts_only_exact_original_bytes_and_is_idempotent(self):
        original=bytes(range(256))+bytes(8);new=bytes([1])*264
        digest=hashlib.sha256(original).hexdigest()
        self.assertTrue(may_replace(original,new,digest))
        self.assertFalse(may_replace(new,new,digest))
        for value in (bytes(264),bytes([255])*264,original[:-1]+b'\x01',b'USER'+original[4:]):
            self.assertFalse(may_replace(value,new,digest))
        self.assertTrue(may_replace(bytes(264),new))
        self.assertTrue(may_replace(bytes([255])*264,new))
        self.assertFalse(may_replace(original,new))
    def test_bank_has_twenty_editable_builtin_records_with_cab80_and_low_reverb(self):
        catalog=json.loads((ROOT/'web/effects.json').read_text(encoding='utf-8'))['effects']
        with tempfile.TemporaryDirectory() as d:
            rows=build_bank(d)
            self.assertEqual(len(rows),20)
            for row in rows:
                record=(Path(d)/('%02d.fpst'%row['number'])).read_bytes()
                self.assertEqual(struct.unpack_from('<I',record,260)[0],zlib.crc32(record[:260]))
                self.assertEqual(record[248:252],bytes([255])*4)
                for block in row['chain']:
                    if block['type']==3:self.assertEqual(block['params']['VOL'],80)
                    if block['type'] in (19,20):self.assertLessEqual(block['params']['E.LEVEL' if block['type']==19 else 'MIX'],25)
            self.assertEqual(catalog['3']['params'][0]['initial'],80)
            for type_id in ('45','47'):
                defaults={p['name']:p['initial'] for p in catalog[type_id]['params']}
                self.assertEqual((defaults['MASTER'],defaults['LEVEL']),(60,57))
