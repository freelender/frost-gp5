"""Pitch-shifting primitives (effects/common/pitch_shift*.h) and the OCTAVE
kernel built on them: correlation and interpolation arithmetic, exact pitch at
every ratio, bounded head distance (latency and ring limits), pick-attack
detection, unused voices, memory layout. Host checks of the kernels; the
offline comparison against the previous algorithms is tools/pitch_harness.py,
listening and the slot-load meter on the pedal decide."""
import ctypes as ct,math,os,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
FS=44100.0

def tone(freqs,n,amp=0.3):
    return [sum(amp*math.sin(2*math.pi*f*i/FS+0.7*k) for k,f in enumerate(freqs)) for i in range(n)]
def harmonic(f0,n,amp=0.25,h=6):
    return [amp*sum(math.sin(2*math.pi*f0*k*i/FS+0.3*k)/k for k in range(1,h+1)) for i in range(n)]
def pitch_cents(y,want):
    """Strongest DFT peak within +/-100 cents of want (0.5-cent steps), Hann window."""
    n=len(y);w=[(.5-.5*math.cos(2*math.pi*i/n))*y[i] for i in range(n)];best=(-1,0)
    for c in range(-200,201):
        x=2*math.pi*want*2**(c/2400.)/FS;cr,ci=math.cos(x),math.sin(x);pr,pi_,re,im=1.,0.,0.,0.
        for a in w:re+=a*pr;im-=a*pi_;pr,pi_=pr*cr-pi_*ci,pr*ci+pi_*cr
        p=re*re+im*im
        if p>best[0]:best=(p,c/2.)
    return best[1]

class PitchShiftTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/pitch_shift_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/pitch_shift_host.c')))
        for f in ('ps_rsqrt','ps_l2f','ps_ncc','ps_read_after','ps_dmin','ps_dmax','ps_octave_up_distance','ps_octave_sub_distance'):getattr(c,f).restype=ct.c_float
        c.ps_rsqrt.argtypes=[ct.c_float];c.ps_l2f.argtypes=[ct.c_int64]
        c.ps_ncc.argtypes=[ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint]
        c.ps_read_after.argtypes=[ct.c_void_p,ct.c_uint,ct.c_float]
        c.ps_run.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_float,ct.c_float,ct.c_float,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        c.ps_octave.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint,ct.c_float,ct.c_float,ct.c_float]
    def run_splicer(self,x,ratio,**k):
        cfg=dict(dmin=48.,dmax=1600.,jmin=64,jmax=1400,every=2,fade_max=512,start=None)
        cfg.update(k)
        if cfg['start'] is None:cfg['start']=cfg['dmin'] if ratio<1 else cfg['dmin']+cfg['fade_max']+600.
        n=len(x);a=(ct.c_float*n)(*x);y=(ct.c_float*n)()
        self.c.ps_run(a,y,n,ratio,cfg['dmin'],cfg['dmax'],cfg['jmin'],cfg['jmax'],cfg['every'],cfg['fade_max'],cfg['start'])
        return list(y)
    def octave(self,x,dry,sub,up):
        n=len(x);a=(ct.c_float*n)(*x);y=(ct.c_float*n)();self.c.ps_octave(a,y,n,dry,sub,up);return list(y)

    def test_history_fits_the_modulation_workspace_and_slot(self):
        self.assertEqual(self.c.ps_history_bytes(),self.c.ps_modulation_bytes())
        self.assertLessEqual(self.c.ps_octave_bytes(),120)   # the slot state union stays 120 bytes

    def test_helpers_match_reference_arithmetic(self):
        for v in (1e-6,0.25,1.,3.,1e6,1e12):self.assertAlmostEqual(self.c.ps_rsqrt(v)*math.sqrt(v),1.,delta=1e-5)
        self.assertEqual(self.c.ps_rsqrt(0.),0.)
        for v in (0,1,-1,2**31+5,-(2**40)-3,123456789012345):self.assertAlmostEqual(self.c.ps_l2f(v),float(v),delta=abs(v)*1e-7+1e-9)

    def test_normalized_correlation_matches_python_including_wrap(self):
        rng=random.Random(5);N=256;r=[rng.randint(-30000,30000) for _ in range(N)];buf=(ct.c_int16*N)(*r)
        for ia,ib,n,s in ((200,150,40,1),(10,250,40,1),(5,90,32,4),(255,0,64,2),(100,100,40,1)):
            a=[r[(ia-k*s)%N] for k in range(n)];b=[r[(ib-k*s)%N] for k in range(n)]
            want=sum(p*q for p,q in zip(a,b))/math.sqrt(sum(p*p for p in a)*sum(q*q for q in b))
            self.assertAlmostEqual(self.c.ps_ncc(buf,N-1,ia,ib,n,s),want,delta=1e-5,msg=(ia,ib,n,s))

    def test_read_interpolates_the_written_history(self):
        x=[0.5*math.sin(0.01*i) for i in range(3000)];n=len(x)
        for d in (2.,10.25,333.5,1500.75):
            want=0.5*math.sin(0.01*(n-d))
            self.assertAlmostEqual(self.c.ps_read_after((ct.c_float*n)(*x),n,d),want,delta=3e-4,msg=d)

    def test_pitch_is_exact_at_every_ratio_with_bounded_delay(self):
        for ratio in (0.5,0.70710678,0.94387431,2.0):
            for f0 in (98.,220.):
                y=self.run_splicer(harmonic(f0,44100),ratio)
                self.assertGreater(self.c.ps_splices(),3,(ratio,f0))
                self.assertLess(abs(pitch_cents(y[22050:22050+8192],f0*ratio)),1.5,(ratio,f0))
                self.assertGreaterEqual(self.c.ps_dmin(),2.,(ratio,f0))
                self.assertLessEqual(self.c.ps_dmax(),1650.,(ratio,f0))

    def test_low_notes_and_chords_raise_without_collapsing(self):
        # E2 and an E power chord need jumps of one (535) or two (1070) source periods.
        for x in (harmonic(82.41,44100),[a+b for a,b in zip(harmonic(82.41,44100,.15),harmonic(123.47,44100,.15))]):
            y=self.run_splicer(x,2.,dmin=32.,dmax=1650.,jmax=1336,fade_max=256)
            self.assertGreater(self.c.ps_dmin(),2.)
            self.assertLess(abs(pitch_cents(y[22050:22050+8192],164.82)),1.5)

    def test_pick_attacks_are_detected_but_sustained_low_chords_are_not(self):
        held=[a+b+c for a,b,c in zip(harmonic(82.41,44100,.2),harmonic(123.47,44100,.2),harmonic(164.81,44100,.2))]
        self.run_splicer(held,0.9);self.assertLessEqual(self.c.ps_onsets(),1)   # the start only
        plucks=[0.]*44100;rng=random.Random(3)
        for t in (4410,15000,30000):
            for i in range(t,t+6000):plucks[i]+=0.3*math.exp(-(i-t)/2000.)*math.sin(2*math.pi*196*(i-t)/FS)+(0.1*rng.uniform(-1,1) if i<t+60 else 0.)
        self.run_splicer(plucks,0.9);self.assertEqual(self.c.ps_onsets(),3)

    def test_octave_voices(self):
        x=harmonic(196.,22050)
        dry=self.octave(x,100.,0.,0.)
        self.assertLess(max(abs(a-b) for a,b in zip(dry,x)),1e-6)        # unused voices are off: dry only
        sub=self.octave(x,0.,100.,0.);up=self.octave(x,0.,0.,100.)
        self.assertLess(abs(pitch_cents(sub[11025:11025+8192],98.)),2.)
        self.assertLess(abs(pitch_cents(up[11025:11025+8192],392.)),2.)
        self.assertTrue(all(math.isfinite(v) and abs(v)<=8. for v in self.octave([4. if i%2 else -4. for i in range(8000)],100.,100.,100.)))

if __name__=='__main__':unittest.main()
