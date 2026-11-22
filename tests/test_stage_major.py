import ctypes as ct
import math
import os
from pathlib import Path
import random
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

GAIN,GATE,EQ,IR,OD,DELAY,CHORUS,FLANGER,REVERB=range(9);EMPTY=255

class StageMajorTests(unittest.TestCase):
    """Stage-major guarded processing must match the sample-major core bit for bit."""
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/stage_major_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/stage_major_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.sm_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_float];cls.c.sm_bound.restype=ct.c_float
        cls.c.sm_block.argtypes=[ct.POINTER(ct.c_float),ct.c_uint,ct.c_uint,ct.c_uint]
    def block(self,values,amp=0,inplace=0):
        a=(ct.c_float*len(values))(*values)
        self.assertEqual(self.c.sm_block(a,len(values),amp,inplace),1,'stage-major output diverged')
    def route(self,types,order=(0,1,2,3,4,5),pre=6):
        r=self.c.sm_route((ct.c_uint*6)(*types),(ct.c_uint*6)(*order),pre);self.assertNotEqual(r,99);return r
    def signal(self,rng,n,t):
        return [0.4*math.sin(0.031*(t+i))+0.2*math.sin(0.27*(t+i))+0.05*(rng.random()-0.5) for i in range(n)]
    def settle(self,rng,t):
        while self.c.sm_transition():self.block(self.signal(rng,64,t));t+=64
        return t
    def test_random_routes_parameters_bypass_and_policies_match_bitwise(self):
        rng=random.Random(20261113);c=self.c
        heavy=[(GAIN,GATE,EQ,IR,EMPTY,EMPTY),(OD,GATE,EQ,IR,DELAY,REVERB),(GAIN,OD,CHORUS,IR,FLANGER,EQ),
               (REVERB,DELAY,OD,IR,IR,GATE),(EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY),(CHORUS,EQ,GAIN,OD,IR,DELAY)]
        for trial in range(6):
            c.sm_init();t=0
            for step in range(40):
                choice=rng.random()
                if choice<0.15:
                    types=list(rng.choice(heavy));order=list(range(6));rng.shuffle(order)
                    self.route(types,order,rng.randint(0,6))
                elif choice<0.55:
                    slot=rng.randrange(6);k=c.sm_param_count(slot)
                    if k:p=rng.randrange(k);lo,hi=c.sm_bound(slot,p,0),c.sm_bound(slot,p,1);c.sm_set(slot,p,lo+(hi-lo)*rng.random())
                elif choice<0.8:c.sm_enable(rng.randrange(6),rng.randint(0,1))
                elif choice<0.9:c.sm_policy(rng.randrange(6),rng.randint(0,1))
                else:
                    slot=rng.randrange(6)
                    if c.sm_type(slot)==IR:c.sm_ir(slot,rng.choice((1,16,64,128,256)))
                for _ in range(rng.randint(1,6)):
                    n=rng.choice((64,64,64,17,1,37,128));amp=rng.randint(0,1)
                    self.block(self.signal(rng,n,t),amp,rng.randint(0,1));t+=n
                    if amp and not c.sm_transition():pass
            self.assertEqual(c.sm_faults(),0)
    def test_all_types_enabled_long_run_and_amp_call_counts(self):
        rng=random.Random(7);c=self.c;c.sm_init()
        self.route((OD,DELAY,CHORUS,IR,REVERB,FLANGER),pre=3);t=self.settle(rng,0)
        for slot in range(6):c.sm_enable(slot,1)
        c.sm_ir(3,128)
        for _ in range(300):self.block(self.signal(rng,64,t),amp=1);t+=64
        self.assertEqual(c.sm_amp_calls_equal(),1)
    def test_block_reverb_matches_per_sample_through_fill_wrap_and_parameter_motion(self):
        rng=random.Random(11);c=self.c;c.sm_init()
        self.route((GAIN,REVERB,EQ,IR,OD,EMPTY),pre=6);t=self.settle(rng,0)
        c.sm_enable(1,1)
        for block in range(220):                          # > 14k samples: every line fills and wraps many times
            if block%7==0:
                for param in range(3):
                    lo,hi=c.sm_bound(1,param,0),c.sm_bound(1,param,1);c.sm_set(1,param,lo+(hi-lo)*rng.random())
            if block==120:c.sm_enable(1,0)               # bypass fade while the tail rings
            if block==150:c.sm_enable(1,1)
            n=rng.choice((64,64,16,15,17,1,33,128))
            self.block(self.signal(rng,n,t),amp=block%2,inplace=block%3==0);t+=n
        c.sm_policy(1,1);t=self.settle(rng,t)            # CUT policy -> per-sample fallback
        for block in range(30):self.block(self.signal(rng,64,t));t+=64
        self.assertEqual(c.sm_faults(),0)
    def test_faults_match_reference_result_output_and_count(self):
        rng=random.Random(3);c=self.c
        for position in (0,5,63):
            for bad in (float('nan'),float('inf'),17.0):
                c.sm_init();self.route((GAIN,EQ,IR,OD,EMPTY,EMPTY));self.settle(rng,0)
                values=self.signal(rng,64,0);values[position]=bad;self.block(values,amp=1)
                self.assertEqual(c.sm_faults(),1)

if __name__=='__main__':unittest.main()
