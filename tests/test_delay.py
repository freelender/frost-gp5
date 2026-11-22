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
F=lambda v:ct.c_float(v).value
def half(v):
    """31b reference: binary16 round-to-nearest-even; subnormal range truncated."""
    a=abs(v)
    if a<2.0**-14:
        return math.copysign(int(a*2.0**24)*2.0**-24,v)
    return struct.unpack('<e',struct.pack('<e',v))[0]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from chain_wav import run

class DelayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/delay.dll' if os.name=='nt' else 'build/delay.so'),ROOT/'tests/delay_host.c')))
        c=cls.lib;c.dl_sample.argtypes=[ct.c_uint]+[ct.c_float]*4;c.dl_sample.restype=c.dl_distance.restype=ct.c_float
        c.dl_half_store.argtypes=[ct.c_float];c.dl_half_load.restype=ct.c_float
        c.fx_configure.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_request.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        c.fx_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float];c.fx_target.restype=ct.c_float
        c.fx_process.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.fx_inplace.argtypes=[ct.c_uint,ct.POINTER(ct.c_float),ct.c_uint]
    def setUp(self):
        for c in (0,1):self.lib.fx_init(c);self.lib.dl_poison(c);self.lib.dl_reset(c)
    def array(self,v):return (ct.c_uint*len(v))(*v)
    def configure(self,c,types):return self.lib.fx_configure(c,self.array(types),len(types))
    def process(self,c,x,inplace=False):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        if inplace:self.lib.fx_inplace(c,a,len(x));return list(a)
        self.lib.fx_process(c,a,b,len(x));return list(b)
    def direct(self,c,x,time=100.,feedback=0.,mix=1.):
        return [self.lib.dl_sample(c,v,time,feedback,mix) for v in x]
    def test_impulse_integer_fractional_min_max_and_feedback_echoes(self):
        # Integer delay D4410: independently known impulse train amplitudes.
        y=self.direct(0,[1.]+[0.]*18000,100.,.5)
        for i in range(len(y)):
            expected=.5**(i//4410-1) if i and i%4410==0 else 0.
            self.assertAlmostEqual(y[i],expected,places=6)
        # Fractional minimum D44.1 splits one impulse between adjacent frames.
        self.lib.dl_reset(0);y=self.direct(0,[1.]+[0.]*100,1.)
        self.assertAlmostEqual(y[44],.9,places=5);self.assertAlmostEqual(y[45],.1,places=5)
        self.lib.dl_reset(0);y=self.direct(0,[1.]+[0.]*22051,500.)
        self.assertEqual(y[22050],1.);self.assertEqual(sum(abs(v) for v in y),1.)
    def test_binary16_storage_conversion(self):
        c=self.lib;r=__import__('random').Random(31)
        values=[0.,-0.,1.,-1.,16.,-16.,2.**-14,2.**-14*(1-2.**-24),2.**-24,2.**-25,3*2.**-26,6e-8,1e-30,
                65504.,1+2.**-11,1+3*2.**-11,1+2.**-11+2.**-23]
        values+=[F(r.uniform(-16,16)) for _ in range(20000)]+[F(math.ldexp(r.uniform(-1,1),r.randint(-40,4))) for _ in range(20000)]
        for v in values:
            v=F(v);h=c.dl_half_store(v)
            self.assertEqual(c.dl_half_load(h),half(v),v)
            self.assertEqual(h,struct.unpack('<H',struct.pack('<e',half(v)))[0],v)
        # every stored word decodes exactly (round trip is the identity)
        for h in list(range(0,0x7c00,7))+list(range(0x8000,0xfc00,7)):
            self.assertEqual(c.dl_half_store(c.dl_half_load(h)),h)
        # relative error bound in the normal range
        for v in values:
            v=F(v)
            if 2.**-14<=abs(v)<=16:self.assertLessEqual(abs(half(v)-v),abs(v)*2.**-11)
    def test_double_reference_fractional_feedback_through_ring_wraps(self):
        # Unbounded-history model, float32 arithmetic and binary16 storage, is
        # independent of circular indexing.
        time=F(7.5);d=F(F(44.1)*time);fb=F(.73);mix=F(.4)
        integer=int(d);fraction=F(d-integer);history=[];expected=[];unquantized=[];plain=[]
        x=[F(.2*math.sin(i*.17)) for i in range(70000)]
        for i,v in enumerate(x):
            near=history[i-integer] if i>=integer else 0.
            far=history[i-integer-1] if i>integer else 0.
            echo=F(near+F(fraction*F(far-near)))
            history.append(half(F(v+F(fb*echo))));expected.append(F(v+F(mix*F(echo-v))))
            near=plain[i-integer] if i>=integer else 0.
            far=plain[i-integer-1] if i>integer else 0.
            echo=(1-fraction)*near+fraction*far
            plain.append(v+.73*echo);unquantized.append(.4*echo+.6*v)
        actual=self.direct(0,x,time,.73,.4)
        self.assertLess(max(abs(a-b) for a,b in zip(actual,expected)),2e-6)
        # storage cost against the float/double delay: about -70 dB of the signal
        err=max(abs(a-b) for a,b in zip(actual,unquantized));peak=max(abs(v) for v in unquantized)
        self.assertLess(20*math.log10(err/peak),-60.)
        self.assertTrue(self.lib.dl_guard(0));self.assertEqual(self.lib.dl_valid(0),22052)
    def test_logical_reset_never_reads_poison_or_old_tail(self):
        c=self.lib
        self.assertEqual(c.dl_word(0,10000),0x7e00) # reset did not clear the ring
        self.assertEqual(self.direct(0,[0.]*23000,500.,.95),[0.]*23000)
        self.direct(0,[.8]*50000,1.,.95);old=c.dl_word(0,10000);c.dl_reset(0)
        self.assertEqual(c.dl_word(0,10000),old) # logical invalidation, not a bulk fill
        self.assertEqual(c.dl_valid(0),0);self.assertEqual(c.dl_position(0),0)
        self.assertEqual(self.direct(0,[0.]*50000,1.,.95),[0.]*50000)
        self.assertTrue(c.dl_guard(0))
    def test_bounded_feedback_and_read_head_slew(self):
        c=self.lib
        y=self.direct(0,[16.]*60000,1.,.95)
        self.assertTrue(all(math.isfinite(v) and abs(v)<=16.00001 for v in y))
        previous=c.dl_distance(0)
        for time in ([500.]*1000+[1.]*1000):
            c.dl_sample(0,0.,time,.95,1.);current=c.dl_distance(0)
            self.assertLessEqual(abs(current-previous),.25001);previous=current
        c.dl_reset(0);y=self.direct(0,[1.]+[0.]*100000,1.,.95)
        self.assertLess(max(abs(v) for v in y[-10000:]),1e-20)
        self.assertTrue(c.dl_guard(0))
    def test_resource_admission_is_atomic_and_binding_is_cold(self):
        c=self.lib;self.assertEqual(c.dl_bytes(),70812)
        self.assertEqual(self.configure(0,[5]),0) # unattached caller-owned memory
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[0,1,2,3,255,255])
        self.assertEqual(c.dl_bind(0),1);self.assertEqual(self.configure(0,[5]),1)
        self.assertEqual(c.dl_unbind(0),0) # cannot detach active workspace
        self.assertEqual(self.configure(0,[5,5]),0)
        self.assertEqual([c.fx_type(0,i) for i in range(6)],[5,255,255,255,255,255])
        for p,v in enumerate((250.,.35,.3)):self.assertAlmostEqual(c.fx_target(0,0,p),v,places=6)
        for p,v in ((0,0.),(0,501.),(1,.951),(2,1.01),(1,float('inf'))):self.assertEqual(c.fx_set(0,0,p,v),0)
        self.process(0,[0.]);self.assertEqual(c.dl_bind(0),0)
        types=[5,5,255,255,255,255]
        self.assertEqual(c.fx_request(0,self.array(types),self.array(list(range(6))),3),0)
        self.assertEqual(c.fx_phase(0),0)
    def test_chain_isolation_chunk_inplace_and_nonfinite_recovery(self):
        c=self.lib
        for chain in (0,1):
            c.dl_bind(chain);self.assertEqual(self.configure(chain,[4,5,2,3]),1)
            for slot in range(4):c.fx_enable(chain,slot,1)
            c.fx_set(chain,1,0,7.5);c.fx_set(chain,1,1,.8)
        x=[math.sin(i*.11)*.2 for i in range(50000)];x[20000]=float('nan');x[30000]=17.
        expected=self.process(0,x);actual=[]
        for i in range(0,len(x),37):actual+=self.process(1,x[i:i+37],True)
        self.assertEqual(actual,expected);self.assertEqual(c.fx_faults(0),2)
        self.assertTrue(all(math.isfinite(v) for v in actual));self.assertEqual(actual[20000],0.)
        c.fx_set(0,1,0,100.);self.process(0,[0.]*2000)
        self.assertEqual(c.fx_target(1,1,0),7.5);self.assertTrue(c.dl_guard(0) and c.dl_guard(1))
    def test_cut_suspend_and_route_changes_clear_tails_without_bulk_clear(self):
        c=self.lib;c.dl_bind(0);self.assertEqual(self.configure(0,[5]),1)
        self.assertEqual(c.dl_policy(0,0,1),1);c.fx_enable(0,0,1)
        self.process(0,[.2]*24000);self.assertGreater(c.dl_valid(0),0)
        c.fx_enable(0,0,0);y=self.process(0,[.1]*128)
        self.assertAlmostEqual(y[-1],.1,places=7);self.assertEqual(c.dl_valid(0),0)
        self.process(0,[.3]*1000);self.assertEqual(c.dl_valid(0),0)
        c.fx_enable(0,0,1);self.assertEqual(self.process(0,[0.]*15000),[0.]*15000)
        self.process(0,[.2]*1000)
        types=[0,255,255,255,5,255]
        self.assertEqual(c.fx_request(0,self.array(types),self.array(list(range(6))),3),1)
        self.process(0,[0.]*256)
        self.assertEqual(c.fx_type(0,4),5);self.assertEqual(c.fx_enabled(0,4),0)
        self.assertEqual(c.fx_target(0,4,0),250.)
        c.fx_enable(0,4,1);self.assertEqual(self.process(0,[0.]*25000),[0.]*25000)
    def test_keep_history_bypass_hides_but_retains_feedback_tail(self):
        c=self.lib;c.dl_bind(0);self.assertEqual(self.configure(0,[5]),1)
        c.fx_enable(0,0,1);self.process(0,[0.]*5000)
        self.process(0,[1.]);c.fx_enable(0,0,0)
        muted=self.process(0,[0.]*15000)
        self.assertLess(max(abs(v) for v in muted),1e-15)
        self.assertGreater(c.dl_valid(0),0)
        c.fx_enable(0,0,1);returned=self.process(0,[0.]*10000)
        # Second repeat survives while the effect is hidden, unlike CUT.
        self.assertGreater(max(returned),.1);self.assertLess(max(returned),.106)
    def test_wav_delay_tail_length_echoes_and_invalid_inputs(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            source=Path(folder)/'in.wav';out=Path(folder)/'out.wav'
            samples=[0]*35000+[12000]+[0]*1000
            with wave.open(str(source),'wb') as w:w.setparams((1,2,44100,0,'NONE',''));w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
            run(source,out,effects=['delay'],delay=(100.,.5,1.),tail_seconds=.3)
            with wave.open(str(out),'rb') as w:
                self.assertEqual(w.getnframes(),len(samples)+13230);raw=w.readframes(w.getnframes());y=struct.unpack('<'+'h'*(len(raw)//2),raw)
            # Float target smoothing leaves a sub-sample offset; interpolation
            # spreads each echo but preserves its summed amplitude locally.
            self.assertLessEqual(abs(sum(y[39409:39413])-12000),2)
            self.assertLessEqual(abs(sum(y[43819:43824])-6000),3)
            self.assertLess(sum(abs(v) for v in y[35001:39000]),2)
            for kwargs in ({'effects':['delay','delay']},{'effects':['delay'],'delay':(501.,.5,.5)},
                           {'tail_seconds':float('nan')},{'tail_seconds':31.}):
                with self.assertRaises(ValueError):run(source,out,**kwargs)

if __name__=='__main__':unittest.main()
