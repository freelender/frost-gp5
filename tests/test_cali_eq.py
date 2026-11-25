"""beta.14 CALI 5-BAND EQ (effects/cali_eq): registry, band calibration and
the classic "V" through the real registry/sample path. Host evidence only;
the pedal DSP cost (engine/load_budget.h) is an estimate until measured."""
import ctypes as ct,cmath,json,math,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
CALI_EQ,GE7=59,31
FS=44100
CENTRES=(80.,240.,750.,2200.,6600.)
def sine(f,n,a=.1):return [a*math.sin(2*math.pi*f*i/FS) for i in range(n)]
def tone(x,f):
    s=sum(v*cmath.exp(-2j*math.pi*f*i/FS) for i,v in enumerate(x));return 2*abs(s)/len(x)
def db(x,f,a=.1):return 20*math.log10(tone(x,f)/a)
class CaliEqTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/cali_eq_tests.dll' if os.name=='nt' else 'build/cali_eq_tests.so'),ROOT/'tests/new_effects_host.c')))
        c.ne_set.argtypes=[ct.c_uint,ct.c_float];c.ne_render.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.lib_bound.restype=ct.c_float;c.lib_name.restype=ct.c_char_p
    def render(self,x,params=None):
        self.c.ne_reset(CALI_EQ)
        for i,v in (params or {}).items():self.assertEqual(self.c.ne_set(i,v),1,(i,v))
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();self.c.ne_render(a,b,len(x))
        y=list(b);self.assertTrue(all(math.isfinite(v) for v in y));return y
    def settled(self,f,params=None):
        n=16384 if f<300 else 8192
        return self.render(sine(f,n),params)[n//2:]
    def test_registry_controls_category_and_picker(self):
        c=self.c
        self.assertEqual(c.lib_name(CALI_EQ),b'CALIEQ');self.assertEqual(c.lib_category(CALI_EQ),5)
        self.assertEqual(c.lib_workspace(CALI_EQ),0);self.assertEqual(c.lib_parameter_count(CALI_EQ),6)
        for p in range(6):
            self.assertEqual([c.lib_bound(CALI_EQ,p,i) for i in range(3)],[-12.,12.,0.],p)   # -12..+12 dB, flat by default
        d=json.loads((ROOT/'web/effects.json').read_text())
        e=d['effects'][str(CALI_EQ)];self.assertEqual([p['name'] for p in e['params']],['80','240','750','2.2K','6.6K','LEVEL'])
        dyn=d['picker'][5];self.assertEqual(dyn.count(CALI_EQ),1);self.assertEqual(dyn.index(CALI_EQ),dyn.index(GE7)+1)
    def test_flat_is_transparent(self):
        for f in (100.,1000.,5000.):
            self.assertAlmostEqual(db(self.settled(f),f),0.,delta=.02,msg=f)
    def test_each_band_hits_its_gain_at_its_centre(self):
        for band,f in enumerate(CENTRES):
            for g in (12.,-12.,6.):
                self.assertAlmostEqual(db(self.settled(f,{band:g}),f),g,delta=.3,msg=(band,g))
    def test_level_and_band_isolation(self):
        self.assertAlmostEqual(db(self.settled(1000.,{5:-6.}),1000.),-6.,delta=.05)
        self.assertAlmostEqual(db(self.settled(6600.,{0:12.}),6600.),0.,delta=.3)    # 80 Hz boost leaves 6.6 kHz alone
        self.assertAlmostEqual(db(self.settled(80.,{4:12.}),80.),0.,delta=.3)        # 6.6 kHz boost leaves 80 Hz alone
    def ge7(self,f,params):
        self.c.ne_reset(GE7)
        for i,v in params.items():self.assertEqual(self.c.ne_set(i,v),1)
        n=8192;x=sine(f,n);a=(ct.c_float*n)(*x);b=(ct.c_float*n)();self.c.ne_render(a,b,n);return db(list(b)[n//2:],f)
    def test_bands_are_broader_than_ge7(self):
        # 1.6-octave sections: half an octave from the centre a -12 dB cut still
        # reaches deeper than EQ-7's one-octave band at the same distance.
        cali=db(self.settled(750.*2**.5,{2:-12.}),750.*2**.5)
        ge7=self.ge7(800.*2**.5,{3:-12.})
        self.assertLess(cali,ge7-1.);self.assertLess(cali,-4.);self.assertGreater(cali,-12.)
    def test_classic_v_scoops_mids_and_lifts_lows_and_highs(self):
        v={0:6.,1:0.,2:-8.,3:0.,4:5.}
        lo,mid,hi=(db(self.settled(f,v),f) for f in (80.,750.,6600.))
        self.assertGreater(lo,4.);self.assertLess(mid,-6.);self.assertGreater(hi,3.)
    def test_extreme_settings_stay_bounded(self):
        x=[.9*math.sin(2*math.pi*80*i/FS) for i in range(8192)]
        y=self.render(x,{0:12.,1:12.,2:12.,3:12.,4:12.,5:12.})
        self.assertTrue(all(abs(v)<=12. for v in y))
if __name__=='__main__':unittest.main()
