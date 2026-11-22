import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class BypassTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/bypass.dll' if os.name=='nt' else 'build/bypass.so'),ROOT/'tests/bypass_host.c')))
        cls.lib.bypass_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.bypass_route.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.bypass_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        cls.lib.bypass_ir.argtypes=[ct.c_uint,ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.bypass_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        for name in ('bypass_wet','bypass_current','bypass_coefficient'):getattr(cls.lib,name).restype=ct.c_float
    def setUp(self):
        for c in (0,1):self.lib.bypass_init(c)
    def configure(self,c,types):self.assertEqual(self.lib.bypass_configure(c,(ct.c_uint*len(types))(*types),len(types)),1)
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=a if inplace else (ct.c_float*len(x))()
        self.lib.bypass_process(c,a,b,len(x));return list(b)
    def ir(self,c,slot,values):self.assertEqual(self.lib.bypass_ir(c,slot,(ct.c_float*len(values))(*values),len(values)),1)
    def policy(self,c,slot,policy=1):self.assertEqual(self.lib.bypass_policy(c,slot,policy),1)
    def test_overdrive_cut_skips_kernel_clears_history_and_smooths_targets(self):
        c=self.lib;self.configure(0,[4]);self.policy(0,0)
        self.process(0,[.1]*1000);self.assertEqual(c.bypass_calls(0,4),0)
        c.bypass_enable(0,0,1);self.process(0,[.2]*128)
        self.assertEqual(c.bypass_calls(0,4),128)
        c.bypass_enable(0,0,0);self.process(0,[.1]*128)
        self.assertEqual(c.bypass_calls(0,4),255)
        self.assertTrue(c.bypass_history_zero(0,0))
        c.bypass_set(0,0,0,20.);self.process(0,[.1]*4000)
        self.assertEqual(c.bypass_calls(0,4),255)
        self.assertGreater(c.bypass_current(0,0,0),19.99)
        c.bypass_enable(0,0,1);self.process(0,[0.]*128)
        self.assertEqual(c.bypass_calls(0,4),383)
    def test_policy_is_explicit_cold_only_and_validated(self):
        self.assertEqual([self.lib.bypass_get_policy(0,i) for i in range(6)],[0]*6)
        for slot,policy in ((6,1),(0,2),(0,0xffffffff)):
            self.assertEqual(self.lib.bypass_policy(0,slot,policy),0)
        self.policy(0,4) # policies can be assigned to reserved empty slots
        self.process(0,[]);self.policy(0,0)
        self.process(0,[.1]);self.assertEqual(self.lib.bypass_policy(0,0,0),0)
        self.configure(1,[0]);self.policy(1,0);self.configure(1,[0])
        self.assertEqual(self.lib.bypass_get_policy(1,0),0) # cold configure restores defaults
    def test_default_history_runs_but_cut_skips_all_real_kernels(self):
        for slot in range(4):self.policy(1,slot)
        x=[math.sin(i*.13)*.1 for i in range(4096)]
        self.assertEqual(self.process(0,x),self.process(1,x))
        self.assertEqual([self.lib.bypass_calls(0,i) for i in range(4)],[4096]*4)
        self.assertEqual([self.lib.bypass_calls(1,i) for i in range(4)],[0]*4)
        self.assertEqual([self.lib.bypass_suspended(1,i) for i in range(4)],[1]*4)
    def test_512_tap_ir_stops_at_exact_dry_and_keeps_asset(self):
        self.configure(0,[3]);self.policy(0,0)
        self.ir(0,0,[.5]+[.5/511]*511);self.lib.bypass_enable(0,0,1)
        self.process(0,[.1]*128);self.assertEqual(self.lib.bypass_wet(0,0),1.)
        self.assertEqual(self.lib.bypass_calls(0,3),128)
        self.lib.bypass_enable(0,0,0);self.process(0,[.1]*127)
        self.assertEqual(self.lib.bypass_suspended(0,0),0)
        self.assertEqual(self.lib.bypass_wet(0,0),1/128)
        self.assertEqual(self.lib.bypass_calls(0,3),255)
        self.assertEqual(self.process(0,[.1]),[ct.c_float(.1).value])
        self.assertEqual(self.lib.bypass_calls(0,3),255)
        self.assertEqual(self.lib.bypass_suspended(0,0),1)
        self.assertTrue(self.lib.bypass_history_zero(0,0))
        position=self.lib.bypass_position(0,0)
        x=[math.sin(i*.2)*.2 for i in range(4096)];self.process(0,x)
        self.assertEqual(self.lib.bypass_calls(0,3),255)
        self.assertEqual(self.lib.bypass_position(0,0),position)
        self.assertEqual(self.lib.bypass_coefficient(0,0,0),.5)
        self.assertAlmostEqual(self.lib.bypass_coefficient(0,0,511),.5/511,places=9)
    def test_all_recursive_histories_clear_and_no_old_tail_returns(self):
        for slot in range(4):self.policy(0,slot);self.lib.bypass_enable(0,slot,1)
        self.ir(0,3,[.5]+[0.]*510+[.5])
        self.lib.bypass_set(0,2,0,1.7);self.process(0,[.1]*1000)
        for slot in range(4):self.lib.bypass_enable(0,slot,0)
        self.process(0,[0.]*128)
        for slot in range(4):
            self.assertTrue(self.lib.bypass_history_zero(0,slot))
            self.assertTrue(self.lib.bypass_suspended(0,slot))
        calls=[self.lib.bypass_calls(0,i) for i in range(4)]
        self.process(0,[.2]*1000)
        self.assertEqual([self.lib.bypass_calls(0,i) for i in range(4)],calls)
        for slot in range(4):self.lib.bypass_enable(0,slot,1)
        self.assertEqual(self.process(0,[0.]*600),[0.]*600)
    def test_reversal_and_reactivation_are_smooth_and_parameters_stay_live(self):
        self.configure(0,[0]);self.policy(0,0);self.lib.bypass_set(0,0,0,2.)
        self.lib.bypass_enable(0,0,1);self.process(0,[.1]*7000)
        self.lib.bypass_enable(0,0,0);before=self.process(0,[.1]*31)[-1]
        self.assertEqual(self.lib.bypass_wet(0,0),97/128)
        self.lib.bypass_enable(0,0,1);after=self.process(0,[.1])[0]
        self.assertEqual(self.lib.bypass_wet(0,0),98/128)
        self.assertLess(abs(after-before),.001)
        self.lib.bypass_enable(0,0,0);self.process(0,[.1]*128)
        self.lib.bypass_set(0,0,0,3.);calls=self.lib.bypass_calls(0,0)
        self.process(0,[0.]*7000)
        self.assertEqual(self.lib.bypass_calls(0,0),calls)
        self.assertAlmostEqual(self.lib.bypass_current(0,0,0),3.,places=4)
        self.lib.bypass_enable(0,0,1);y=self.process(0,[.1]*128)
        self.assertAlmostEqual(y[-1],.3,places=5)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.002)
    def test_independent_duplicate_ir_policies(self):
        self.configure(0,[3,3]);self.policy(0,0)
        self.process(0,[.1]*2000)
        self.assertEqual(self.lib.bypass_ir_calls(0,0),0)
        self.assertEqual(self.lib.bypass_ir_calls(0,1),2000)
        self.assertEqual((self.lib.bypass_suspended(0,0),self.lib.bypass_suspended(0,1)),(1,0))
        self.lib.bypass_enable(0,0,1);self.process(0,[.1])
        self.assertEqual(self.lib.bypass_ir_calls(0,0),1)
        self.assertEqual(self.lib.bypass_ir_calls(0,1),2001)
        self.assertEqual(self.lib.bypass_calls(1,3),0)
    def test_chunk_inplace_faults_and_repeated_toggles(self):
        def render(c,chunk):
            for slot in range(4):self.policy(c,slot)
            result=[]
            for phase in range(6):
                for slot in range(4):self.lib.bypass_enable(c,slot,phase%2)
                x=[((i*131)%1023-511)/8192 for i in range(351)]
                x[20]=float('nan');x[46]=17.
                for i in range(0,len(x),chunk):result+=self.process(c,x[i:i+chunk],inplace=c==1)
            return result
        self.assertEqual(render(0,37),render(1,64))
        # Fault containment does not wake an already suspended effect.
        self.lib.bypass_enable(0,3,0);self.process(0,[0.]*128)
        count=self.lib.bypass_calls(0,3);self.process(0,[float('nan'),.1])
        self.assertEqual(self.lib.bypass_calls(0,3),count)
        self.assertTrue(self.lib.bypass_suspended(0,3))
    def test_slot_policy_survives_type_change_and_empty_slot_activation(self):
        self.policy(0,0);self.policy(0,4)
        self.process(0,[.1]*200)
        types=(ct.c_uint*6)(2,1,2,3,3,255);order=(ct.c_uint*6)(*range(6))
        self.assertEqual(self.lib.bypass_route(0,types,order,3),1)
        self.process(0,[0.]*256)
        for slot in (0,4):
            self.assertEqual(self.lib.bypass_get_policy(0,slot),1)
            self.assertEqual(self.lib.bypass_suspended(0,slot),1)
        # Original IR (pool 0, KEEP) runs; new IR (pool 1, CUT) never processes.
        self.assertEqual(self.lib.bypass_ir_calls(0,1),0)
        self.lib.bypass_enable(0,4,1);self.process(0,[.1])
        self.assertEqual(self.lib.bypass_ir_calls(0,1),1)

if __name__=='__main__':unittest.main()
