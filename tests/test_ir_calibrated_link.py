"""Calibrated (>1 L1) cabinet coefficients through the real linked IR kernels."""
import ctypes as ct
import os
import unittest
import test_ir_link as link

class CalibratedIrLinkTests(unittest.TestCase):
    def test_calibrated_gain_survives_controller_fixed_point_path(self):
        c=ct.CDLL(str(link.build(link.ROOT/('build/ir_calibrated_link.dll' if os.name=='nt'
                                          else 'build/ir_calibrated_link.so'),
                                 link.ROOT/'tests/ir_link_host.c',
                                 includes=[link.ROOT/'tests/stubs'],
                                 flags=['-DFROST_IR_FFT_PARTS=8u','-DFROST_IR_FFT_SPLIT_SPECTRUM=1'])))
        c.ln_level.restype=c.ln_scale.restype=ct.c_float
        h=link.IrLinkTests();h.c=c
        taps=[v*4 for v in link.cab()];h.setup(taps)
        x=[v*.15 for v in link.tone(64*900)];y=h.run_blocks(x)
        self.assertEqual(c.ln_remote(),1)
        ref=link.conv(x[-64*40-128:],taps);level=c.ln_level(0)
        want=[v*level for v in ref[-64*30-128:-128]];tail=y[-64*30:]
        self.assertLess(max(abs(a-b) for a,b in zip(tail,want))/max(map(abs,want)),2e-4)
