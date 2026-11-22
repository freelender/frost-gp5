"""AMP model files end to end on the host (EXPERIMENTAL): the USB file
commands of the generic controller (fcm_files.h) against a simulated run-A
flash, written by tools/fcm_files.py exactly as on the pedal, then the
controller's reconcile client (fcm_client.h) copying the selected MODEL's
record to the DSP (fcm_transfer.c) and the AMP block playing it. No pedal,
timing or audio-quality claim."""
import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/"tests"))
from host import build
import fcm_bank as B
import fcm_files as F
from test_fcm import model
AMP=33
class FcmFilesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/fcm_files_editor.dll' if os.name=='nt' else 'build/fcm_files_editor.so'),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.ge_fcm_ws_gain.restype=ct.c_float;
        for f in ('ge_dsp_rx','ge_fcm_client','ge_fcm_ws'):getattr(cls.lib,f).restype=ct.c_uint
        cls.lib.ge_fcm_label.restype=ct.c_char_p;cls.lib.ge_fcm_label.argtypes=[ct.c_int]
        cls.lib.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
    def setUp(self):
        self.lib.ge_init();self.pedal=F.Pedal(self.ask)
    def ask(self,payload):
        q=(ct.c_uint8*len(payload))(*payload);out=(ct.c_uint8*100)()
        n=self.lib.ge_host(q,len(payload),out);return bytes(out[:n]) if n else None
    def settle(self,n=200):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready() and self.lib.ge_fcm_client(0)==0:return
        self.fail('controller did not settle')
    def amp_route(self,model_value=0):
        """Slot 3 becomes the AMP block, with MODEL = model_value, through the real editor."""
        c=self.lib;self.settle()
        self.assertEqual(c.ge_type_change(3,AMP),1);self.settle()
        self.assertEqual(c.ge_type(3),AMP)
        if model_value:self.assertEqual(c.ge_queue(3,0,model_value),1)
        self.settle();self.lib.ge_audio(2)
    def sync(self):
        for _ in range(4):
            self.lib.ge_advance(250);self.settle();self.lib.ge_audio(2)
    def test_who_and_bounds(self):
        self.assertEqual(self.pedal.who(),((0,2,1),'frost'))
        self.assertIsNone(self.ask([0x7e,0x99]))                                  # not ours: platform handles it
        self.assertEqual(self.ask([0x7e,0x05,*struct.pack('<I',0x13e000),4])[2],0) # before run A
        self.assertEqual(self.ask([0x7e,0x05,*struct.pack('<I',0x15fffc),8])[2],0) # crosses the end
        self.assertEqual(self.ask([0x7e,0x07,*struct.pack('<I',0x160000)])[2],0)   # the settings store: refused
        self.assertEqual(self.ask([0x7e,0x07,*struct.pack('<I',0x13f800)])[2],0)   # not sector aligned
        self.assertEqual(self.ask([0x7e,0x08,*struct.pack('<I',0x13f002),1,2,3,4])[2],0)
        self.assertEqual(self.ask([0x7e,0x08,*struct.pack('<I',0x13f000),1,2,3,4,5])[2],0)       # not a multiple of 4
        self.assertEqual(self.lib.ge_rx_resets(),0)
        self.assertEqual(self.ask([0x7e,0x07,*struct.pack('<I',0x13f000)])[2],1)
        self.assertEqual(self.lib.ge_rx_resets(),1)                               # DSP-link receiver cleared after a write
    def test_add_list_replace_remove(self):
        a=B.pack_record('JCM800',model(drive=12.0));b=B.pack_record('PLEXI',model(drive=4.0))
        self.assertEqual(self.pedal.add(7,a,say=lambda *_:None),0)
        self.assertEqual(self.pedal.add(9,b,say=lambda *_:None),1)
        m=self.pedal.models();self.assertEqual(sorted(m),['amp-07','amp-09'])
        self.assertEqual(self.pedal.read(F.BASE+F.HEADER,400),a)
        self.assertEqual(self.pedal.add(7,b,say=lambda *_:None),2)              # new copy first, then the old erased
        m=self.pedal.models();self.assertEqual(m['amp-07']['sector'],2);self.assertEqual(len(self.pedal.files()),2)
        self.assertEqual(self.pedal.remove(7),1);self.assertEqual(sorted(self.pedal.models()),['amp-09'])
        self.assertEqual(self.pedal.remove(7),0)
    def test_selected_model_is_sent_played_and_cleared(self):
        c=self.lib;rec=B.pack_record('JCM800',model(gain=2.5,drive=12.0));crc=struct.unpack_from('<I',rec,396)[0]
        self.amp_route(5);self.sync()
        self.assertEqual(c.ge_fcm_client(1),1)                                   # STATUS learned: nothing held
        self.assertEqual(c.ge_dsp_rx(0),0);self.assertEqual(c.ge_fcm_ws(3,2),0)   # default model plays
        self.assertEqual(self.lib.ge_fcm_label(5),b'05 DEFAULT')
        self.pedal.add(5,rec,say=lambda *_:None);self.sync()
        self.assertEqual([c.ge_dsp_rx(i) for i in range(3)],[2,5,crc])
        self.assertEqual(bytes(c.ge_dsp_rx_byte(i) for i in range(400)),rec)
        self.assertEqual(c.ge_fcm_ws(3,2),1);self.assertEqual(c.ge_fcm_ws(3,0),6)   # loaded index+1, from the record
        self.assertAlmostEqual(c.ge_fcm_ws_gain(3),2.5,places=6)
        self.assertEqual(self.lib.ge_fcm_label(5),b'05 JCM800')
        calls=c.ge_calls();self.sync();self.assertLess(c.ge_calls()-calls,20)      # in sync: no transfer repeats
        self.assertEqual(c.ge_dsp_rx(4),1)
        c.ge_queue(3,0,6);self.sync();self.assertEqual(c.ge_fcm_ws(3,2),0)          # MODEL without a file: default
        c.ge_queue(3,0,5);self.sync();self.assertEqual(c.ge_fcm_ws(3,2),1);self.assertEqual(c.ge_dsp_rx(4),1)  # held, no resend
        self.pedal.remove(5);self.sync()
        self.assertEqual(c.ge_dsp_rx(0),0);self.assertEqual(c.ge_fcm_ws(3,2),0)    # stale record cleared
    def test_replaced_file_is_resent_and_dsp_restart_recovers(self):
        c=self.lib;a=B.pack_record('A',model(gain=1.5));b=B.pack_record('B',model(gain=3.0))
        self.pedal.add(0,a,say=lambda *_:None);self.amp_route(0);self.sync()
        self.assertAlmostEqual(c.ge_fcm_ws_gain(3),1.5,places=6)
        self.pedal.add(0,b,say=lambda *_:None);self.sync()
        self.assertAlmostEqual(c.ge_fcm_ws_gain(3),3.0,places=6);self.assertEqual(c.ge_dsp_rx(2),struct.unpack_from('<I',b,396)[0])
        c.ge_dsp_reset();self.settle();self.assertEqual(c.ge_dsp_rx(0),0)        # DSP restarted: route + record gone
        self.assertEqual(c.ge_type_change(3,AMP),1);self.settle();self.sync()
        self.assertEqual(c.ge_dsp_rx(0),2);self.assertAlmostEqual(c.ge_fcm_ws_gain(3),3.0,places=6)
    def test_lost_replies_are_resent_and_a_corrupt_file_is_ignored(self):
        c=self.lib;rec=bytearray(B.pack_record('A',model(gain=2.0)))
        self.pedal.add(1,bytes(rec),say=lambda *_:None)
        rec[40]^=1;s=self.pedal.add(2,bytes(rec)[:396]+bytes(rec[396:]),say=lambda *_:None)  # record CRC now wrong
        self.amp_route(1);self.sync();self.assertEqual(c.ge_dsp_rx(0),2)
        c.ge_queue(3,0,2);self.sync()
        self.assertEqual(c.ge_fcm_ws(3,2),0);self.assertEqual(c.ge_dsp_rx(1),1)  # bad record never sent; default plays
        self.assertEqual(self.lib.ge_fcm_label(2),b'02 DEFAULT');self.assertGreaterEqual(s,0)
    def test_no_amp_block_means_no_traffic(self):
        c=self.lib;self.pedal.add(0,B.pack_record('A',model()),say=lambda *_:None);self.settle()
        calls=c.ge_calls()
        for _ in range(5):c.ge_advance(250);c.ge_pump(1)
        self.assertEqual(c.ge_dsp_rx(0),0);self.assertEqual(c.ge_fcm_client(1),0)
        self.assertLessEqual(c.ge_calls()-calls,10)
if __name__=='__main__':unittest.main()
