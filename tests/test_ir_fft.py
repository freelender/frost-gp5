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
KEEP,CUT=0,1
F=ct.c_float

class IRFFTTests(unittest.TestCase):
    """31a partitioned FFT IR: equal to the direct form up to float rounding,
    with the direct-form history kept exactly in step."""
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/'+getattr(cls,'library_name','ir_fft_host')+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/ir_fft_host.c',includes=[ROOT/'tests/stubs'],flags=getattr(cls,'build_flags',()))))
        c=cls.c
        c.k_load.argtypes=[ct.POINTER(F),ct.c_uint]
        c.k_process.argtypes=[ct.POINTER(F),ct.POINTER(F),ct.POINTER(F),ct.c_uint,ct.c_uint,ct.c_uint]
        c.c_set.argtypes=[ct.c_uint,ct.c_uint,F]
        c.c_ir_values.argtypes=[ct.c_uint,ct.POINTER(F),ct.c_uint]
        c.c_block.argtypes=[ct.POINTER(F),ct.POINTER(F),ct.POINTER(F),ct.c_uint,ct.c_uint]
    # ---- kernel ----
    def kload(self,h):return self.c.k_load((F*len(h))(*h),len(h))
    def kproc(self,x,mode,inplace=0):
        n=len(x);a=(F*n)(*x);ya=(F*n)();yb=(F*n)()
        self.assertEqual(self.c.k_process(a,ya,yb,n,mode,inplace),1)
        return list(ya),list(yb)
    def test_partition_count_and_impulse(self):
        c=self.c;r=random.Random(31)
        for taps in (1,2,17,63,64,65,128,129,200,255,256):
            c.k_init(1);h=[r.uniform(-.5,.5)/taps for _ in range(taps)]
            self.assertEqual(self.kload(h),1)
            self.assertEqual(c.k_parts(),(taps+63)//64)
            y=[]
            for _ in range(5):
                a,b=self.kproc(([1.0] if not y else [0.0])+[0.0]*63,1);y+=a
            err=max(abs(p-q) for p,q in zip(y,h+[0.0]*(len(y)-taps)))
            self.assertLess(err,1e-7,taps)
            self.assertTrue(c.k_guard())
        c.k_init(1);self.assertEqual(self.kload([0.001]*257),1);self.assertEqual(c.k_parts(),0)
        c.k_init(1);self.assertEqual(self.kload([0.001]*512),1);self.assertEqual(c.k_parts(),0)
    def test_random_signal_mixed_paths_match_direct_form(self):
        c=self.c;r=random.Random(3101)
        for taps in (1,5,64,100,128,192,256):
            c.k_init(1);h=[r.uniform(-1,1)/taps for _ in range(taps)];self.assertEqual(self.kload(h),1)
            worst=0.0;ffts=0
            for step in range(120):
                if c.k_fill()==0 and r.random()<0.7:
                    x=[r.uniform(-1,1) for _ in range(64)];a,b=self.kproc(x,1,r.randint(0,1));ffts+=1
                else:
                    x=[r.uniform(-1,1) for _ in range(r.choice((1,7,31,64,65,64-c.k_fill(),64-c.k_fill())))];a,b=self.kproc(x,0)
                worst=max(worst,max(abs(p-q) for p,q in zip(a,b)))
                self.assertEqual(c.k_history_equal(),1,(taps,step))
                if step==60:c.k_clear()
            self.assertGreater(ffts,40);self.assertLess(worst,4e-7,taps);self.assertTrue(c.k_guard())
    def test_inplace_is_bit_identical_and_unbound_is_direct(self):
        c=self.c;r=random.Random(5);h=[r.uniform(-.3,.3)/50 for _ in range(150)]
        x=[r.uniform(-1,1) for _ in range(64*6)]
        outs=[]
        for inplace in (0,1):
            c.k_init(1);self.kload(h);y=[]
            for i in range(0,len(x),64):y+=self.kproc(x[i:i+64],1,inplace)[0]
            outs.append(y)
        self.assertEqual(outs[0],outs[1])
        c.k_init(0);self.kload(h);self.assertEqual(c.k_parts(),0)
        a,b=self.kproc(x,0);self.assertEqual(a,b)   # no workspace: unchanged direct form
    def test_accuracy_vs_double_reference(self):
        c=self.c;r=random.Random(77);taps=256
        h=[F(r.gauss(0,1)*math.exp(-i/40)).value for i in range(taps)];s=sum(abs(v) for v in h);h=[F(v/s).value for v in h]
        c.k_init(1);self.kload(h)
        x=[F(0.5*math.sin(0.05*i)+0.3*r.uniform(-1,1)).value for i in range(64*40)]
        y=[];yd=[]
        for i in range(0,len(x),64):a,b=self.kproc(x[i:i+64],1);y+=a;yd+=b
        exact=[math.fsum(h[k]*x[n-k] for k in range(min(n+1,taps))) for n in range(len(x))]
        ef=math.sqrt(sum((p-q)**2 for p,q in zip(y,exact))/len(x));ed=math.sqrt(sum((p-q)**2 for p,q in zip(yd,exact))/len(x))
        rms=math.sqrt(sum(q*q for q in exact)/len(x))
        # Both float32 forms sit far below audibility; FFT error stays within 4x direct.
        self.assertLess(20*math.log10(ef/rms),-125);self.assertLess(ef,4*ed+1e-12)
    # ---- chain ----
    def route(self,types,order=(0,1,2,3,4,5),pre=6):
        r=self.c.c_route((ct.c_uint*6)(*types),(ct.c_uint*6)(*order),pre);self.assertNotEqual(r,99);return r
    def signal(self,rng,n,t):
        return [0.4*math.sin(0.031*(t+i))+0.2*math.sin(0.27*(t+i))+0.05*(rng.random()-0.5) for i in range(n)]
    def block(self,x,inplace=0,tol=5e-5):
        n=len(x);a=(F*n)(*x);ya=(F*n)();yb=(F*n)()
        self.assertEqual(self.c.c_block(a,ya,yb,n,inplace),1,'transition/fault state diverged')
        err=max(abs(p-q) for p,q in zip(ya,yb));self.assertLess(err,tol);return err
    def test_random_routes_policies_and_chunks_match_direct_chain(self):
        rng=random.Random(20261115);c=self.c
        # No GATE after an IR: a threshold decision may legitimately flip on rounding.
        routes=[(GAIN,GATE,EQ,IR,EMPTY,EMPTY),(GATE,OD,EQ,IR,DELAY,REVERB),(GAIN,OD,CHORUS,IR,FLANGER,EQ),
                (REVERB,DELAY,OD,IR,IR,EQ),(CHORUS,EQ,GAIN,OD,IR,DELAY),(IR,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY)]
        fft_blocks=0
        for trial in range(6):
            c.c_init(rng.choice((1,2)));t=0
            for step in range(50):
                choice=rng.random()
                if choice<0.15:self.route(list(rng.choice(routes)),list(range(6)),6)
                elif choice<0.45:
                    slot=rng.randrange(6)
                    if c.c_type(slot)==IR:c.c_set(slot,0,rng.uniform(0.2,1.5))
                elif choice<0.75:c.c_enable(rng.randrange(6),rng.randint(0,1))
                elif choice<0.85:c.c_policy(rng.randrange(6),rng.randint(0,1))
                else:
                    slot=rng.randrange(6)
                    if c.c_type(slot)==IR:c.c_ir(slot,rng.choice((1,16,64,128,200,256,300)))
                for _ in range(rng.randint(1,6)):
                    n=rng.choice((64,64,64,64,17,1,37,128))
                    fft_blocks+=n==64 and any(c.c_fft_slot(s) for s in range(6))
                    self.block(self.signal(rng,n,t),rng.randint(0,1));t+=n
            self.assertEqual(c.c_faults(),0)
        self.assertGreater(fft_blocks,300)
    def test_cut_policy_footswitch_toggles_in_64_blocks(self):
        rng=random.Random(9);c=self.c;c.c_init(1)
        self.route((GAIN,EQ,IR,EMPTY,EMPTY,EMPTY));t=0
        while c.c_transition():self.block(self.signal(rng,64,t));t+=64
        c.c_ir(2,256);c.c_enable(2,1);self.assertEqual(c.c_policy(2,CUT),1)
        while c.c_transition():self.block(self.signal(rng,64,t));t+=64
        for i in range(40):
            if i%5==0:c.c_enable(2,(i//5)%2)
            self.block(self.signal(rng,64,t),i%2);t+=64
            self.assertEqual(c.c_fft_slot(2),1)
        self.assertEqual(c.c_faults(),0)
    def test_route_fades_run_stage_major_with_fft(self):
        rng=random.Random(12);c=self.c;c.c_init(1)
        self.route((GAIN,OD,EQ,IR,DELAY,EMPTY));t=0
        fades=0
        for i in range(30):
            if not c.c_transition() and i%6==0:
                c.c_ir(3,rng.choice((128,256)));c.c_enable(3,1);c.c_enable(4,1)
                self.route((GAIN,OD,EQ,IR,DELAY,rng.choice((EMPTY,REVERB,CHORUS))))
            fades+=bool(c.c_transition())
            self.block(self.signal(rng,64,t));t+=64
        self.assertGreater(fades,8);self.assertEqual(c.c_faults(),0)
    def test_workspace_size_fits_large_margin(self):
        # 30b left 6,228 B of LARGE; one workspace must leave >= 512 B.
        self.assertLessEqual(self.c.c_bytes(),6228-512)

class CabQ15Tests(unittest.TestCase):
    """31a: the 256-tap baked cabinet is stored as q15 plus one scale."""
    def test_generator_precision_and_exact_loader(self):
        import cab_ir
        r=random.Random(4);h=[r.gauss(0,1)*math.exp(-i/50) for i in range(256)];l1=sum(abs(v) for v in h)
        h=[F(v/l1).value for v in h];q,scale=cab_ir.q15(h)
        self.assertEqual(max(abs(v) for v in q),32767)
        self.assertLessEqual(max(abs(a-b*scale) for a,b in zip(h,q)),scale/2*1.0001)
        c=ct.CDLL(str(build(ROOT/('build/ir_fft_q15_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/ir_fft_host.c',includes=[ROOT/'tests/stubs'])))
        c.k_load_q15.argtypes=[ct.POINTER(ct.c_int16),ct.c_uint,F];c.k_coefficient.restype=F;c.k_coefficient.argtypes=[ct.c_uint]
        c.k_init(1);self.assertEqual(c.k_load_q15((ct.c_int16*256)(*q),256,scale),1);self.assertEqual(c.k_parts(),4)
        self.assertEqual([c.k_coefficient(i) for i in range(258)],[F(v*scale).value for v in q]+[0.0,0.0])
        for bad in (0.0,-scale,float('nan'),8.0/32768*1.01):self.assertEqual(c.k_load_q15((ct.c_int16*256)(*q),256,bad),0)
        self.assertEqual(c.k_load_q15((ct.c_int16*256)(*([32767]*256)),256,8.0/32768),0)   # L1 > 8
        self.assertEqual([c.k_coefficient(i) for i in range(4)],[F(v*scale).value for v in q[:4]])  # atomic
    def test_line_firmware_boots_q15_cabinet_on_fft(self):
        import cab_ir,tempfile
        r=random.Random(8);h=[r.gauss(0,1)*math.exp(-i/50) for i in range(256)];l1=sum(abs(v) for v in h)
        h=[F(v/l1).value for v in h]
        info=dict(source='t.wav',source_sha256='0'*64,source_rate=44100,trimmed_leading_samples=0,kept_taps=256,fade_taps=24)
        with tempfile.TemporaryDirectory() as d:
            inc=Path(d)/'tests'/'fixtures';inc.mkdir(parents=True)
            (inc/'diagnostic_ir.h').write_text(cab_ir.header(h,info,'q15'))
            lib=ct.CDLL(str(build(ROOT/('build/line_q15_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/line_host.c',
                                  includes=[Path(d),ROOT/'tests/stubs'])))
        lib.line_block.argtypes=[ct.c_int32,ct.c_uint32,ct.c_uint];lib.line_ir_tap.restype=F;lib.line_sample.restype=ct.c_int32
        lib.line_init()
        q,scale=cab_ir.q15(h)
        self.assertEqual((lib.line_ir_len(),lib.line_ir_fft_parts()),(256,4))
        self.assertEqual(lib.line_ir_tap(0),F(q[0]*scale).value)
        audio=False
        for i in range(300):
            self.assertEqual(lib.line_block(int(268435456*math.sin(0.07*i)),65536,0),1)
            audio|=any(lib.line_sample(k) for k in range(64))
        self.assertTrue(audio);self.assertEqual(lib.line_value(1),0)   # no latch: 256 taps admitted on FFT
        self.assertEqual(lib.line_ir_taps(256),1);self.assertEqual(lib.line_ir_len(),256)

if __name__=='__main__':unittest.main()
