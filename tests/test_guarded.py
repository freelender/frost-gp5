import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class GuardedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/guarded'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/guarded_host.c')))
        cls.c.guarded_process.argtypes=cls.c.guarded_legacy.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint,ct.c_uint]
        cls.c.chain_faults.restype=ct.c_uint32
    def setUp(self):self.c.guarded_init()
    def run_block(self,values,amp=0,inplace=False,legacy=False):
        n=len(values);x=(ct.c_float*n)(*values);y=x if inplace else (ct.c_float*n)()
        if legacy:self.c.guarded_legacy(x,y,n,amp);result=None
        else:result=self.c.guarded_process(x,y,n,amp)
        return result,bytes(y)
    def test_valid_streams_match_legacy_bitwise_inplace_and_partitioned(self):
        values=[math.sin(i*.13)*.2 for i in range(2048)]
        for amp in (0,1):
            self.c.guarded_init();_,expected=self.run_block(values,amp,legacy=True)
            for chunk in (17,64,2048):
                self.c.guarded_init();actual=b''
                for start in range(0,len(values),chunk):
                    result,data=self.run_block(values[start:start+chunk],amp,inplace=True)
                    self.assertEqual(result,1);actual+=data
                self.assertEqual(actual,expected)
    def test_fault_discards_prefix_and_stops_callbacks_after_single_reset(self):
        self.c.guarded_fire(3)
        result,data=self.run_block([.25]*64,amp=1)
        self.assertEqual(result,0);self.assertEqual(data,bytes(64*4))
        self.assertEqual((self.c.guarded_calls(0),self.c.guarded_calls(1)),(4,1))
        self.assertEqual(self.c.chain_faults(),1)
        # Core policy is per-call; the pedal adapter owns the boot latch.
        self.c.guarded_fire(0xffffffff)
        result,data=self.run_block([.25]*64,amp=1)
        self.assertEqual(result,1);self.assertNotEqual(data,bytes(64*4))
    def test_bad_input_at_any_position_stops_before_amp_and_clears_inplace(self):
        for position in (0,17,63):
            for bad in (float('nan'),float('inf'),17.):
                self.c.guarded_init();values=[.25]*64;values[position]=bad
                result,data=self.run_block(values,amp=1,inplace=True)
                self.assertEqual((result,data),(0,bytes(256)))
                self.assertEqual((self.c.guarded_calls(0),self.c.guarded_calls(1)),(position,1))
                self.assertEqual(self.c.chain_faults(),1)
    def test_saturated_counter_does_not_hide_fault_or_resume_remaining_samples(self):
        self.c.guarded_saturate();self.c.guarded_fire(2)
        result,data=self.run_block([.25]*64,amp=1)
        self.assertEqual((result,data),(0,bytes(256)))
        self.assertEqual(self.c.chain_faults(),0xffffffff)
        self.assertEqual((self.c.guarded_calls(0),self.c.guarded_calls(1)),(3,1))
    def test_empty_call_does_not_start_or_reset_anything(self):
        self.assertEqual(self.c.guarded_process(None,None,0,1),1)
        self.assertEqual(self.c.guarded_started(),0)
        self.assertEqual((self.c.guarded_calls(0),self.c.guarded_calls(1)),(0,0))
    def test_fault_does_not_continue_route_fade_after_failing_sample(self):
        self.assertEqual(self.c.guarded_transition(),1)
        self.assertEqual(self.run_block([.25]*5,amp=1)[0],1)
        self.assertEqual(self.c.guarded_position(),5)
        self.c.guarded_fire(2)
        self.assertEqual(self.run_block([.25]*7,amp=1),(0,bytes(28)))
        # 31a: fades run stage-major; a faulted block (silenced, histories reset)
        # keeps none of its fade progress, so the fade resumes from 5, never past
        # the failing sample. Sample-major fallback blocks still stop at it.
        self.assertEqual(self.c.guarded_position(),5)
        self.assertEqual(self.c.guarded_calls(1),1)

if __name__=='__main__':unittest.main()
