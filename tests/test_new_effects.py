"""Stage 5 response/stability/workspace tests; host evidence, no pedal claims."""
import ctypes as ct,json,math,os,random,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
NS,MB,HP,LP,WAH,BOOST,FOXX,TAPE,FUZZ,ACOUSTIC=range(35,45)
FS=44100
def sine(f,n=22050,a=.25):return [a*math.sin(2*math.pi*f*i/FS) for i in range(n)]
def rms(x):return math.sqrt(sum(v*v for v in x)/len(x))
class NewEffectsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/new_effects_tests.dll' if os.name=='nt' else 'build/new_effects_tests.so'),ROOT/'tests/new_effects_host.c')))
        cls.c.ne_set.argtypes=[ct.c_uint,ct.c_float]
        cls.c.ne_render.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        cls.c.lib_bound.restype=ct.c_float
        cls.c.lib_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_uint,ct.c_float]
        cls.c.lib_target.restype=ct.c_float
    def render(self,t,x,params=None,reset=True):
        if reset:self.c.ne_reset(t)
        for i,v in (params or {}).items():self.assertEqual(self.c.ne_set(i,v),1,(t,i,v))
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))();self.c.ne_render(a,b,len(x));return list(b)
    def gain(self,t,f,p):
        x=sine(f,a=.01);y=self.render(t,x,p);return rms(y[11025:])/rms(x[11025:])
    def test_registry_and_no_new_per_slot_or_large_workspace_memory(self):
        c=self.c;self.assertEqual(c.lib_effect_count(),62);self.assertEqual(c.lib_state_bytes(),120)
        self.assertEqual([c.lib_workspace(i) for i in range(35,45)],[0]*7+[2,0,0])
        self.assertEqual(c.lib_category(29),5) # existing M102-inspired DYNA retained
        d=json.loads((ROOT/'web/effects.json').read_text());seen=[]
        for cat,g in enumerate(d['picker']):
            for t in g:
                seen.append(t)
                if t!=255:self.assertEqual(d['effects'][str(t)]['category'],cat)
        self.assertEqual(sorted(seen),list(range(62))+[255])
    def test_hpf_lpf_cutoff_response_and_slope(self):
        for t in (HP,LP):
            p={0:1000,1:.707,2:0}
            self.assertAlmostEqual(self.gain(t,1000,p),.707,delta=.025)
            stop=100 if t==HP else 10000;passing=10000 if t==HP else 100
            g=self.gain(t,stop,p);self.assertLess(g,.02);self.assertGreater(self.gain(t,passing,p),.95)
            p[2]=1;self.assertLess(self.gain(t,stop,p),g*.04)
    def test_multiband_unity_reconstruction_and_low_band_compression(self):
        x=sine(80)+sine(5100);y=self.render(MB,x,{3:1})
        self.assertLess(max(abs(a-b) for a,b in zip(x,y)),2e-7)
        self.assertEqual(self.render(MB,x,{6:0}),list((ct.c_float*len(x))(*x)))
        p={0:-60,1:0,2:0,3:10,4:1,5:200}
        self.assertLess(self.gain(MB,80,p),.6);self.assertGreater(self.gain(MB,5000,p),.85)
    def test_suppressor_noise_attenuation_damp_zero_and_mute(self):
        x=sine(997,a=.00001);y=self.render(NS,x,{1:0,2:100})
        self.assertLess(rms(y[-5000:]),rms(x[-5000:])*.02)
        loud=sine(997,a=.2);y=self.render(NS,loud,{2:0})
        self.assertLess(max(abs(a-b) for a,b in zip(loud,y)),1e-7)
        self.assertEqual(max(map(abs,self.render(NS,loud,{3:2}))),0)
    def test_tape_first_repeat_reset_and_existing_delay_exclusivity(self):
        x=[.5]+[0.0]*12000;p={0:100,1:0,2:100,3:0,4:0,5:0}
        y=self.render(TAPE,x,p);self.assertEqual(max(map(abs,y[:4409])),0)
        self.assertGreater(max(map(abs,y[4410:4450])),.05)
        y=self.render(TAPE,[0.0]*12000,p);self.assertEqual(max(map(abs,y)),0)
        self.c.lib_init(0);types=(ct.c_uint*2)(TAPE,5);self.assertEqual(self.c.lib_configure(0,types,2),0)
    def test_treble_boost_rejects_bass_and_boost_control_is_effective(self):
        self.assertLess(self.gain(BOOST,60,{0:0}),self.gain(BOOST,3000,{0:0})*.2)
        self.assertGreater(self.gain(BOOST,1000,{0:100}),self.gain(BOOST,1000,{0:0})*5)
    def test_foxx_stages_manual_position_and_modulation_are_distinct(self):
        x=sine(400);a=self.render(FOXX,x,{3:0});b=self.render(FOXX,x,{3:1})
        self.assertGreater(rms([u-v for u,v in zip(a,b)]),.01)
        a=self.render(FOXX,x,{5:1,4:10});b=self.render(FOXX,x,{5:1,4:90})
        self.assertGreater(rms([u-v for u,v in zip(a,b)]),.01)
    def test_wah_modes_and_manual_control_change_spectrum(self):
        x=[a+b for a,b in zip(sine(300),sine(2000))]
        ys=[self.render(WAH,x,{3:m}) for m in range(5)]
        for a,b in zip(ys,ys[1:]):self.assertGreater(rms([u-v for u,v in zip(a,b)]),.005)
        self.assertEqual(self.render(WAH,x,{7:0}),list((ct.c_float*len(x))(*x)))
    def test_fuzz_cleanup_nonlinearity_and_dc_rejection(self):
        quiet=sine(220,a=.0001);loud=sine(220,a=.3)
        a=self.render(FUZZ,quiet);b=self.render(FUZZ,loud)
        self.assertGreater(rms(a)/rms(quiet),rms(b)/rms(loud)*3)
        self.assertLess(abs(sum(b[-11025:])/11025),.005)
    def test_acoustic_modes_and_mix(self):
        x=[a+b for a,b in zip(sine(170),sine(900))]
        ys=[self.render(ACOUSTIC,x,{2:m}) for m in range(3)]
        for a,b in zip(ys,ys[1:]):self.assertGreater(rms([u-v for u,v in zip(a,b)]),.01)
        self.assertEqual(self.render(ACOUSTIC,x,{3:0}),list((ct.c_float*len(x))(*x)))
    def test_all_new_models_remain_finite_at_limits_and_through_edits(self):
        rng=random.Random(17);x=[rng.uniform(-.7,.7) for _ in range(3000)]
        for t in range(35,45):
            for limit in (0,1):
                p={i:self.c.lib_bound(t,i,limit) for i in range(self.c.lib_parameter_count(t))}
                y=self.render(t,x,p)
                self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in y),(t,limit,max(map(abs,y))))
                y=self.render(t,[0.0]*3000,{i:self.c.lib_bound(t,i,1-limit) for i in p},reset=False)
                self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in y),(t,limit))
    def test_chain_partitions_preset_roundtrip_and_instances(self):
        c=self.c;x=sine(117,n=4096,a=.1);a=(ct.c_float*len(x))(*x)
        for t in range(35,45):
            outputs=[]
            for fixture,chunk in ((0,31),(1,64)):
                c.lib_init(fixture);types=(ct.c_uint*1)(t)
                self.assertEqual(c.lib_configure(fixture,types,1),1)
                self.assertEqual(c.lib_enable(fixture,0,1),1)
                b=(ct.c_float*len(x))();self.assertEqual(c.lib_process(fixture,a,b,len(x),chunk),0)
                outputs.append(bytes(b))
            self.assertEqual(outputs[0],outputs[1],t)
            value=c.lib_bound(t,0,0);self.assertEqual(c.lib_set(0,0,0,value),1)
            self.assertEqual(c.lib_capture(0),1);c.lib_init(1)
            self.assertEqual(c.lib_recall(1),1);z=(ct.c_float*512)();y=(ct.c_float*512)()
            self.assertEqual(c.lib_process(1,z,y,512,64),0)
            self.assertEqual(c.lib_type(1,0),t);self.assertEqual(c.lib_target(1,0,0),value)
        for t in (NS,MB,HP,LP,WAH,BOOST,FOXX,FUZZ,ACOUSTIC):
            c.lib_init(0);types=(ct.c_uint*2)(t,t);self.assertEqual(c.lib_configure(0,types,2),1)
            self.assertEqual(c.lib_set(0,0,0,c.lib_bound(t,0,0)),1)
            self.assertEqual(c.lib_target(0,1,0),c.lib_bound(t,0,2))
if __name__=='__main__':unittest.main()
