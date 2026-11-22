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

class ModulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/modulation.dll' if os.name=='nt' else 'build/modulation.so'),ROOT/'tests/modulation_host.c')))
        c=cls.lib;c.mo_sample.argtypes=[ct.c_uint]*3+[ct.c_float]*4
        for n in ('mo_sample','mo_phase','mo_distance','mo_bound','fx_target'):getattr(c,n).restype=ct.c_float
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
    def setUp(self):
        for c in (0,1):
            self.lib.fx_init(c);self.lib.mo_poison(c)
            for w in (0,1):self.lib.mo_reset(c,w)
    def array(self,v):return (ct.c_uint*len(v))(*v)
    def configure(self,c,types):return self.lib.fx_configure(c,self.array(types),len(types))
    def route(self,c,types,order=None):return self.lib.fx_request(c,self.array(types),self.array(order or list(range(6))),3)
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        self.lib.fx_process(c,a,b,len(x));return list(b)
    def direct(self,x,type=6,rate=.8,depth=.5,value=.3,c=0,w=0):
        return [self.lib.mo_sample(c,w,type,v,rate,depth,value) for v in x]
    def test_zero_depth_analytic_impulse_and_signed_feedback(self):
        y=self.direct([1.]+[0.]*1200,depth=0.,value=1.)
        self.assertEqual(y[661:663],[.5,.5]);self.assertEqual(sum(abs(v) for v in y),1.)
        for feedback in (-.5,.5):
            self.lib.mo_reset(0,0);y=self.direct([1.]+[0.]*800,7,depth=0.,value=feedback)
            self.assertEqual(y[0],.5)
            for window,expected in [((132,134),.5),((264,267),.5*feedback),((396,400),.5*feedback**2)]:
                self.assertAlmostEqual(sum(y[window[0]:window[1]]),expected,places=6)
    def test_double_history_reference_through_lfo_cycles_and_ring_wraps(self):
        c=self.lib;x=[.2*math.sin(i*.031) for i in range(30000)]
        for type,rate,depth,value in ((6,3.7,.9,.4),(7,5.,1.,-.8)):
            c.mo_reset(0,0);history=[];phase=distance=0.;expected=[]
            step=f32(f32(rate)*f32(1/44100));depth=f32(depth);value=f32(value)
            for i,v in enumerate(x):
                triangle=4*phase-1 if phase<.5 else 3-4*phase
                lfo=(3*triangle-triangle**3)/2
                desired=((3+2.5*depth*lfo) if type==7 else (15+10*depth*lfo))*f32(44.1)
                distance=desired if not distance else max(distance-.5,min(distance+.5,desired))
                d=int(distance);fraction=distance-d
                near=history[i-d] if i>=d else 0.;far=history[i-d-1] if i>d else 0.
                echo=near*(1-fraction)+far*fraction
                history.append(max(-16.,min(16.,f32(v)+(value*echo if type==7 else 0.))))
                mix=.5 if type==7 else value;expected.append(f32(v)*(1-mix)+mix*echo)
                phase=f32(phase+step)
                if phase>=1:phase=f32(phase-1)
            actual=self.direct(x,type,rate,depth,value)
            self.assertLess(max(abs(a-b) for a,b in zip(actual,expected)),2e-5)
            self.assertTrue(c.mo_guard(0));self.assertEqual(c.mo_valid(0,0),1152)
    def test_comb_cancellation_and_chorus_modulation_sidebands(self):
        # Depth0 produces a fixed delay: half-period dry/wet cancellation.
        for type,delay in ((6,.015),(7,.003)):
            self.lib.mo_reset(0,0);frequency=1/(2*delay)
            x=[.1*math.sin(2*math.pi*frequency*i/44100) for i in range(10000)]
            y=self.direct(x,type,depth=0.,value=.5 if type==6 else 0.)
            self.assertLess(max(abs(v) for v in y[3000:]),1e-4)
        n=22050;x=[.1*math.sin(2*math.pi*1000*i/44100) for i in range(n*3)]
        def amp(y,hz):return abs(sum(v*complex(math.cos(2*math.pi*hz*i/44100),-math.sin(2*math.pi*hz*i/44100)) for i,v in enumerate(y)))*2/len(y)
        self.lib.mo_reset(0,0);plain=self.direct(x,rate=2.,depth=0.,value=1.)[-n:]
        self.lib.mo_reset(0,0);mod=self.direct(x,rate=2.,depth=.1,value=1.)[-n:]
        self.assertLess(amp(plain,1002),1e-5);self.assertGreater(amp(mod,1002),.002)
    def test_phase_bounds_slew_feedback_rails_and_logical_reset(self):
        c=self.lib;self.assertEqual(c.mo_bytes(),4624)
        self.assertEqual(c.mo_word(0,0,1000),0x7fc00000)
        self.assertEqual(self.direct([0.]*3000,7,value=-.9),[0.]*3000)
        for type in (6,7):
            c.mo_reset(0,0);previous=None
            for i in range(15000):
                value=c.mo_sample(0,0,type,16. if i<10000 else 0.,5. if i%2 else .05,1. if i<7000 else 0.,.9)
                distance=c.mo_distance(0,0)
                self.assertTrue(0<=c.mo_phase(0,0)<1)
                self.assertTrue(22.04<=distance<=1102.51)
                if previous is not None:self.assertLessEqual(abs(distance-previous),.50001)
                previous=distance;self.assertTrue(math.isfinite(value) and abs(value)<=16.00001)
            old=c.mo_word(0,0,1000);c.mo_reset(0,0)
            self.assertEqual(c.mo_word(0,0,1000),old);self.assertEqual(c.mo_phase(0,0),0.)
            self.assertEqual(self.direct([0.]*10000,type,value=-.9 if type==7 else .3),[0.]*10000)
        self.assertTrue(c.mo_guard(0))
    def test_atomic_pool_admission_defaults_and_registry_metadata(self):
        c=self.lib;self.assertEqual(self.configure(0,[6]),0);self.assertEqual(c.mo_bind(0),1)
        self.assertEqual(self.configure(0,[6,7]),1);self.assertEqual(c.mo_unbind(0),0)
        self.assertNotEqual(c.mo_identity(0,0),c.mo_identity(0,1))
        self.assertEqual(self.configure(0,[6,7,6]),0)
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[6,7,255,255,255,255])
        for type in (6,7):
            self.assertEqual(c.fx_workspace_bytes(type),4624)
            for p in range(3):
                # Pedal knobs use human ranges; native/wire floats retain the
                # original rate/depth/feedback contract independently.
                bounds=((.05,5.,.8 if type==6 else .3),(0.,1.,.5),
                        (0.,1.,.3) if type==6 else (-.9,.9,.5))[p]
                for f in range(3):self.assertEqual(c.mo_bound(type,p,f),f32(bounds[f]))
                self.assertEqual(c.mo_meta(type,p,3),100 if p==0 else 1)
        for slot,p,value in ((0,0,.049),(1,0,5.1),(0,1,1.1),(1,2,-.91),(1,2,float('nan'))):
            self.assertEqual(c.fx_set(0,slot,p,value),0)
        self.process(0,[0.]);self.assertEqual(c.mo_bind(0),0)
        self.assertEqual(self.route(0,[6,6,7,255,255,255]),0);self.assertEqual(c.fx_phase(0),0)
    def test_flanger_feedback_extremes_decay_and_remain_bounded(self):
        for feedback in (-.9,.9):
            self.lib.mo_reset(0,0)
            driven=self.direct([16.]*15000,7,5.,1.,feedback)
            self.assertTrue(all(math.isfinite(v) and abs(v)<=16.00001 for v in driven))
            self.lib.mo_reset(0,0)
            tail=self.direct([1.]+[0.]*70000,7,5.,1.,feedback)
            self.assertLess(max(abs(v) for v in tail[-5000:]),1e-8)
            self.assertTrue(self.lib.mo_guard(0))
    def test_six_effect_chain_chunk_inplace_faults_and_isolation(self):
        c=self.lib
        for chain in (0,1):
            c.mo_bind(chain);c.mo_bind_delay(chain)
            self.assertEqual(self.configure(chain,[4,6,7,5,2,3]),1)
            for slot in range(6):c.fx_enable(chain,slot,1)
            c.fx_set(chain,2,2,-.8);c.fx_set(chain,3,0,20.)
        x=[math.sin(i*.2)*.2 for i in range(40000)];x[10000]=float('nan');x[17000]=17.
        expected=self.process(0,x);other=[]
        for i in range(0,len(x),37):other+=self.process(1,x[i:i+37],True)
        self.assertEqual(other,expected);self.assertEqual(c.fx_faults(0),2)
        self.assertTrue(all(math.isfinite(v) for v in other));self.assertTrue(c.mo_guard(0) and c.mo_guard(1))
        c.fx_set(0,1,0,5.);self.process(0,[.1]*2000)
        self.assertAlmostEqual(c.fx_target(1,1,0),.8,places=6)
    def test_duplicate_instances_and_live_workspace_reassignment(self):
        c=self.lib;c.mo_bind(0);self.assertEqual(self.configure(0,[6,6]),1)
        for slot in (0,1):c.fx_enable(0,slot,1)
        c.fx_set(0,1,0,3.);self.process(0,[.1]*3000)
        a,b=c.mo_identity(0,0),c.mo_identity(0,1);self.assertNotEqual(a,b)
        self.assertNotEqual(c.mo_phase(0,a),c.mo_phase(0,b))
        # New earlier flanger must not steal the unchanged slot1 workspace.
        self.assertEqual(self.route(0,[7,6,255,255,255,255]),1);self.process(0,[0.]*256)
        self.assertEqual(c.mo_identity(0,1),b);self.assertNotEqual(c.mo_identity(0,0),b)
        self.assertEqual(c.fx_enabled(0,0),0);self.assertAlmostEqual(c.fx_target(0,1,0),3.)
        # Release slot0 and acquire a later slot; preserve unchanged identity.
        self.assertEqual(self.route(0,[255,6,255,7,255,255]),1);self.process(0,[0.]*256)
        self.assertEqual(c.mo_identity(0,1),b);self.assertNotEqual(c.mo_identity(0,3),b)
        self.assertTrue(c.mo_guard(0))
    def test_cut_suspends_phase_and_keep_advances_it(self):
        c=self.lib
        for chain in (0,1):
            c.mo_bind(chain);self.assertEqual(self.configure(chain,[6,7]),1)
            for slot in (0,1):
                if chain==0:self.assertEqual(c.mo_policy(chain,slot,1),1)
                c.fx_enable(chain,slot,1)
            self.process(chain,[.2]*3000)
            for slot in (0,1):c.fx_enable(chain,slot,0)
            self.process(chain,[.1]*128)
        self.assertEqual([c.mo_valid(0,w) for w in (0,1)],[0,0])
        self.assertEqual(self.process(0,[.1]*3000),[f32(.1)]*3000)
        self.assertEqual([c.mo_phase(0,w) for w in (0,1)],[0.,0.])
        phases=[c.mo_phase(1,w) for w in (0,1)];self.process(1,[0.]*3000)
        self.assertNotEqual(phases,[c.mo_phase(1,w) for w in (0,1)])
        for slot in (0,1):c.fx_enable(0,slot,1)
        self.assertEqual(self.process(0,[0.]*3000),[0.]*3000)
    def test_wav_combined_and_repeated_routes_tail_and_rejections(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            source=Path(folder)/'in.wav';out=Path(folder)/'out.wav';dry=Path(folder)/'dry.wav'
            samples=[round(math.sin(i*.13)*10000) for i in range(8000)]
            with wave.open(str(source),'wb') as w:w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
            run(source,dry,effects=[])
            for route in (['chorus','flanger','delay','ir'],['chorus','chorus'],['flanger','flanger']):
                run(source,out,effects=route,flanger=(2.,1.,-.8),tail_seconds=.1)
                with wave.open(str(out),'rb') as w:self.assertEqual(w.getnframes(),12410)
                self.assertNotEqual(out.read_bytes(),dry.read_bytes())
            for kwargs in ({'effects':['chorus','flanger','chorus']},{'effects':['flanger'],'flanger':(.3,.5,-.91)},
                           {'effects':['chorus'],'chorus':(0.,.5,.3)}):
                with self.assertRaises(ValueError):run(source,out,**kwargs)

if __name__=='__main__':unittest.main()
