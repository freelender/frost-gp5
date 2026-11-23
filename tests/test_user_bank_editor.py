import ctypes as ct
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from preset_bank import Capture,header
from prepare_gp5 import stage
import test_preset_read as read_tests

class UserBankEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.captures=[]
        for i in range(16):
            w=read_tests.PresetReadTests.body(None)
            w[27]=struct.unpack('<I',struct.pack('<f',.5+i*.025))[0]
            if i==15:w[9:15]=list(reversed(range(6)));w[2]=1
            name='USER '+str(i)
            if i==3:name='Long preset name needs safe display truncation'
            if i==4:name='Čist 😀 / "quote"'
            cls.captures.append(Capture(name,w))
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            folder=Path(folder);(folder/'user_bank.h').write_bytes(header(cls.captures))
            cls.lib=ct.CDLL(str(build(ROOT/('build/user_bank_editor.dll' if os.name=='nt' else 'build/user_bank_editor.so'),
                ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs',folder],flags=['-DFROST_USER_BANK=1'])))
        c=cls.lib;c.ge_turn.argtypes=[ct.c_int];c.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_bank_word.restype=ct.c_uint;c.ge_bank_name.restype=c.ge_bank_label.restype=ct.c_char_p
        c.ge_actual_value.restype=c.ge_ir_coefficient.restype=ct.c_float
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=400):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail('bank editor did not become coherent')
    def until(self,state):
        for _ in range(400):
            if self.lib.ge_preset_state()==state:return
            self.lib.ge_pump(1)
        self.fail('bank upload state did not arrive')
    def open(self):
        c=self.lib;c.ge_press();c.ge_turn(-1);c.ge_press();self.assertEqual(c.ge_view(),3)
    def test_maximum_bank_rom_exact_words_no_boot_overwrite_and_bounded_names(self):
        c=self.lib;self.assertEqual(c.ge_bank_count(),16);self.assertEqual(c.ge_actual_revision(),1)
        self.assertEqual([c.ge_actual_type(i) for i in range(6)],[0,1,2,3,255,255])
        for i,p in enumerate(self.captures):
            self.assertEqual(c.ge_bank_name(i),p.name.encode('utf-8'))
            self.assertEqual(tuple(c.ge_bank_word(i,j) for j in range(1071)),p.words)
            self.assertLessEqual(len(c.ge_bank_label(i)),23)
        self.assertTrue(c.ge_bank_label(3).endswith(b'>'))
        self.assertEqual(c.ge_bank_label(4),b'*IST * / *QUOTE*')
        self.assertEqual(c.ge_client_bytes(),796) # eight-parameter mirrors, dirty masks, edit spacing, atomic enable-mask pending flag and 32i refusal counter
    def test_user_bank_preserves_immutable_retries_for_every_stage(self):
        c=self.lib
        for state in (3,4,5):
            for mode in (1,2,3):
                c.ge_init();self.settle();self.assertEqual(c.ge_preset_start(15),1);self.until(state)
                c.ge_prepare();saved=tuple(c.ge_prepared_word(i) for i in range(12))
                c.ge_drop(1,mode);c.ge_deliver();c.ge_prepare()
                self.assertEqual(tuple(c.ge_prepared_word(i) for i in range(12)),saved)
                c.ge_deliver();self.settle();self.assertEqual(c.ge_actual_revision(),2)
                self.assertFalse(c.ge_preset_error());self.assertEqual(c.ge_commands(),1)
                self.assertEqual(c.ge_ir_taps(3),512)
    def test_pending_intent_bounds_and_reconnect_discard_uncertain_upload(self):
        c=self.lib;self.assertEqual(c.ge_preset_start(16),0);self.assertEqual(c.ge_queue_raw(0,0,1500),1)
        self.assertEqual(c.ge_preset_start(15),0);self.settle()
        self.assertEqual(c.ge_preset_start(15),1);self.until(4);c.ge_pump(10)
        c.ge_reconnect();self.settle();self.assertEqual(c.ge_actual_type(0),0)
        c.ge_preset_start(15);self.settle();self.assertFalse(c.ge_preset_error())
        self.assertEqual(c.ge_actual_type(0),3);self.assertTrue(c.ge_muted())
    def test_staging_rejects_corrupt_or_diagnostic_bank_before_copying(self):
        commit=json.loads((ROOT/'dependencies.lock.json').read_text())['OpenGP5']['commit']
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            folder=Path(folder);bad=folder/'bad.fbank';bad.write_bytes(b'not a valid bank')
            for controller in ('generic','diagnostic'):
                destination=folder/('stage-'+controller)
                with patch('prepare_gp5.subprocess.check_output',side_effect=[commit,'']):
                    with self.assertRaises(ValueError):stage(folder/'platform',destination,controller,bad)
                self.assertFalse(destination.exists())
    def test_sixteenth_ram_save_is_independent_and_bounded(self):
        c=self.lib;c.ge_preset_start(15);self.settle()
        c.ge_queue_raw(0,0,1250);self.settle();self.assertEqual(c.ge_ram_save(15),1);self.settle()
        self.assertEqual(c.ge_ram_valid(15),1);self.assertEqual(c.ge_ram_valid(0),0)
        self.assertEqual(c.ge_ram_bytes(),70472) # 16 x 1101 words + valid/epoch;self.assertEqual(c.ge_capture_bytes(),4472)
        c.ge_queue_raw(0,0,1500);self.settle();c.ge_preset_start(15);self.settle()
        self.assertEqual(c.ge_actual_value(0,0),1.25);self.assertTrue(c.ge_muted())

if __name__=='__main__':unittest.main()
