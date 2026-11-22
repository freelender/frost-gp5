import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class FrameworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/framework.dll' if os.name=='nt' else 'build/framework.so'),ROOT/'tests/framework_host.c')))
        cls.lib.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        cls.lib.fx_set_initial.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        cls.lib.fx_ir.argtypes=[ct.c_uint,ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        cls.lib.fx_eq_low.restype=ct.c_float
    def setUp(self):
        for c in range(2):self.lib.fx_init(c)
    def route(self,c,types):
        return self.lib.fx_configure(c,(ct.c_uint*len(types))(*types),len(types))
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        self.lib.fx_process(c,a,b,len(x));return list(b)
    def test_registry_and_atomic_cold_routes(self):
        self.assertEqual([self.lib.fx_parameters(i) for i in range(9)],[1,1,3,1,3,3,3,3,3])
        self.assertEqual(self.lib.fx_parameters(52),0) # first unregistered ID after the Stage 5 additions
        self.assertEqual(self.lib.fx_workspace_bytes(3),4104)
        self.assertEqual(self.route(0,[2,0,2,255,3,1]),1)
        for bad in ([0,99],[0]*7,[3,3,3]):self.assertEqual(self.route(0,bad),0)
        self.assertEqual([self.lib.fx_type(0,i) for i in range(6)],[2,0,2,255,3,1])
        self.assertEqual(self.lib.fx_configure(0,None,1),0)
        self.assertEqual(self.lib.fx_enable(0,3,1),0)
        self.assertEqual(self.lib.fx_set(0,3,0,1.),0)
        self.assertEqual(self.lib.fx_set(0,6,0,1.),0)
        self.assertEqual(self.lib.fx_set(0,0,3,1.),0)
        self.process(0,[]) # zero-sized call does not lock configuration
        self.assertEqual(self.route(0,[0]),1)
        self.process(0,[.1])
        self.assertEqual(self.route(0,[2]),0)
        self.assertEqual(self.lib.fx_type(0,0),0)
    def test_duplicate_ir_instances_and_chain_isolation(self):
        # Different FIRs must convolve, never share coefficients/history.
        self.assertEqual(self.route(0,[3,3]),1)
        self.assertEqual(self.route(1,[]),1)
        for slot,coeff in enumerate(([1.,.5],[1.,-.25])):
            self.assertEqual(self.lib.fx_ir(0,slot,(ct.c_float*2)(*coeff),2),1)
            self.assertEqual(self.lib.fx_set_initial(0,slot,0,1.0),1) # isolate convolution at unity VOL
            self.assertEqual(self.lib.fx_enable(0,slot,1),1)
        self.process(0,[0.]*5000)
        y=self.process(0,[.1]+[0.]*8)
        for actual,expected in zip(y,[.1,.025,-.0125]+[0.]*6):self.assertAlmostEqual(actual,expected,places=6)
        self.assertEqual(self.process(1,[0.]*9),[0.]*9)
        self.assertEqual(self.lib.fx_ir(0,6,(ct.c_float*1)(1),1),0)
    def test_duplicate_recursive_histories_and_parameter_isolation(self):
        # A disabled leading gain changes only its own parameter state.
        for c in range(2):
            self.route(c,[0,1,2,2])
            for slot in (1,2,3):self.lib.fx_enable(c,slot,1)
            self.lib.fx_set(c,2,0,1.7);self.lib.fx_set(c,3,2,.2)
        self.lib.fx_set(0,0,0,3.)
        x=[math.sin(i*.17)*.1 for i in range(8000)]
        self.assertEqual(self.process(0,x),self.process(1,x))
        for slot in (1,2,3):self.assertTrue(self.lib.fx_same_slot(0,1,slot))
        # Different EQ settings must generate different recursive histories.
        self.assertNotEqual(self.lib.fx_eq_low(0,2),self.lib.fx_eq_low(0,3))
        # Cross-chain edits and processing never mutate the untouched chain.
        self.lib.fx_set(0,2,0,.3);self.process(0,x)
        self.assertFalse(self.lib.fx_same_slot(0,1,2))
    def test_six_slots_chunk_inplace_and_fault_recovery(self):
        for c in range(2):
            self.route(c,[1,2,0,2,3,1])
            for slot in range(6):self.lib.fx_enable(c,slot,1)
            self.lib.fx_set(c,2,0,1.3);self.lib.fx_set(c,3,1,.2)
        x=[math.sin(i*.3)*.15 for i in range(6000)]
        x[75]=float('nan');x[120]=17.
        expected=self.process(0,x);actual=[]
        for i in range(0,len(x),37):actual.extend(self.process(1,x[i:i+37],True))
        self.assertEqual(expected,actual)
        self.assertEqual(expected[75],0.);self.assertEqual(expected[120],0.)
        self.assertTrue(all(math.isfinite(v) for v in actual))

if __name__=='__main__':unittest.main()
