import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from platform_compat import patch_dsp_link

class PacedUartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/paced_uart'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/paced_uart_host.c')))
        cls.lib.paced_send.argtypes=[ct.c_uint,ct.c_uint32,ct.c_uint];cls.lib.paced_value.restype=ct.c_uint32
    def setUp(self):self.lib.paced_phase(0)
    def test_short_and_long_frames_keep_exact_byte_order_and_burst_limits(self):
        c=self.lib
        for n in (6,8,12,16,17,40,52,404):
            self.assertEqual(c.paced_send(n,0,0),n)
            self.assertEqual(c.paced_value(0,0),1 if n<=16 else (n+7)//8)
            self.assertEqual(bytes(c.paced_value(4,i) for i in range(n)),bytes((i*37+11)&255 for i in range(n)))
            if n>16:self.assertTrue(all(c.paced_value(3,i)<=8 for i in range(c.paced_value(0,0))))
    def test_gaps_and_wraparound_are_bounded_by_controller_ticks(self):
        c=self.lib
        for start in (0,0xfffffffe):
            c.paced_send(52,start,0);times=[c.paced_value(5,i) for i in range(c.paced_value(0,0))]
            self.assertTrue(all(((b-a)&0xffffffff)>=2 for a,b in zip(times,times[1:])))
            self.assertLess((c.paced_value(6,0)-start)&0xffffffff,40)
    def test_clock_quantization_preserves_two_ms_minimum_idle(self):
        c=self.lib
        for phase in range(0,1000,73):
            c.paced_phase(phase);c.paced_send(52,0,0)
            for i in range(1,c.paced_value(0,0)):
                self.assertGreaterEqual(c.paced_value(7,i)-c.paced_value(8,i-1),2000)
    def test_short_write_stops_and_clears_outstanding_wait(self):
        c=self.lib;self.assertEqual(c.paced_send(52,0,3),0)
        self.assertEqual((c.paced_value(0,0),c.paced_value(1,0),c.paced_value(2,0)),(3,16,0))
    def test_guarded_source_hook_rejects_unexpected_driver(self):
        old='    gp5_uart0_send(tx, len, 100);           /* 404 bytes take 9 ms at 460800 baud */'
        self.assertEqual(patch_dsp_link(old),'#include "paced_uart.inc"')
        for source in ('',old+old,old.replace('100','99',1)):
            with self.assertRaises(ValueError):patch_dsp_link(source)
    def test_fifo_timing_model_exhibits_burst_loss_and_pacing_reduces_it(self):
        # Hypothetical16-byte FIFO with drain every3ms; not a hardware measurement.
        byte_us=1e6*10/460800
        def dropped(paced,phase):
            queue=0;lost=0;next_drain=phase
            for i in range(40):
                arrival=i*byte_us+(i//8)*2000 if paced else i*byte_us
                while next_drain<=arrival:queue=0;next_drain+=3000
                if queue==16:lost+=1
                else:queue+=1
            return lost
        self.assertGreater(max(dropped(False,p) for p in range(0,3000,50)),0)
        self.assertEqual(max(dropped(True,p) for p in range(0,3000,50)),0)

if __name__=='__main__':unittest.main()
