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
            if (kind,mode)==(1,4):
                # 32a: compact SHIMMER aligns its grain restarts; the frozen
                # float legacy copy does not, so only bounds are comparable.
                self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in b));continue
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
    def test_spring_block_is_bitwise_the_per_sample_reference(self):
        for seed in (1,2,3):
            with self.subTest(seed=seed):self.assertEqual(self.lib.rv16_spring_block_matches(150000,seed),-1)
    def test_shimmer_octave_is_in_tune_for_every_note(self):
        # 32a: with fixed grain restarts the octave landed up to ~150 cents
        # off, depending on the note. 32b: held note, wet only, measured 3 s
        # in, once the tank's own onset ringing has decayed (earlier, that
        # ringing rather than the shifter dominates the low notes).
        # beta.11: a 32768-sample window (an 8192 window is ~42 cents wide at
        # 220 Hz) and the median over three input levels. Low notes' octave
        # moves with level (110 Hz: -60..+51 cents over 0.2..0.4 input, before
        # and after the beta.11 tail codec); a single level measured luck.
        fs,n=44100,32768
        hann=[.5-.5*math.cos(2*math.pi*i/n) for i in range(n)]
        def power(s,f):
            c=2*math.cos(2*math.pi*f/fs);p=q=0.
            for v,h in zip(s,hann):p,q=v*h+c*p-q,p
            return p*p+q*q-c*p*q
        for f0 in (82,110,147,196,262,330,392,440):
            with self.subTest(f0=f0):
                found=[]
                for level in (.25,.3,.35):
                    x=[level*math.sin(2*math.pi*f0*i/fs) for i in range(int(fs*4.0))]
                    y=self.render(1,1,4,x,(100,50,60));s=[a-b for a,b in zip(y,x)][int(2.8*fs):int(2.8*fs)+n]
                    found.append(abs(max(range(-150,151,3),key=lambda c:power(s,2*f0*2**(c/1200)))))
                self.assertLessEqual(sorted(found)[1],25,found)
    def test_high_input_stays_bounded_for_every_network(self):
        x=[16. if i%2 else -16. for i in range(20000)]
        for kind,mode,p in [(0,0,(.95,0,1)),*[(1,m,(100,100,100)) for m in range(5)],(2,0,(100,100,100))]:
            with self.subTest(kind=kind,mode=mode):self.assertTrue(all(math.isfinite(v) and abs(v)<=16 for v in self.render(1,kind,mode,x,p)))
    def test_tail_codec_is_ieee_half_round_to_nearest_even(self):
        """The host C model must equal the DSP's VCVTB: IEEE binary16, ties to
        even, subnormals kept (Python struct 'e' is the reference)."""
        import random,struct
        c=self.lib;c.rv16_half.argtypes=[ct.c_float];c.rv16_half_value.restype=ct.c_float
        rng=random.Random(1611);values=[0.,-0.,6.1035156e-05,5.9604645e-08,2.9802322e-08,65504.,-65504.]
        for e in range(-27,16):
            for _ in range(300):values.append(rng.choice((1,-1))*rng.uniform(1,2)*2.**e)
        for h in range(0,0x7c00,7):                     # exact ties between neighbouring halves
            a=struct.unpack('<e',struct.pack('<H',h))[0];b=struct.unpack('<e',struct.pack('<H',h+1))[0];values.append((a+b)/2)
        for v in values:
            v=struct.unpack('<f',struct.pack('<f',v))[0]
            want=struct.unpack('<H',struct.pack('<e',v))[0]
            self.assertEqual(c.rv16_half(v),want,v)
            self.assertEqual(struct.pack('<f',c.rv16_half_value(want)),struct.pack('<f',struct.unpack('<e',struct.pack('<H',want))[0]),hex(want))
    def test_fading_tail_keeps_its_resolution_down_to_minus_100_dbfs(self):
        """beta.11 (tester report: comb-filter / noise-gate sound as reverbs
        fade). The 16-bit tail must track the float network as it decays, not
        grain up and cut to zero near -66 dBFS as linear Q11/Q12 storage did."""
        n=44100*6;x=[0.]*n
        for f in (82.41,123.47,164.81,207.65):
            for i in range(66150):x[i]+=.06*math.exp(-2.5*i/44100)*sum(math.sin(2*math.pi*f*h*i/44100)/h for h in range(1,6))
        for kind,mode,p in [(0,0,(.9,.5,1)),(1,0,(100,50,70)),(1,1,(100,50,70)),(1,2,(100,50,70)),(1,3,(100,50,70)),(2,0,(100,50,70))]:
            with self.subTest(kind=kind,mode=mode):
                a=self.render(0,kind,mode,x,p);b=self.render(1,kind,mode,x,p)
                a=[v-d for v,d in zip(a,x)];b=[v-d for v,d in zip(b,x)]
                checked=0
                for s in range(66150,n,11025):
                    sig=sum(v*v for v in a[s:s+11025])/11025
                    if not 1e-10<sig<1e-4:continue          # -100..-40 dBFS windows
                    err=sum((v-w)**2 for v,w in zip(a[s:s+11025],b[s:s+11025]))/11025
                    self.assertGreater(10*math.log10(sig/max(err,1e-30)),55,(s/44100,10*math.log10(sig)))
                    checked+=1
                self.assertGreaterEqual(checked,2)
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
