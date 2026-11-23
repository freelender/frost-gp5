"""32l effects (effects/extras): behaviour on the host through the real
registry/sample path. Host evidence only; pedal DSP cost is measured separately."""
import ctypes as ct,cmath,math,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
ROTARY,RING,CRUSH,HTREM,MUFF,BOOST,SWELL=range(52,59)
FS=44100
def sine(f,n,a=.25):return [a*math.sin(2*math.pi*f*i/FS) for i in range(n)]
def rms(x):return math.sqrt(sum(v*v for v in x)/len(x))
def tone(x,f):
    """Amplitude of frequency f in x (single-bin DFT)."""
    s=sum(v*cmath.exp(-2j*math.pi*f*i/FS) for i,v in enumerate(x));return 2*abs(s)/len(x)
def envelope(x,w=441):return [rms(x[i:i+w]) for i in range(0,len(x)-w,w)]
def crossings(e):
    m=sum(e)/len(e);return sum(1 for a,b in zip(e,e[1:]) if (a-m)*(b-m)<0)
class ExtrasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/extras_tests.dll' if os.name=='nt' else 'build/extras_tests.so'),ROOT/'tests/new_effects_host.c')))
        c.ne_set.argtypes=[ct.c_uint,ct.c_float];c.ne_render.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.lib_bound.restype=ct.c_float
    def render(self,t,x,params=None):
        self.c.ne_reset(t)
        for i,v in (params or {}).items():self.assertEqual(self.c.ne_set(i,v),1,(t,i,v))
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();self.c.ne_render(a,b,len(x))
        y=list(b);self.assertTrue(all(math.isfinite(v) for v in y),t);return y
    def test_registry_names_categories_and_state_fits(self):
        c=self.c;c.lib_name.restype=ct.c_char_p
        self.assertEqual([c.lib_name(t) for t in range(52,59)],[b'ROTARY',b'RING',b'CRUSH',b'H-TREM',b'BIG PI',b'BOOST',b'SWELL'])
        self.assertEqual([c.lib_category(t) for t in range(52,59)],[4,4,1,4,1,0,5])
        self.assertEqual([c.lib_workspace(t) for t in range(52,59)],[0]*7)
        self.assertEqual(c.lib_state_bytes(),120)            # no slot grew
    def test_rotary_fast_spins_faster_than_slow_and_stays_near_unity(self):
        x=sine(3000,FS*4,.2)
        slow=self.render(ROTARY,x,{0:0.0,1:100.0})[FS*2:];fast=self.render(ROTARY,x,{0:1.0,1:100.0})[FS*2:]
        cs,cf=crossings(envelope(slow)),crossings(envelope(fast))
        self.assertLess(cs,8);self.assertGreater(cf,3*cs)      # ~0.8 Hz vs ~6.8 Hz rotor
        self.assertGreater(rms(fast)/rms(x[FS*2:]),.5);self.assertLess(rms(fast)/rms(x[FS*2:]),1.5)
        lo=self.render(ROTARY,sine(100,FS),{2:0.0})[FS//2:];hi=self.render(ROTARY,sine(100,FS),{2:100.0})[FS//2:]
        self.assertGreater(rms(lo),3*rms(hi))                  # BALANCE 0 keeps the drum (lows)
    def test_ring_modulator_moves_energy_to_sum_and_difference(self):
        y=self.render(RING,sine(1000,8820),{0:440.0,1:100.0})
        self.assertLess(tone(y,1000),.01);self.assertGreater(tone(y,560),.1);self.assertGreater(tone(y,1440),.1)
        dry=self.render(RING,sine(1000,8820),{1:0.0});self.assertGreater(tone(dry,1000),.24)
    def test_crusher_quantizes_and_mix_zero_is_dry(self):
        x=sine(220,4410,.8)
        y=self.render(CRUSH,x,{0:2.0,1:44.1,2:100.0})
        self.assertLessEqual(len({round(v,6) for v in y}),5)  # 2 bits: a handful of levels
        y=self.render(CRUSH,x,{0:16.0,1:2.0,2:100.0})
        self.assertGreater(sum(1 for a,b in zip(y,y[1:]) if a==b),len(y)*.8)   # 2 kHz hold
        self.assertEqual(self.render(CRUSH,x,{2:0.0}),[float(ct.c_float(v).value) for v in x])
    def test_harmonic_tremolo_bands_move_in_opposite_phase(self):
        lo=envelope(self.render(HTREM,sine(100,FS),{0:4.0,1:100.0}))[20:]
        hi=envelope(self.render(HTREM,sine(4000,FS),{0:4.0,1:100.0}))[20:]
        ml,mh=sum(lo)/len(lo),sum(hi)/len(hi)
        corr=sum((a-ml)*(b-mh) for a,b in zip(lo,hi))/math.sqrt(sum((a-ml)**2 for a in lo)*sum((b-mh)**2 for b in hi))
        self.assertLess(corr,-.5)
    def test_muff_gain_is_bounded_and_tone_tilts(self):
        y=self.render(MUFF,sine(196,FS//2,.02),{0:100.0,1:50.0,2:50.0})
        self.assertGreater(rms(y[4410:]),.2);self.assertLess(max(abs(v) for v in y),2.0)
        dark=self.render(MUFF,sine(196,FS//2,.05),{1:0.0});bright=self.render(MUFF,sine(196,FS//2,.05),{1:100.0})
        ratio=lambda y:tone(y[4410:],196*9)/tone(y[4410:],196)
        self.assertGreater(ratio(bright),3*ratio(dark))
    def test_boost_gain_in_db_and_bright_lift(self):
        x=sine(500,8820,.01)
        self.assertAlmostEqual(rms(self.render(BOOST,x,{0:0.0}))/rms(x),1.0,places=2)
        self.assertAlmostEqual(rms(self.render(BOOST,x,{0:20.0}))/rms(x),10.0,delta=.05)
        h=sine(8000,8820,.01);plain=rms(self.render(BOOST,h,{0:0.0,1:0.0})[441:]);lift=rms(self.render(BOOST,h,{0:0.0,1:1.0})[441:])
        self.assertGreater(lift/plain,1.3)
    def test_swell_fades_in_each_note_after_silence(self):
        note=[0.0]*4410+sine(220,FS,.3)
        y=self.render(SWELL,note+[0.0]*8820+note,{0:500.0,1:50.0,2:50.0})
        first=rms(y[4410:4410+2205]);later=rms(y[4410+FS-4410:4410+FS])
        self.assertLess(first,.25*later)                       # first 50 ms of the note is quiet
        self.assertAlmostEqual(later/rms(note[-4410:]),1.0,delta=.05)
        start=4410+FS+8820+4410                                # second note swells again
        self.assertLess(rms(y[start:start+2205]),.25*later)
if __name__=='__main__':unittest.main()
