"""Controller edit-write rate limit (line-test builds: FROST_EDITOR_WRITE_MS=120).

Host model only. Rapid knob edits coalesce to the newest value, are spaced by
at least 120 ms, the final value always arrives, and enable/bypass writes are
never delayed. Default (non-line-test) builds keep 0 ms (unchanged)."""
import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
SLOT=0x7f40;PARAMETER,ENABLE=1,2

class EditThrottleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/edit_throttle'+('.dll' if os.name=='nt' else '.so')),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_EDITOR_WRITE_MS=120u'])))
        cls.c.ge_queue.argtypes=[ct.c_uint,ct.c_uint,ct.c_int];cls.c.ge_actual_value.restype=ct.c_float
    def setUp(self):
        self.c.ge_init()
        for _ in range(300):
            self.c.ge_pump(1)
            if self.c.ge_ready():break
        self.assertTrue(self.c.ge_ready())
    def step(self,ms):
        """Advance ms, then let the editor send at most one frame and deliver it."""
        c=self.c;c.ge_advance(ms);n=c.ge_prepare()
        if not n:return None
        frame=(c.ge_prepared_word(0)&0xffff,c.ge_prepared_word(3),c.ge_prepared_word(7),c.ge_prepared_word(8))
        c.ge_deliver();return frame
    def test_rapid_edits_coalesce_spaced_and_final_value_lands(self):
        c=self.c;sent=[]
        self.assertEqual(c.ge_queue(1,0,40),1);sent.append(self.step(1))     # first edit goes out at once
        for v in (41,42,43,44):self.assertEqual(c.ge_queue(1,0,v),1);sent.append(self.step(10))
        self.assertEqual([s for s in sent[1:] if s],[])                          # inside 120 ms: nothing sent
        while True:
            f=self.step(10)
            if f:sent.append(f);break
        writes=[s for s in sent if s and s[0]==SLOT and s[1]==PARAMETER]
        self.assertEqual(len(writes),2)                                          # 5 edits -> 2 writes
        for _ in range(50):self.step(20)
        self.assertEqual(c.ge_value(1,0),44)
        self.assertAlmostEqual(c.ge_actual_value(1,0),10**(-5+4*.44),places=9)   # newest value reached the DSP
    def test_enable_writes_are_not_delayed_by_parameter_throttle(self):
        c=self.c
        self.assertEqual(c.ge_queue(1,0,30),1);self.step(1)
        self.assertEqual(c.ge_queue(1,0,31),1)                                   # parameter now held back
        slot=c.ge_selected();before=c.ge_actual_enabled(slot);c.ge_foot()
        f=self.step(1);self.assertIsNotNone(f);self.assertEqual((f[0],f[1]),(SLOT,ENABLE))
        self.assertEqual(c.ge_actual_enabled(slot),1-before)

if __name__=='__main__':unittest.main()
