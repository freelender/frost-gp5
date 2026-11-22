import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class HumanControlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/human_controls'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.c
        c.ge_to_dsp.argtypes=[ct.c_uint,ct.c_uint,ct.c_int];c.ge_to_dsp.restype=ct.c_float
        c.ge_from_dsp.argtypes=[ct.c_uint,ct.c_uint,ct.c_float]
        c.ge_dsp_meta.restype=ct.c_float;c.ge_actual_value.restype=ct.c_float
        c.ge_queue.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        for name in ('ge_human_name','ge_human_hint','ge_human_text'):getattr(c,name).restype=ct.c_char_p
    def setUp(self):self.c.ge_init();self.settle()
    def settle(self):
        for _ in range(150):
            self.c.ge_pump(1)
            if self.c.ge_ready():return
        self.fail('editor did not settle')
    def test_all_effect_controls_roundtrip_and_preserve_dsp_bounds(self):
        c=self.c
        for type,count in enumerate((1,1,3,1,3,3,3,3,3)):
            for p in range(count):
                lo,hi,step,initial,scale=[c.ge_human_meta(type,p,i) for i in range(5)]
                self.assertGreater(step,0);self.assertGreater(scale,0)
                a,b=c.ge_dsp_meta(type,p,0),c.ge_dsp_meta(type,p,1)
                previous=None
                for value in range(lo,hi+1):
                    raw=c.ge_to_dsp(type,p,value)
                    self.assertTrue(a<=raw<=b,(type,p,value,raw))
                    self.assertEqual(c.ge_from_dsp(type,p,raw),value,(type,p,value))
                    if previous is not None:
                        self.assertTrue(raw<previous if (type,p)==(8,1) else raw>previous)
                    previous=raw
                self.assertEqual(c.ge_from_dsp(type,p,c.ge_dsp_meta(type,p,2)),initial)
    def test_volumes_gain_and_mix_have_familiar_controls(self):
        c=self.c
        for type,p in ((3,0),(4,2)):
            self.assertEqual(c.ge_human_name(type,p),b'VOL')
            self.assertEqual([c.ge_human_meta(type,p,i) for i in (0,1)], [0,100])
            self.assertEqual(c.ge_to_dsp(type,p,0),0.)
            self.assertEqual(c.ge_to_dsp(type,p,50),1.)
            self.assertEqual(c.ge_to_dsp(type,p,100),2.)
        self.assertEqual(c.ge_human_name(4,0),b'GAIN')
        self.assertEqual((c.ge_to_dsp(4,0,0),c.ge_to_dsp(4,0,100)),(1.,20.))
        for type in (5,6,8):
            self.assertEqual(c.ge_human_name(type,2),b'MIX')
            self.assertEqual((c.ge_to_dsp(type,2,0),c.ge_to_dsp(type,2,100)),(0.,1.))
        self.assertLess(c.ge_to_dsp(5,1,100),1.) # maximum repeat remains stable
    def test_gate_is_logarithmic_and_reverb_tone_turns_brighter(self):
        c=self.c
        for knob,threshold in ((0,.00001),(25,.0001),(50,.001),(75,.01),(100,.1)):
            self.assertAlmostEqual(c.ge_to_dsp(1,0,knob),threshold,places=8)
        self.assertEqual(c.ge_human_name(8,1),b'TONE')
        self.assertEqual((c.ge_to_dsp(8,1,0),c.ge_to_dsp(8,1,100)),(1.,0.))
        self.assertEqual(c.ge_to_dsp(7,2,0),0.)
        self.assertLess(c.ge_to_dsp(7,2,-100),0.)
    def test_real_queue_updates_wire_floats_and_rejects_bad_knob_values(self):
        c=self.c
        self.assertEqual(c.ge_queue(3,0,50),1);self.settle()
        self.assertEqual(c.ge_actual_value(3,0),1.)
        self.assertEqual(c.ge_queue(3,0,100),1);self.settle()
        self.assertEqual(c.ge_actual_value(3,0),2.)
        before=c.ge_actual_revision()
        for bad in (-1,101):self.assertEqual(c.ge_queue(3,0,bad),0)
        self.assertEqual(c.ge_actual_revision(),before)
        self.assertEqual(c.ge_queue(1,0,75),1);self.settle()
        self.assertAlmostEqual(c.ge_actual_value(1,0),.01,places=8)
    def test_display_and_bypass_do_not_quantize_saved_values(self):
        c=self.c;self.assertEqual(c.ge_queue_raw(3,0,1235),1);self.settle()
        before=c.ge_raw(3,0)
        for _ in range(5):
            self.assertEqual(c.ge_value(3,0),62)
            self.assertEqual(c.ge_human_text(3,0),b'62')
            self.assertEqual(c.ge_paint(),0)
        # Select IR with the real gesture, then selected-only bypass.
        for _ in range(2):c.ge_turn(1)
        c.ge_foot();self.settle()
        self.assertEqual(c.ge_raw(3,0),before)
    def test_rate_and_time_keep_physical_units_and_all_types_are_available(self):
        c=self.c
        for type in range(9):
            c.ge_init();self.settle()
            self.assertEqual(c.ge_type_change(4,type),1);self.settle()
            self.assertEqual(c.ge_type(4),type)
            if type in (6,7):
                self.assertEqual(c.ge_human_text(4,0),b'0.80HZ' if type==6 else b'0.30HZ')
            if type==5:self.assertEqual(c.ge_human_text(4,0),b'250MS')
        self.assertEqual(c.ge_type_change(4,52),0) # first unregistered type after the Stage 5 additions
