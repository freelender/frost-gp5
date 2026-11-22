import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from chain_wav import run

class OverdriveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/overdrive.dll' if os.name=='nt' else 'build/overdrive.so'),ROOT/'tests/overdrive_host.c')))
        c=cls.lib
        c.od_sample.argtypes=[ct.c_uint]+[ct.c_float]*4;c.od_sample.restype=ct.c_float
        c.od_history.restype=c.od_bound.restype=c.fx_target.restype=ct.c_float
        c.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
    def setUp(self):
        for c in (0,1):self.lib.fx_init(c);self.lib.od_reset(c)
    def array(self,x):return (ct.c_uint*len(x))(*x)
    def configure(self,c,types):self.assertEqual(self.lib.fx_configure(c,self.array(types),len(types)),1)
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        self.lib.fx_process(c,a,b,len(x));return list(b)
    def direct(self,x,drive=4.,tone=.5,level=.5,c=0):
        return [self.lib.od_sample(c,v,drive,tone,level) for v in x]
    def test_double_reference_symmetry_and_output_bounds(self):
        # Independent double-precision recurrence with clamped cubic
        # shape evaluated as a polynomial; compare at rail crossings and tails.
        x=[16.,-16.,0.,1.,-1.]+[math.sin(i*.137)*.23 for i in range(12000)]+[0.]*5000
        for drive,tone,level in ((1.,0.,2.),(4.,.5,.5),(20.,1.,2.)):
            self.lib.od_reset(0);self.lib.od_reset(1);previous=hp=lp=0.;expected=[]
            for v in x:
                v=ct.c_float(v).value;hp=v-previous+.995*hp;previous=v
                u=max(-1.,min(1.,drive*hp));z=(3*u-u**3)/2
                lp+=(.03+.47*tone**2)*(z-lp);expected.append(level*lp)
            y=self.direct(x,drive,tone,level);negative=self.direct([-v for v in x],drive,tone,level,1)
            self.assertLess(max(abs(a-b) for a,b in zip(y,expected)),2e-5)
            self.assertEqual(y,[-v for v in negative])
            self.assertTrue(all(math.isfinite(v) and abs(v)<=level+1e-6 for v in y))
    def test_dc_rejection_tone_response_and_nonlinear_harmonics(self):
        y=self.direct([.5]*12000);self.assertLess(max(abs(v) for v in y[-1000:]),1e-8)
        n=4096;frequency=64
        x=[.2*math.sin(2*math.pi*frequency*i/n) for i in range(n*3)]
        def amplitude(y,k):return abs(sum(v*complex(math.cos(2*math.pi*k*i/n),-math.sin(2*math.pi*k*i/n)) for i,v in enumerate(y)))*2/n
        self.lib.od_reset(0);clean=self.direct(x,1.,1.,1.)[-n:]
        self.lib.od_reset(0);driven=self.direct(x,12.,1.,1.)[-n:]
        self.assertGreater(amplitude(driven,frequency*3)/amplitude(driven,frequency),.15)
        self.assertLess(amplitude(clean,frequency*3)/amplitude(clean,frequency),.01)
        high=[.001*math.sin(2*math.pi*700*i/n) for i in range(n*3)]
        self.lib.od_reset(0);dark=self.direct(high,1.,0.,1.)[-n:]
        self.lib.od_reset(0);bright=self.direct(high,1.,1.,1.)[-n:]
        self.assertGreater(amplitude(bright,700),10*amplitude(dark,700))
    def test_registry_defaults_metadata_and_rejected_targets(self):
        c=self.lib
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[0,1,2,3,255,255])
        self.assertEqual(c.fx_state_bytes(4),12);self.assertEqual(c.fx_workspace_bytes(4),0)
        self.assertEqual(c.fx_instance_bytes(),208);self.assertEqual(c.fx_bytes(),13920) # was 64/12936: 8 params + engage input + model state per slot, 8-param pending preset
        self.configure(0,[4]);self.assertEqual(c.fx_enabled(0,0),0)
        for p,initial in enumerate((4.,.5,.5)):
            self.assertEqual(c.fx_target(0,0,p),initial)
            bounds=((1.,20.,4.),(0.,1.,.5),(0.,2.,.5))[p]
            for f in range(3):self.assertEqual(c.od_bound(p,f),ct.c_float(bounds[f]).value)
            self.assertEqual((c.od_meta(p,0),c.od_meta(p,1),c.od_meta(p,3)),(0,100,1))
        for p,v in ((0,.9),(0,20.1),(1,-.01),(1,1.01),(2,2.01),(2,float('nan')),(3,1.)):
            self.assertEqual(c.fx_set(0,0,p,v),0)
    def test_native_rate_high_frequency_alias_is_explicit(self):
        # Third harmonic of bin1300 folds to196 at this native sample rate.
        # This characterizes a known limitation, not an anti-alias quality gate.
        n=4096;k=1300;x=[.2*math.sin(2*math.pi*k*i/n) for i in range(n*3)]
        y=self.direct(x,1.,1.,1.)[-n:]
        def amplitude(k):return abs(sum(v*complex(math.cos(2*math.pi*k*i/n),-math.sin(2*math.pi*k*i/n)) for i,v in enumerate(y)))*2/n
        self.assertGreater(amplitude(196),.0001)
        self.assertGreater(amplitude(k),amplitude(196))
    def test_duplicate_state_isolation_chunk_inplace_and_fault_reset(self):
        c=self.lib
        for chain in (0,1):
            self.configure(chain,[4,4,2,3])
            for slot in range(4):c.fx_enable(chain,slot,1)
            c.fx_set(chain,0,0,12.);c.fx_set(chain,1,1,.1)
        x=[math.sin(i*.23)*.25 for i in range(10000)];x[500]=float('nan');x[850]=17.
        y=self.process(0,x);other=[]
        for i in range(0,len(x),37):other+=self.process(1,x[i:i+37],True)
        self.assertEqual(y,other);self.assertEqual(c.fx_faults(0),2)
        self.assertEqual(y[500],0.);self.assertEqual(y[850],0.)
        self.assertNotEqual(c.od_history(0,0,2),c.od_history(0,1,2))
        c.fx_set(0,0,0,2.);self.process(0,[.2]*100)
        self.assertEqual(c.fx_target(1,0,0),12.)
        self.assertTrue(all(math.isfinite(v) for v in y))
    def test_cut_bypass_resets_history_and_route_type_defaults(self):
        c=self.lib;self.configure(0,[4]);self.assertEqual(c.od_policy(0,0,1),1)
        c.fx_enable(0,0,1);self.process(0,[.3]*1000);c.fx_enable(0,0,0)
        y=self.process(0,[.1]*128)
        self.assertAlmostEqual(y[-1],.1,places=7)
        self.assertEqual([c.od_history(0,0,i) for i in range(3)],[0.]*3)
        self.assertEqual(self.process(0,[0.]*256),[0.]*256)
        c.fx_enable(0,0,1);self.assertEqual(self.process(0,[0.]*128),[0.]*128)
        c.fx_set(0,0,0,19.)
        types=[0,255,255,255,4,255];order=list(range(6))
        self.assertEqual(c.fx_request(0,self.array(types),self.array(order),3),1)
        self.process(0,[0.]*256)
        self.assertEqual(c.fx_type(0,4),4);self.assertEqual(c.fx_target(0,4,0),4.)
        self.assertEqual(c.fx_enabled(0,4),0)
    def test_offline_wav_routes_saturation_and_invalid_settings(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            source=Path(folder)/'in.wav';out=Path(folder)/'out.wav';dry=Path(folder)/'dry.wav'
            samples=[round(math.sin(i*.11)*26000) for i in range(10000)]
            with wave.open(str(source),'wb') as w:w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
            run(source,dry,effects=[])
            run(source,out,effects=['overdrive','overdrive','eq'],overdrive=(20.,1.,2.),bypass_policy='cut')
            with wave.open(str(out),'rb') as w:
                self.assertEqual(w.getnframes(),len(samples));data=w.readframes(len(samples));y=struct.unpack('<'+'h'*len(samples),data)
            self.assertNotEqual(out.read_bytes(),dry.read_bytes());self.assertTrue(any(abs(v)>=32767 for v in y))
            with self.assertRaises(ValueError):run(source,out,effects=['overdrive'],overdrive=(21.,.5,.5))

if __name__=='__main__':unittest.main()
