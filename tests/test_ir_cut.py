import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

IR,EMPTY=3,255
F=ct.c_float

class IRCutTests(unittest.TestCase):
    """32c cabinet LOW CUT / HIGH CUT on the IR block's FFT path (the path
    the pedal runs): off by default and for stored 0, 12 dB/oct when on."""
    @classmethod
    def setUpClass(cls):
        suffix='.dll' if os.name=='nt' else '.so'
        cls.c=c=ct.CDLL(str(build(ROOT/('build/ir_cut_fft_host'+suffix),ROOT/'tests/ir_fft_host.c',includes=[ROOT/'tests/stubs'])))
        c.c_set.argtypes=[ct.c_uint,ct.c_uint,F]
        c.c_ir_values.argtypes=[ct.c_uint,ct.POINTER(F),ct.c_uint]
        c.c_block.argtypes=[ct.POINTER(F),ct.POINTER(F),ct.POINTER(F),ct.c_uint,ct.c_uint]
        cls.ui=ui=ct.CDLL(str(build(ROOT/('build/ir_cut_ui'+suffix),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        ui.ge_to_dsp.restype=F;ui.ge_from_dsp.argtypes=[ct.c_uint,ct.c_uint,F]
    def chain(self,low,high):
        c=self.c;c.c_init(1)
        self.assertNotEqual(c.c_route((ct.c_uint*6)(IR,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY),(ct.c_uint*6)(*range(6)),6),99)
        while c.c_transition():self.run_silence(1)
        h=[1.0]+[0.0]*127;self.assertEqual(c.c_ir_values(0,(F*128)(*h),128),1);self.assertEqual(c.c_enable(0,1),1)
        self.assertEqual(c.c_set(0,0,1.0),1)
        if low is not None:self.assertEqual(c.c_set(0,1,low),1)
        if high is not None:self.assertEqual(c.c_set(0,2,high),1)
        self.run_silence(40)                      # bypass fade-in and parameter smoothing settle
    def run_silence(self,blocks):
        n=64
        for _ in range(blocks):a=(F*n)();ya=(F*n)();yb=(F*n)();self.c.c_block(a,ya,yb,n,0)
    def gain_db(self,hz,low,high,blocks=200):
        self.chain(low,high);c=self.c;n=64;out=[];inp=[]
        for b in range(blocks):
            x=[0.25*math.sin(2*math.pi*hz*(b*n+i)/44100) for i in range(n)]
            a=(F*n)(*x);ya=(F*n)();yb=(F*n)();c.c_block(a,ya,yb,n,0)
            if b>=blocks//2:out+=list(ya);inp+=x
        self.assertTrue(c.c_fft_slot(0));self.assertEqual(c.c_faults(),0)
        return 20*math.log10(math.sqrt(sum(v*v for v in out)/sum(v*v for v in inp)))
    def test_defaults_and_stored_zero_are_off(self):
        for low,high in ((None,None),(0.0,0.0),(19.0,999.0),(0.0,20000.0)):
            for hz in (50,1000,15000):
                with self.subTest(low=low,high=high,hz=hz):self.assertAlmostEqual(self.gain_db(hz,low,high,60),0.0,delta=0.02)
    def test_low_cut_and_high_cut_slopes(self):
        self.assertLess(abs(self.gain_db(200,200.0,None)+3.0),0.5)       # -3 dB at the cutoff
        self.assertLess(self.gain_db(50,200.0,None),-22.0)                # two octaves below: ~-24 dB
        self.assertGreater(self.gain_db(2000,200.0,None),-0.1)
        self.assertLess(abs(self.gain_db(4000,None,4000.0)+3.0),0.6)
        self.assertLess(self.gain_db(16000,None,4000.0),-20.0)
        self.assertGreater(self.gain_db(500,None,4000.0),-0.1)
        both=self.gain_db(1000,100.0,8000.0);self.assertGreater(both,-0.2);self.assertLess(both,0.1)
    def test_moving_cutoffs_stay_finite(self):
        c=self.c;self.chain(None,None);n=64
        for b in range(300):
            self.assertEqual(c.c_set(0,1,float((b*37)%401)),1);self.assertEqual(c.c_set(0,2,1000.0+(b*911)%19001),1)
            x=[0.8*math.sin(0.05*(b*n+i))+0.2*math.sin(1.9*(b*n+i)) for i in range(n)]
            a=(F*n)(*x);ya=(F*n)();yb=(F*n)();c.c_block(a,ya,yb,n,0)
            self.assertTrue(all(math.isfinite(v) and abs(v)<4.0 for v in ya))
        self.assertEqual(c.c_faults(),0)
    def test_pedal_controls(self):
        ui=self.ui
        self.assertEqual(ui.ge_human_count(IR),3)
        self.assertEqual(ui.ge_from_dsp(IR,2,0.0),20000)                  # older presets: off
        self.assertEqual(ui.ge_from_dsp(IR,2,5000.0),5000)
        self.assertEqual(ui.ge_from_dsp(IR,1,0.0),0)
        self.assertEqual(ui.ge_from_dsp(IR,1,120.0),120)
        self.assertEqual(ui.ge_to_dsp(IR,1,250),250.0)
        self.assertEqual(ui.ge_to_dsp(IR,2,1000),1000.0)
        self.assertEqual(ui.ge_to_dsp(IR,2,20000),20000.0)
        self.assertAlmostEqual(ui.ge_to_dsp(IR,0,80),1.6,places=6)

if __name__=='__main__':unittest.main()
