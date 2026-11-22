import ctypes as ct
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
EMPTY=255
IDENTITY=list(range(6))

class RoutingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/routing.dll' if os.name=='nt' else 'build/routing.so'),ROOT/'tests/framework_host.c')))
        for name in ('fx_configure',):getattr(cls.lib,name).argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        cls.lib.fx_ir.argtypes=[ct.c_uint,ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
        for name in ('fx_process','fx_inplace','fx_amp_process'):
            getattr(cls.lib,name).argtypes=([ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint] if name=='fx_inplace' else
                [ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]+([ct.c_uint] if name=='fx_amp_process' else []))
        cls.lib.fx_target.restype=cls.lib.fx_ir_coefficient.restype=ct.c_float
    def setUp(self):
        for c in (0,1):self.lib.fx_init(c)
    def array(self,v):return (ct.c_uint*len(v))(*v)
    def configure(self,c,types):self.assertEqual(self.lib.fx_configure(c,self.array(types),len(types)),1)
    def route(self,c,types,order=IDENTITY,pre=3):
        return self.lib.fx_request(c,self.array(types),self.array(order),pre)
    def process(self,c,x,amp=None,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        if amp is None:self.lib.fx_process(c,a,b,len(x))
        else:self.lib.fx_amp_process(c,a,b,len(x),amp)
        return list(b)
    def gain_setup(self,c):
        self.configure(c,[0]);self.lib.fx_set(c,0,0,2.);self.lib.fx_enable(c,0,1)
        self.process(c,[.1]*7000)
    def test_atomic_validation_noop_and_copied_request(self):
        types=[0,1,2,3,EMPTY,EMPTY]
        self.assertEqual(self.route(0,types),1);self.assertEqual(self.lib.fx_phase(0),0)
        invalid=[([0,99,2,3,EMPTY,EMPTY],IDENTITY,3),([3,3,3,EMPTY,EMPTY,EMPTY],IDENTITY,3),
                 (types,[0,0,2,3,4,5],3),(types,[0,1,2,3,4,6],3),(types,IDENTITY,7)]
        for t,o,pre in invalid:
            self.assertEqual(self.route(0,t,o,pre),0)
            self.assertEqual(self.lib.fx_phase(0),0)
        self.assertEqual(self.lib.fx_request(0,None,self.array(IDENTITY),3),0)
        self.assertEqual(self.lib.fx_request(0,self.array(types),None,3),0)
        x=[math.sin(i*.12)*.1 for i in range(500)]
        self.assertEqual(self.process(0,x),self.process(1,x))
        t=self.array(types);o=self.array([5,4,3,2,1,0])
        self.assertEqual(self.lib.fx_request(0,t,o,0),1)
        t[0]=99;o[0]=99 # caller can release/change the request immediately
        self.process(0,[0.]*256)
        self.assertEqual([self.lib.fx_order(0,i) for i in range(6)],[5,4,3,2,1,0])
        self.assertEqual(self.lib.fx_type(0,0),0)
    def test_exact_fade_commit_and_busy_controls(self):
        self.gain_setup(0)
        self.assertEqual(self.route(0,[2,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY],pre=0),1)
        self.assertEqual(self.lib.fx_set(0,0,0,3.),0)
        self.assertEqual(self.lib.fx_enable(0,0,0),0)
        self.assertEqual(self.route(0,[0]*6),0)
        self.process(0,[]);self.assertEqual(self.lib.fx_phase(0),1)
        y=self.process(0,[.1]*127)
        self.assertEqual(self.lib.fx_type(0,0),0)
        y+=self.process(0,[.1]);self.assertEqual(y[-1],0.)
        self.assertEqual(self.lib.fx_type(0,0),2)
        self.assertEqual(self.lib.fx_phase(0),2)
        self.assertEqual(self.lib.fx_enabled(0,0),0)
        self.assertEqual(self.lib.fx_target(0,0,0),1.)
        self.assertEqual(self.route(0,[0]*6),0)
        y+=self.process(0,[.1]*127);self.assertEqual(self.lib.fx_phase(0),2)
        y+=self.process(0,[.1]);self.assertEqual(self.lib.fx_phase(0),0)
        self.assertAlmostEqual(y[-1],.1,places=6)
        self.assertLess(max(abs(a-b) for a,b in zip(y,y[1:])),.002)
        self.assertEqual(self.lib.fx_enable(0,0,1),1)
    def test_amp_boundary_and_order_are_observable_without_nam(self):
        # Synthetic hard clip is a test oracle, not a NAM implementation.
        for pre,expected in ((0,.2),(1,.15),(6,.15)):
            self.lib.fx_init(0);self.gain_setup(0)
            self.assertEqual(self.route(0,[0,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY],pre=pre),1)
            self.process(0,[.1]*256,amp=1)
            self.assertAlmostEqual(self.process(0,[.1],amp=1)[0],expected,places=5)
            self.assertEqual(self.lib.fx_amp_calls(0),257)
        self.lib.fx_init(0);self.configure(0,[0,0])
        for slot,gain in ((0,2.),(1,.5)):
            self.lib.fx_set(0,slot,0,gain);self.lib.fx_enable(0,slot,1)
        self.route(0,[0,0,EMPTY,EMPTY,EMPTY,EMPTY],pre=1)
        self.process(0,[.2]*10000,amp=1)
        self.assertAlmostEqual(self.process(0,[.2],amp=1)[0],.075,places=5)
        resets=self.lib.fx_amp_resets(0)
        self.route(0,[0,0,EMPTY,EMPTY,EMPTY,EMPTY],[1,0,2,3,4,5],pre=1)
        self.process(0,[.2]*256,amp=1)
        self.assertEqual(self.lib.fx_amp_resets(0),resets+1)
        self.assertAlmostEqual(self.process(0,[.2],amp=1)[0],.2,places=5)
    def test_ir_assets_retained_new_instances_independent_and_tails_cut(self):
        self.configure(0,[3,3]);coeff=(ct.c_float*3)(.7,.2,.1)
        self.lib.fx_ir(0,1,coeff,3);self.lib.fx_enable(0,1,1)
        self.process(0,[0.]*5000)
        self.route(0,[0,3,3,EMPTY,EMPTY,EMPTY],[2,1,0,3,4,5],3)
        self.assertEqual(self.lib.fx_ir(0,1,coeff,3),0) # asset replacement is busy too
        self.process(0,[0.]*256)
        self.assertEqual(self.lib.fx_ir_taps(0,1),3)
        self.assertAlmostEqual(self.lib.fx_ir_coefficient(0,1,0),.7,places=6)
        self.assertEqual(self.lib.fx_ir_taps(0,2),1)
        self.assertEqual(self.lib.fx_ir_coefficient(0,2,0),1.)
        self.assertTrue(self.lib.fx_ir_distinct(0,1,2))
        self.assertEqual(self.lib.fx_enabled(0,1),1)
        self.assertEqual(self.lib.fx_enabled(0,2),0)
        self.lib.fx_init(0);self.configure(0,[3])
        delayed=(ct.c_float*201)(*([0.]*200+[1.]))
        self.lib.fx_ir(0,0,delayed,201);self.lib.fx_enable(0,0,1)
        self.process(0,[0.]*5000);self.process(0,[.1])
        self.route(0,[3,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY],pre=0)
        self.assertEqual(self.process(0,[0.]*400),[0.]*400)
        self.assertEqual(self.lib.fx_ir_taps(0,0),201)
    def test_transitions_partition_inplace_and_chain_isolation(self):
        for c in (0,1):self.gain_setup(c)
        types=[2,0,3,1,EMPTY,EMPTY];order=[3,1,0,2,4,5]
        self.route(0,types,order,2)
        x=[math.sin(i*.2)*.1 for i in range(1000)];x[10]=float('nan')
        reference=self.process(0,x)
        self.assertEqual(self.lib.fx_type(1,0),0)
        self.assertEqual(self.lib.fx_phase(1),0)
        self.route(1,types,order,2);actual=[]
        for i in range(0,len(x),37):actual+=self.process(1,x[i:i+37],inplace=True)
        self.assertEqual(reference,actual)
        self.assertEqual(actual[10],0.)
        self.assertEqual(self.lib.fx_phase(1),0)
        self.assertEqual(self.lib.fx_faults(1),1)
    def test_amp_faults_contained_and_faulting_frames_advance_transition(self):
        self.route(0,[0,1,2,3,EMPTY,EMPTY],pre=0)
        y=self.process(0,[.1]*256,amp=2)
        self.assertEqual(y,[0.]*256)
        self.assertEqual(self.lib.fx_phase(0),0)
        self.assertEqual(self.lib.fx_faults(0),256)
        self.assertEqual(self.process(0,[.1],amp=3),[0.])
        self.assertEqual(self.process(0,[float('nan')],amp=1),[0.])
        self.assertTrue(math.isfinite(self.process(0,[.1],amp=1)[0]))

    def test_amp_history_is_cut_at_silent_commit(self):
        self.configure(0,[])
        self.route(0,[EMPTY]*6,list(reversed(IDENTITY)),pre=0)
        self.process(0,[.1]*127,amp=4)
        self.assertEqual(self.lib.fx_amp_resets(0),0)
        self.assertEqual(self.process(0,[.1],amp=4),[0.])
        self.assertEqual(self.lib.fx_amp_resets(0),1)
        self.assertEqual(self.process(0,[0.],amp=4),[0.])
        self.assertEqual(self.lib.fx_amp_calls(0),129)

if __name__=='__main__':unittest.main()
