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
f32=lambda x:ct.c_float(x).value
LENGTHS=(1009,1153,1289,1423,149,211)

class ReverbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/reverb.dll' if os.name=='nt' else 'build/reverb.so'),ROOT/'tests/reverb_host.c')))
        c=cls.lib;c.rv_sample.argtypes=[ct.c_uint]+[ct.c_float]*4
        for n in ('rv_sample','rv_damped','rv_bound','fx_target'):getattr(c,n).restype=ct.c_float
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
    def setUp(self):
        for c in (0,1):self.lib.fx_init(c);self.lib.rv_poison(c);self.lib.rv_reset(c)
    def array(self,v):return (ct.c_uint*len(v))(*v)
    def configure(self,c,types):return self.lib.fx_configure(c,self.array(types),len(types))
    def route(self,c,types,order=None):return self.lib.fx_request(c,self.array(types),self.array(order or list(range(6))),3)
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        self.lib.fx_process(c,a,b,len(x));return list(b)
    def direct(self,x,decay=.75,damp=.5,mix=1.,c=0):return [self.lib.rv_sample(c,v,decay,damp,mix) for v in x]
    def test_analytic_first_arrivals_and_allpass_energy(self):
        y=self.direct([1.]+[0.]*50000,decay=0.,damp=0.)
        self.assertEqual(y[:1009],[0.]*1009)
        a=2*math.pi*180/44100;a/=1+a
        self.assertAlmostEqual(y[1009],(1-a)**2/64,places=8)
        # Independently construct the filtered excitation and feedforward comb
        # bank; the following allpass cascade must preserve its energy.
        z0=z1=0.;send=[];comb=[]
        for i in range(len(y)):
            x=1. if i==0 else 0.
            z0+=a*(x-z0);hp=x-z0;z1+=a*(hp-z1);send.append(hp-z1)
            comb.append(sum(send[i-L] for L in LENGTHS[:4] if i>=L)/16)
        self.assertAlmostEqual(sum(v*v for v in y),sum(v*v for v in comb),places=8)
    def test_block_equals_sample_bitwise_through_warmup_and_steady_wraps(self):
        self.lib.rv_block_matches_sample.restype=ct.c_int
        for seed in (1,7,2501):self.assertEqual(self.lib.rv_block_matches_sample(12000,seed),-1)
    def test_independent_unbounded_double_reference_and_wraps(self):
        history=[[] for _ in LENGTHS];filtered=[0.]*4;bass=[0.]*4;expected=[]
        send_z=[0.,0.];ah=2*math.pi*180/44100;ah/=1+ah
        ab=2*math.pi*300/44100;ab/=1+ab
        x=[math.sin(i*.071)*.3 for i in range(45000)]
        for n,v in enumerate(x):
            wet=0.;dry=f32(v);v=dry
            for i in range(2):send_z[i]+=ah*(v-send_z[i]);v-=send_z[i]
            for i,length in enumerate(LENGTHS[:4]):
                delayed=history[i][n-length] if n>=length else 0.
                filtered[i]+=(1-.95*.6)*(delayed-filtered[i])
                bass[i]+=ab*(filtered[i]-bass[i])
                history[i].append(.25*v+.85*(filtered[i]-.15*bass[i]));wet+=delayed*.25
            for i in (4,5):
                delayed=history[i][n-LENGTHS[i]] if n>=LENGTHS[i] else 0.
                out=delayed-.5*wet;history[i].append(wet+.5*out);wet=out
            expected.append(dry*.6+.4*wet)
        y=self.direct(x,.85,.6,.4)
        self.assertLess(max(abs(a-b) for a,b in zip(y,expected)),3e-6)
        self.assertEqual([self.lib.rv_valid(0,i) for i in range(6)],list(LENGTHS))
        self.assertTrue(self.lib.rv_guard(0))
    def test_decay_damping_and_feedback_extreme_are_observable(self):
        impulse=[1.]+[0.]*70000
        self.lib.rv_reset(0);short=self.direct(impulse,.2,0.)
        self.lib.rv_reset(0);long=self.direct(impulse,.9,0.)
        self.assertGreater(sum(v*v for v in long[20000:]),1000*sum(v*v for v in short[20000:]))
        self.lib.rv_reset(0);dark=self.direct(impulse,.9,1.)
        def brightness(y):
            y=y[10000:];return sum((a-b)**2 for a,b in zip(y[1:],y))/sum(v*v for v in y)
        self.assertLess(brightness(dark),.3*brightness(long))
        self.lib.rv_reset(0);tail=self.direct([1.]+[0.]*160000,.95,1.)
        self.assertLess(max(abs(v) for v in tail[-5000:]),.001)
        self.lib.rv_reset(0);driven=self.direct([16.]*30000,.95,0.)
        self.assertTrue(all(math.isfinite(v) and abs(v)<=16. for v in driven))
        self.assertTrue(self.lib.rv_guard(0))
    def test_logical_reset_poison_and_old_tail_never_return(self):
        c=self.lib;self.assertEqual(c.rv_word(0,5000),0x7fc00000)
        self.assertEqual(self.direct([0.]*12000),[0.]*12000)
        self.direct([.5]*20000,.95,1.);old=c.rv_word(0,5000);c.rv_reset(0)
        self.assertEqual(c.rv_word(0,5000),old)
        self.assertEqual([c.rv_valid(0,i) for i in range(6)],[0]*6)
        self.assertEqual([c.rv_damped(0,i) for i in range(4)],[0.]*4)
        self.assertEqual(self.direct([0.]*30000,.95,1.),[0.]*30000)
        self.assertTrue(c.rv_guard(0))
    def test_cold_resource_admission_metadata_and_rejections(self):
        c=self.lib;self.assertEqual(c.rv_bytes(),21232) # 21000 + appended RV-6/spring network state;self.assertEqual(self.configure(0,[8]),0)
        self.assertEqual(c.rv_bind(0),1);self.assertEqual(self.configure(0,[8]),1)
        self.assertEqual(c.rv_unbind(0),0);self.assertEqual(self.configure(0,[8,8]),0)
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[8,255,255,255,255,255])
        for p,default in enumerate((.75,.5,.25)):
            self.assertAlmostEqual(c.fx_target(0,0,p),default)
            # Native decay/damping/mix floats remain preset-compatible; the
            # human knobs are normalized and TONE reverses damping direction.
            bounds=((0.,.95,.75),(0.,1.,.5),(0.,1.,.25))[p]
            for field in range(3):self.assertEqual(c.rv_bound(p,field),f32(bounds[field]))
            self.assertEqual((c.rv_meta(p,0),c.rv_meta(p,1),c.rv_meta(p,3)),(0,100,1))
        for p,v in ((0,-.01),(0,.951),(1,1.01),(2,-.1),(0,float('nan'))):self.assertEqual(c.fx_set(0,0,p,v),0)
        self.process(0,[0.]);self.assertEqual(c.rv_bind(0),0)
        self.assertEqual(self.route(0,[8,8,255,255,255,255]),0);self.assertEqual(c.fx_phase(0),0)
        c.fx_init(0);self.assertEqual(self.configure(0,[8]),0) # init detaches memory
    def test_independent_chains_six_effects_chunk_inplace_and_faults(self):
        c=self.lib
        for chain in (0,1):
            c.rv_bind(chain);c.rv_bind_others(chain);self.assertEqual(self.configure(chain,[6,7,5,8,4,3]),1)
            for slot in range(6):c.fx_enable(chain,slot,1)
            c.fx_set(chain,2,0,20.);c.fx_set(chain,3,0,.9)
        x=[math.sin(i*.1)*.2 for i in range(40000)];x[14000]=float('nan');x[21000]=17.
        expected=self.process(0,x);other=[]
        for i in range(0,len(x),37):other+=self.process(1,x[i:i+37],True)
        self.assertEqual(other,expected);self.assertEqual(c.fx_faults(0),2)
        self.assertTrue(all(math.isfinite(v) for v in other))
        c.fx_set(0,3,0,.1);self.process(0,[.1]*3000)
        self.assertAlmostEqual(c.fx_target(1,3,0),.9,places=6)
        self.assertTrue(c.rv_guard(0) and c.rv_guard(1))
    def test_cut_and_keep_tail_contracts_and_reenable(self):
        c=self.lib
        for chain in (0,1):
            c.rv_bind(chain);self.assertEqual(self.configure(chain,[8]),1)
            if chain==0:self.assertEqual(c.rv_policy(chain,0,1),1)
            c.fx_set(chain,0,0,.9);c.fx_enable(chain,0,1);self.process(chain,[0.]*5000)
            self.process(chain,[1.]);c.fx_enable(chain,0,0);self.process(chain,[0.]*4000)
        self.assertEqual([c.rv_valid(0,i) for i in range(6)],[0]*6)
        self.assertEqual(self.process(0,[.1]*3000),[f32(.1)]*3000)
        self.assertEqual([c.rv_valid(0,i) for i in range(6)],[0]*6)
        c.fx_enable(0,0,1);self.assertEqual(self.process(0,[0.]*15000),[0.]*15000)
        c.fx_enable(1,0,1);retained=self.process(1,[0.]*15000)
        self.assertGreater(max(abs(v) for v in retained[3000:]),1e-4)
    def test_live_route_retention_type_defaults_and_tail_reset(self):
        c=self.lib;c.rv_bind(0);self.assertEqual(self.configure(0,[8]),1)
        c.fx_set(0,0,0,.9);c.fx_enable(0,0,1);self.process(0,[.2]*5000)
        self.assertEqual(self.route(0,[8,255,255,255,255,255],[5,4,3,2,1,0]),1)
        self.process(0,[0.]*256)
        self.assertAlmostEqual(c.fx_target(0,0,0),.9,places=6)
        self.assertEqual(self.process(0,[0.]*10000),[0.]*10000)
        self.assertEqual(self.route(0,[255,255,255,255,8,255]),1);self.process(0,[0.]*256)
        self.assertEqual(c.fx_enabled(0,4),0);self.assertEqual(c.fx_target(0,4,0),.75)
        c.fx_enable(0,4,1);self.assertEqual(self.process(0,[0.]*10000),[0.]*10000)
    def test_wav_reverb_tail_and_resource_parameter_validation(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            source=Path(folder)/'in.wav';out=Path(folder)/'out.wav'
            samples=[0]*6000+[12000]+[0]*99
            with wave.open(str(source),'wb') as w:w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
            run(source,out,effects=['reverb'],reverb=(.9,.5,1.),tail_seconds=.5)
            with wave.open(str(out),'rb') as w:
                self.assertEqual(w.getnframes(),len(samples)+22050);raw=w.readframes(w.getnframes());y=struct.unpack('<'+'h'*(len(raw)//2),raw)
            self.assertTrue(any(abs(v)>1 for v in y[len(samples):]))
            run(source,out,effects=['chorus','delay','reverb','ir'],tail_seconds=.1)
            for kwargs in ({'effects':['reverb','reverb']},{'effects':['reverb'],'reverb':(1.,.5,.25)},
                           {'effects':['reverb'],'reverb':(.7,float('inf'),.25)}):
                with self.assertRaises(ValueError):run(source,out,**kwargs)

if __name__=='__main__':unittest.main()
