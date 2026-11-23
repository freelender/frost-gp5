import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from preset_bank import Capture,recall_packets
import test_preset_read as read_tests

def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]

class RamPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/ram_presets.dll' if os.name=='nt' else 'build/ram_presets.so'),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.ge_turn.argtypes=[ct.c_int];c.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_wire.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint];c.ge_reply.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        c.ge_ram_word.restype=ct.c_uint;c.ge_actual_value.restype=c.ge_ir_coefficient.restype=ct.c_float
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=450):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail(f'not ready: capture={self.lib.ge_capture_state()} error={self.lib.ge_capture_error()}')
    def until(self,state):
        for _ in range(450):
            if self.lib.ge_capture_state()==state:return
            self.lib.ge_pump(1)
        self.fail('capture state not reached')
    def wire(self,p):
        a=(ct.c_uint8*len(p))(*p);self.assertEqual(self.lib.ge_wire(int.from_bytes(p[:2],'little'),a,len(p)),1)
    def remote_gain(self,value):
        self.wire(struct.pack('<HH8I',0x7f40,36,1,9000,1,self.lib.ge_actual_revision(),0,0,0,bits(value)))
    def save(self,profile=0):self.assertEqual(self.lib.ge_ram_save(profile),1);self.settle();self.assertFalse(self.lib.ge_capture_error())
    def load(self,profile=0):self.assertEqual(self.lib.ge_preset_start(profile),1);self.settle()
    def test_save_only_captures_targets_no_dsp_mutation_then_load_discards_later_edits(self):
        c=self.lib;c.ge_paint();clean=[c.ge_pixel(y*160+x) for y in range(12) for x in range(105)]
        c.ge_queue_raw(0,0,1750);self.settle();before=c.ge_actual_revision();commands=c.ge_commands()
        self.save();self.assertEqual(c.ge_actual_revision(),before);self.assertEqual(c.ge_commands(),commands)
        self.assertEqual(c.ge_ram_valid(0),1);self.assertEqual(c.ge_ram_word(0,27),bits(1.75));self.assertEqual(c.ge_changed(),0)
        c.ge_paint();self.assertEqual([c.ge_pixel(y*160+x) for y in range(12) for x in range(105)],clean)
        c.ge_queue_raw(0,0,2250);self.settle();self.assertEqual(c.ge_actual_value(0,0),2.25)
        self.load();self.assertEqual(c.ge_actual_value(0,0),1.75);self.assertTrue(c.ge_muted());self.assertEqual(c.ge_changed(),0)
        c.ge_paint();self.assertEqual([c.ge_pixel(y*160+x) for y in range(12) for x in range(105)],clean)
        self.remote_gain(1.5);c.ge_pump(20);self.settle();self.assertEqual(c.ge_changed(),1)
        self.assertEqual(c.ge_capture_bytes(),4488);   # 32q: +retries, reason, retry_at
        self.assertEqual(c.ge_ram_bytes(),13220) # 3 x 1101 words + valid/epoch
    def test_complete_two_ir_capture_and_recall_preserves_all_words_and_signed_zero(self):
        c=self.lib;w=read_tests.PresetReadTests.body(None)
        for p in recall_packets(Capture('Synthetic',w),1,1,first_token=1000):self.wire(p)
        c.ge_audio(4);c.ge_pump(25);self.settle();self.save(2)
        self.assertEqual(tuple(c.ge_ram_word(2,i) for i in range(1071)),tuple(w))
        c.ge_queue_raw(3,0,1400);self.settle();self.load(2)
        self.assertEqual(c.ge_ir_taps(0),512);self.assertEqual(c.ge_ir_taps(3),512)
        self.assertEqual(bits(c.ge_ir_coefficient(0,0)),0x80000000)
        self.assertAlmostEqual(c.ge_actual_value(3,0),.8,places=6);self.assertEqual(c.ge_faults(),0)
    def test_each_profile_has_independent_overlay_and_rom_remains_unchanged(self):
        c=self.lib;original=c.ge_bank_word(0,27)
        for i,value in enumerate((.5,1.25,2.)):
            c.ge_queue_raw(0,0,int(value*1000));self.settle();self.save(i)
        for i,value in enumerate((.5,1.25,2.)):
            self.load(i);self.assertEqual(c.ge_actual_value(0,0),value)
        self.assertEqual(c.ge_bank_word(0,27),original)
    def test_lost_requests_replies_and_bad_tokens_retry_each_read_stage_exactly(self):
        c=self.lib
        for state in (1,2,3):
            for mode in (1,2,3):
                c.ge_init();self.settle();c.ge_ram_save(0);self.until(state)
                self.assertEqual(c.ge_prepare(),24);saved=tuple(c.ge_prepared_word(i) for i in range(6))
                c.ge_drop(1,mode);c.ge_deliver();self.assertEqual(c.ge_prepare(),24)
                self.assertEqual(tuple(c.ge_prepared_word(i) for i in range(6)),saved)
                c.ge_deliver();self.settle();self.assertFalse(c.ge_capture_error());self.assertEqual(c.ge_ram_valid(0),1)
                self.assertEqual(c.ge_actual_revision(),1)
    def test_intervening_live_change_before_during_or_final_read_preserves_old_save(self):
        c=self.lib
        for state in (1,2,3):
            c.ge_init();self.settle();c.ge_queue_raw(0,0,1250);self.settle();self.save()
            c.ge_queue_raw(0,0,1500);self.settle();c.ge_ram_save(0);self.until(state);self.remote_gain(2.)
            self.settle();self.assertTrue(c.ge_capture_error());self.assertEqual(c.ge_ram_word(0,27),bits(1.25))
            self.assertEqual(c.ge_actual_value(0,0),2.)
    def test_corrupt_chunk_or_final_crc_never_publishes_partial_capture(self):
        c=self.lib;self.save();original=tuple(c.ge_ram_word(0,i) for i in range(1071))
        c.ge_queue_raw(0,0,1250);self.settle();c.ge_ram_save(0);self.until(3);c.ge_prepare();token=c.ge_prepared_word(2)
        reply=struct.pack('<HH11I',0x7f44,48,1,token,0,c.ge_actual_revision(),0,0,123,1071,0,0,0)
        a=(ct.c_uint8*48)(*reply);c.ge_reply(a,48);self.settle()
        self.assertTrue(c.ge_capture_error());self.assertEqual(tuple(c.ge_ram_word(0,i) for i in range(1071)),original)
        self.assertEqual(c.ge_actual_value(0,0),1.25)
    def test_malformed_count_index_padding_and_header_do_not_advance_capture(self):
        c=self.lib
        for offset,value in ((0,0),(20,1),(24,4),(8,999)):
            c.ge_init();self.settle();c.ge_ram_save(0);self.until(2);c.ge_prepare();saved=tuple(c.ge_prepared_word(i) for i in range(6))
            reply=bytearray(struct.pack('<HH11I',0x7f44,48,1,saved[2],0,1,0,5,1,44100,3,0,1))
            struct.pack_into('<I',reply,offset,value);a=(ct.c_uint8*48)(*reply);self.assertEqual(c.ge_reply(a,48),0)
            c.ge_prepare();self.assertEqual(tuple(c.ge_prepared_word(i) for i in range(6)),saved);c.ge_deliver();self.settle()
            self.assertFalse(c.ge_capture_error());self.assertEqual(c.ge_ram_valid(0),1)
    def test_reconnect_aborts_partial_capture_preserves_saves_and_controller_init_discards(self):
        c=self.lib;c.ge_queue_raw(0,0,1250);self.settle();self.save()
        c.ge_queue_raw(0,0,1500);self.settle();c.ge_ram_save(0);self.until(2);c.ge_pump(10)
        c.ge_reconnect();self.settle();self.assertEqual(c.ge_ram_word(0,27),bits(1.25))
        c.ge_dsp_reset();self.settle();self.assertEqual(c.ge_ram_valid(0),1);self.load();self.assertEqual(c.ge_actual_value(0,0),1.25)
        c.ge_init();self.settle();self.assertEqual(c.ge_ram_valid(0),0);self.load();self.assertEqual(c.ge_actual_value(0,0),.75)
    def test_pending_writes_load_and_counter_exhaustion_reject_save_start(self):
        c=self.lib;c.ge_queue_raw(0,0,1250);self.assertEqual(c.ge_ram_save(0),0);self.settle()
        c.ge_preset_start(1);self.assertEqual(c.ge_ram_save(0),0);self.settle()
        self.assertEqual(c.ge_ram_save(3),0);c.ge_set_token(0xfffffffe)
        self.assertEqual(c.ge_ram_save(0),0);self.assertEqual(c.ge_ram_valid(0),0)

if __name__=='__main__':unittest.main()
