"""beta.14 SL-100 (effects/sl100): an original three-channel lead head inspired
by the Soldano SLO-100. Registry, channel voicing, bright cap, loudness against
UK-800, neck-pickup tightness, channel switching and bounds. Host evidence
only; no hardware or model-match claims. Pedal DSP cost is measured separately."""
import ctypes as ct,json,math,os,random,unittest
import test_new_effects as ne
from test_effect_library import tone_amp,brightness
SL100,UK800,BROWN,FCM=61,49,51,33
CLEAN,CLEAN_BRT,CRUNCH,CRUNCH_BRT,LEAD=range(5)
def chord(n=44100,a=.25):
    out=[]
    for i in range(n):
        t=i/ne.FS;env=math.exp(-1.5*t)
        out.append(a*env*sum(math.sin(2*math.pi*f*t)*w for f,w in ((82.4,1),(110,.8),(164.8,.6),(220,.4),(330,.3)))/3.1)
    return out
def db(y):return 10*math.log10(sum(v*v for v in y)/len(y)+1e-30)
class Sl100Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h=ne.NewEffectsTests()
        cls.c=cls.h.c=ct.CDLL(str(ne.build(ne.ROOT/('build/sl100_tests.dll' if os.name=='nt' else 'build/sl100_tests.so'),ne.ROOT/'tests/new_effects_host.c')))
        c=cls.c;c.ne_set.argtypes=[ct.c_uint,ct.c_float];c.ne_render.argtypes=[ct.POINTER(ct.c_float)]*2+[ct.c_uint]
        c.lib_bound.restype=ct.c_float;c.lib_name.restype=ct.c_char_p
    def render(self,x,p=None,t=SL100):return self.h.render(t,x,p)
    def test_registry_controls_and_picker(self):
        c=self.c
        self.assertEqual(c.lib_name(SL100),b'SL-100');self.assertEqual(c.lib_category(SL100),7);self.assertEqual(c.lib_workspace(SL100),0)
        self.assertEqual(c.lib_parameter_count(SL100),8)
        d=json.loads((ne.ROOT/'web/effects.json').read_text());e=d['effects'][str(SL100)]
        self.assertEqual([(p['name'],p['initial']) for p in e['params']],
                         [('MODE',4),('GAIN',55),('BASS',45),('MID',60),('TREBLE',55),('PRESENCE',50),('MASTER',50),('LEVEL',70)])
        self.assertEqual(e['params'][0]['labels'],['CLEAN','CLEAN BRT','CRUNCH','CRUNCH BRT','LEAD'])
        amp=d['picker'][7];self.assertEqual(amp.count(SL100),1);self.assertEqual(amp.index(SL100),amp.index(BROWN)+1)
    def test_channel_loudness_matches_uk800_at_defaults(self):
        x=chord();ref=db(self.render(x,t=UK800))
        for mode,lo,hi in ((CLEAN,-3.,-1.),(CLEAN_BRT,-3.,-.5),(CRUNCH,-1.,1.),(CRUNCH_BRT,-1.,1.),(LEAD,-1.,1.)):
            self.assertTrue(lo<=db(self.render(x,{0:mode}))-ref<=hi,mode)
    def test_channels_rise_in_gain_clean_crunch_lead(self):
        # Saturation as compression: how much the output drops when the input
        # drops 20 dB (host, 220 Hz, defaults): CLEAN ~19, CRUNCH ~9, LEAD ~2 dB.
        r=lambda y:math.sqrt(sum(v*v for v in y[11025:])/11025)
        def drop(p):return 20*math.log10(r(self.render(ne.sine(220,n=22050,a=.2),p))/r(self.render(ne.sine(220,n=22050,a=.02),p)))
        clean,crunch,lead=(drop({0:m}) for m in (CLEAN,CRUNCH,LEAD))
        self.assertGreater(clean,17.);self.assertTrue(5.<crunch<13.);self.assertLess(lead,3.)
        x=ne.sine(220,n=22050,a=.2)
        def distortion(y):
            y=y[11025:];return sum(tone_amp(y,f) for f in (440,660,880,1100))/max(tone_amp(y,220),1e-9)
        self.assertLess(distortion(self.render(x,{0:CLEAN})),.05)     # clean stays clean at the default GAIN
        for m in (CLEAN,CRUNCH,LEAD):self.assertGreater(distortion(self.render(x,{0:m,1:100})),distortion(self.render(x,{0:m,1:0})),m)
    def test_bright_cap_lifts_treble_mostly_at_low_gain(self):
        x=ne.sine(2500,n=22050,a=.05)
        low=self.h.gain(SL100,2500,{0:CLEAN_BRT,1:15})/self.h.gain(SL100,2500,{0:CLEAN,1:15})
        high=self.h.gain(SL100,2500,{0:CRUNCH_BRT,1:100})/self.h.gain(SL100,2500,{0:CRUNCH,1:100})
        self.assertGreater(low,1.3);self.assertLess(high,low)
        self.assertAlmostEqual(self.h.gain(SL100,100,{0:CLEAN_BRT,1:15})/self.h.gain(SL100,100,{0:CLEAN,1:15}),1.,delta=.1)   # lows untouched
    def test_presence_brightens_and_tone_controls_work(self):
        x=chord(22050)
        self.assertGreater(brightness(self.render(x,{5:100})),brightness(self.render(x,{5:0})))
        self.assertGreater(self.h.gain(SL100,100,{0:CLEAN,1:0,2:100}),self.h.gain(SL100,100,{0:CLEAN,1:0,2:0})*1.4)
        self.assertGreater(self.h.gain(SL100,4000,{0:CLEAN,1:0,4:100}),self.h.gain(SL100,4000,{0:CLEAN,1:0,4:0})*1.4)
    def test_lead_keeps_neck_pickup_bass_out_of_the_clipping(self):
        n=16384
        x=[.16*math.sin(2*math.pi*82.41*i/ne.FS)+.04*math.sin(2*math.pi*700*i/ne.FS) for i in range(n)]
        y=self.render(x,{0:LEAD,1:80})[8192:]
        carrier=tone_amp(y,700)
        side=math.sqrt(sum(tone_amp(y,700+s*82.41*k)**2 for k in (1,2,3) for s in (-1,1)))
        self.assertLess(20*math.log10(side/carrier),-14)
    def test_channel_switch_keeps_running_without_a_reset(self):
        c=self.c;c.ne_reset(SL100);x=chord(8192);a=(ct.c_float*4096)(*x[:4096]);b=(ct.c_float*4096)()
        c.ne_render(a,b,4096);first=list(b)
        self.assertEqual(c.ne_set(0,CLEAN),1)
        a=(ct.c_float*4096)(*x[4096:]);c.ne_render(a,b,4096);second=list(b)
        self.assertTrue(all(math.isfinite(v) and abs(v)<4. for v in first+second))
        self.assertLess(abs(second[0]-first[-1]),.5)          # no jump at the switch
    def test_extremes_and_random_edits_stay_bounded(self):
        x=[.9*math.sin(2*math.pi*82*i/ne.FS) for i in range(8192)]
        for mode in range(5):
            y=self.render(x,{0:mode,1:100,2:100,3:100,4:100,5:100,6:100,7:100})
            self.assertTrue(all(math.isfinite(v) and abs(v)<16. for v in y),mode)   # the engine's limit, as the other heads
        rnd=random.Random(5)
        for _ in range(20):
            p={0:rnd.randrange(5)};p.update({k:float(rnd.randrange(101)) for k in range(1,8)})
            self.assertTrue(all(math.isfinite(v) for v in self.render(x[:2048],p)))
        self.assertTrue(all(v==0. for v in self.render([0.]*2048)))   # silence in, silence out
    def test_block_kernel_is_bit_identical_to_the_sample_kernel(self):
        # beta.14 optimization: the settled block path must not change the sound.
        c=self.c;c.ne_render_sl100_block.argtypes=[ct.POINTER(ct.c_float)]*2+[ct.c_uint]
        x=chord(8192);rnd=random.Random(100)
        cases=[{0:m} for m in range(5)]+[{0:rnd.randrange(5),**{k:float(rnd.randrange(101)) for k in range(1,8)}} for _ in range(6)]
        for p in cases:
            ref=self.render(x,p)
            c.ne_reset(SL100)
            for i,v in p.items():self.assertEqual(c.ne_set(i,v),1)
            a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();c.ne_render_sl100_block(a,b,len(x))
            self.assertEqual(list(b),ref,p)
if __name__=='__main__':unittest.main()
