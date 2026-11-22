"""Host results only: compact history precision, reset, stability and dispatch."""
import ctypes as ct
import json
import math
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class Reverb16Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/reverb16'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/reverb16_host.c')))
        cls.lib.rv16_render.argtypes=[ct.c_uint]*3+[ct.POINTER(ct.c_float)]*2+[ct.c_uint]+[ct.c_float]*3
        cls.lib.rv16_chain.argtypes=[ct.c_uint]+[ct.POINTER(ct.c_float)]*2+[ct.c_uint]
        cls.lib.rv16_store.argtypes=[ct.c_float,ct.c_float]
        cls.lib.rv16_load.argtypes=[ct.c_int,ct.c_float];cls.lib.rv16_load.restype=ct.c_float
    def render(self,compact,kind,mode,x,p):
        a=(ct.c_float*len(x))(*x);b=(ct.c_float*len(x))()
        self.assertEqual(self.lib.rv16_render(compact,kind,mode,a,b,len(x),*p),0)
        return list(b)
    def test_storage_halves_samples_without_changing_float_state(self):
        self.assertEqual(self.lib.rv16_bytes(0)-self.lib.rv16_bytes(1),5234*2-48-480)
        self.assertEqual(self.lib.rv16_bytes(1),11292)
    def test_codec_saturates_before_conversion_and_truncates_toward_zero(self):
        c=self.lib
        for v,want in [(float('nan'),0),(float('inf'),32767),(-float('inf'),-32768),(16.,32767),(-16.,-32768),(.25,512),(-.25,-512)]:
            self.assertEqual(c.rv16_store(v,2048),want)
        for v in [-.003,-.0002,0,.0002,.003]:
            got=c.rv16_load(c.rv16_store(v,2048),1/2048)
            self.assertLessEqual(abs(got),abs(v)+1e-10);self.assertLess(abs(got-v),1/2048)
    def test_plain_block_matches_sample_warmup_and_wraps(self):
        self.assertEqual(self.lib.rv16_block_sample(0,0,20000),-1)
    def test_fast_quantizer_matches_original_at_every_integer_boundary(self):
        self.assertEqual(self.lib.rv16_quantizer_sweep(),0)
    def test_fast_quantizer_matches_original_for_arbitrary_float_bit_patterns(self):
        self.assertEqual(self.lib.rv16_quantizer_random(2000000),0)
    def test_integer_rail_test_matches_original_float_clamp(self):
        self.assertEqual(self.lib.rv16_limit_random(2000000),0)
    def test_all_rv6_modes_block_match_reference_and_wraps(self):
        for mode in range(5):
            with self.subTest(mode=mode):self.assertEqual(self.lib.rv16_block_sample(1,mode,20000),-1)
    def test_poisoned_reset_is_silent_for_every_network(self):
        for kind,mode,p in [(0,0,(.95,.5,1)),*[(1,m,(100,50,100)) for m in range(5)],(2,0,(100,100,50))]:
            with self.subTest(kind=kind,mode=mode):self.assertEqual(self.render(1,kind,mode,[0.]*14000,p),[0.]*14000)
    def test_error_and_tail_energy_against_float_network(self):
        # Full-rate float implementation independently checks storage error.
        x=[.35*math.exp(-i/18000)*sum(math.sin(i*.0157*j)/j for j in range(1,6)) if i<44100 else 0. for i in range(132300)]
        rows=[]
        for kind,mode,p in [(0,0,(.9,.5,1)),*[(1,m,(100,50,85)) for m in range(5)],(2,0,(50,100,50))]:
            a=self.render(0,kind,mode,x,p);b=self.render(1,kind,mode,x,p)
            err=math.sqrt(sum((v-w)**2 for v,w in zip(a,b))/len(a))
            peak=max(abs(v-w) for v,w in zip(a,b))
            tail_a=sum(v*v for v in a[44100:]);tail_b=sum(v*v for v in b[44100:])
            rows.append(dict(kind=kind,mode=mode,rms_error_dbfs=20*math.log10(max(err,1e-30)),peak_error=peak,tail_energy_ratio=tail_b/max(tail_a,1e-30)))
            self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in b))
            self.assertLess(20*math.log10(max(err,1e-30)),-68)
            self.assertLess(peak,.002)
            # The bass-filtered generic tail has lower energy at the same
            # fixed storage LSB; allow 4% loss there, retaining the absolute
            # noise/peak limits above and 3% for the model networks.
            self.assertGreater(tail_b/max(tail_a,1e-30),.96 if kind==0 else .97)
            self.assertLess(tail_b/max(tail_a,1e-30),1.03)
        (ROOT/'build/reverb31j').mkdir(exist_ok=True)
        (ROOT/'build/reverb31j/quality.json').write_text(json.dumps(dict(kind='host numerical comparison, not listening or pedal measurement',scenarios=rows),indent=2)+'\n')
    def test_high_input_stays_bounded_for_every_network(self):
        x=[16. if i%2 else -16. for i in range(20000)]
        for kind,mode,p in [(0,0,(.95,0,1)),*[(1,m,(100,100,100)) for m in range(5)],(2,0,(100,100,100))]:
            with self.subTest(kind=kind,mode=mode):self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in self.render(1,kind,mode,x,p)))
    def test_quiet_tail_does_not_leave_persistent_quantizer_noise(self):
        x=[1.]+[0.]*440999
        for kind,mode,p in [(0,0,(.95,.5,1)),(1,1,(100,50,100)),(2,0,(50,100,50))]:
            with self.subTest(kind=kind,mode=mode):
                y=self.render(1,kind,mode,x,p)
                self.assertLess(max(abs(v) for v in y[-44100:]),1e-7)
    def test_active_chain_dispatch_uses_compact_workspace(self):
        for t in (8,19,20):
            a=(ct.c_float*5000)(*[.1*math.sin(i*.1) for i in range(5000)]);b=(ct.c_float*5000)()
            self.assertEqual(self.lib.rv16_chain(t,a,b,5000),1)
            self.assertTrue(all(math.isfinite(v) for v in b))

    def test_zero_wet_preserves_dry_for_every_mode_and_backend(self):
        x=[.3*math.sin(i*.13)+.2*math.sin(i*.017) for i in range(6000)]
        expected=[ct.c_float(v).value for v in x]
        for compact in (0,1):
            for kind,mode,p in [(0,0,(.9,.8,0)),*[(1,m,(0,50,90)) for m in range(5)],(2,0,(80,0,50))]:
                with self.subTest(compact=compact,kind=kind,mode=mode):
                    self.assertEqual(self.render(compact,kind,mode,x,p),expected)

    def test_wet_excitation_rejects_sustained_dc(self):
        # Bass protection must act before the tanks, rather than just hiding
        # their buildup with an output filter. A settled DC input excites no
        # wet signal apart from float roundoff; check the complete generic comb network in both backends.
        for compact in (0,1):
            y=self.render(compact,0,0,[.25]*132300,(.9,.5,1))
            self.assertLess(max(map(abs,y[-44100:])),2e-6)

if __name__=='__main__':unittest.main()
