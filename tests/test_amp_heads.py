"""Algorithmic amp-head behavior and integration; no hardware/model-match claims."""
import ctypes as ct
import json
import math
import os
import random
import unittest
import test_new_effects as ne
from test_effect_library import tone_amp, brightness

HEADS = range(45, 52)

class AmpHeadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h = ne.NewEffectsTests()
        cls.c = cls.h.c = ct.CDLL(str(ne.build(ne.ROOT/('build/amp_heads_tests.dll' if os.name == 'nt'
                                                     else 'build/amp_heads_tests.so'),
                                            ne.ROOT/'tests/new_effects_host.c')))
        c = cls.c
        c.ne_set.argtypes = [ct.c_uint, ct.c_float]
        c.ne_render.argtypes = [ct.POINTER(ct.c_float), ct.POINTER(ct.c_float), ct.c_uint]
        c.lib_set.argtypes = [ct.c_uint, ct.c_uint, ct.c_uint, ct.c_float]
        c.lib_bound.restype = c.lib_target.restype = ct.c_float

    def render(self, t, x, p=None):
        return self.h.render(t, x, p)

    def test_seven_amp_entries_have_no_workspace_and_keep_old_ids(self):
        c = self.c
        self.assertEqual(c.lib_effect_count(), 59)
        self.assertEqual(c.lib_state_bytes(), 120)
        d = json.loads((ne.ROOT/'web/effects.json').read_text())
        self.assertEqual(d['picker'][7], list(HEADS)+[33])
        self.assertEqual(d['effects']['33']['short'], 'NAM')
        for t in HEADS:
            self.assertEqual((c.lib_parameter_count(t), c.lib_workspace(t), c.lib_category(t)), (7, 0, 7))
            self.assertEqual([p['name'] for p in d['effects'][str(t)]['params'][:4]],
                             ['GAIN', 'BASS', 'MID', 'TREBLE'])
        self.assertEqual(d['effects']['47']['params'][4]['name'], 'CUT')

    def test_voices_are_different_after_loudness_normalization(self):
        x = [sum(.15*math.sin(2*math.pi*f*i/ne.FS)/k for k,f in enumerate((110,220,440,880,1760),1))
             * math.exp(-i/15000) for i in range(8192)]
        ys = [self.render(t, x) for t in HEADS]
        normalized=[]
        for y in ys:
            level=ne.rms(y);normalized.append([v/level for v in y])
        for i,a in enumerate(normalized):
            for b in normalized[i+1:]:
                self.assertGreater(ne.rms([p-q for p,q in zip(a,b)]), .025)

    def test_gain_creates_harmonics_and_jc_is_clean_at_default(self):
        x = ne.sine(220, n=22050, a=.2)
        def distortion(y):
            y=y[11025:]
            return sum(tone_amp(y, f) for f in (440,660,880,1100))/max(tone_amp(y,220),1e-9)
        for t in HEADS:
            lo = distortion(self.render(t,x,{0:0}))
            hi = distortion(self.render(t,x,{0:100}))
            self.assertGreater(hi, lo+.002, (t,lo,hi))
        self.assertLess(distortion(self.render(48,x)), .02)
        self.assertGreater(distortion(self.render(51,x)), .15)

    def test_bass_mid_treble_controls_shape_their_bands(self):
        for t in HEADS:
            for p,f in ((1,100),(2,700),(3,5000)):
                # Quiet input isolates tone behavior from saturation.
                lo = self.h.gain(t,f,{0:0,p:0})
                hi = self.h.gain(t,f,{0:0,p:100})
                self.assertGreater(hi, lo*1.4,(t,p,lo,hi))

    def test_ac30_cut_darkens_and_other_presence_controls_brighten(self):
        x=[a+b for a,b in zip(ne.sine(300,n=8192),ne.sine(5000,n=8192))]
        for t in HEADS:
            low=brightness(self.render(t,x,{4:0}));high=brightness(self.render(t,x,{4:100}))
            if t==47:self.assertLess(high,low*.8)
            else:self.assertGreater(high,low*1.1)

    def test_master_and_level_mute_and_silence_has_no_self_noise(self):
        x=ne.sine(440,n=4096)
        for t in HEADS:
            for p in (5,6):self.assertEqual(max(map(abs,self.render(t,x,{p:0}))),0)
            self.assertEqual(max(map(abs,self.render(t,[0.0]*4096))),0)
            y=self.render(t,[.5]+[0.0]*44100)
            self.assertLess(max(map(abs,y[-4410:])),1e-6)

    def test_control_extremes_and_random_edits_remain_bounded(self):
        rng=random.Random(5150)
        for t in HEADS:
            self.render(t,[0.0])
            for i in range(30):
                x=[rng.uniform(-.7,.7) for _ in range(128)]
                p={j:float(rng.choice((0,100))) for j in range(7)}
                y=self.h.render(t,x,p,reset=False)
                self.assertTrue(all(math.isfinite(v) and abs(v)<16 for v in y),t)

    def test_presets_partitions_and_six_independent_head_instances(self):
        c=self.c;x=ne.sine(110,n=4096,a=.05);a=(ct.c_float*len(x))(*x)
        def process(fixture,chunk):
            y=(ct.c_float*len(x))();self.assertEqual(c.lib_process(fixture,a,y,len(x),chunk),0);return bytes(y)
        for t in HEADS:
            ys=[]
            for fixture,chunk in ((0,31),(1,64)):
                c.lib_init(fixture);types=(ct.c_uint*1)(t)
                self.assertEqual(c.lib_configure(fixture,types,1),1)
                self.assertEqual(c.lib_enable(fixture,0,1),1);ys.append(process(fixture,chunk))
            self.assertEqual(*ys)
            self.assertEqual(c.lib_set(0,0,6,37),1);self.assertEqual(c.lib_capture(0),1)
            c.lib_init(1);self.assertEqual(c.lib_recall(1),1);process(1,64)
            self.assertEqual(c.lib_type(1,0),t);self.assertEqual(c.lib_target(1,0,6),37)
        c.lib_init(0);types=(ct.c_uint*6)(45,46,47,48,49,51)
        self.assertEqual(c.lib_configure(0,types,6),1)
        self.assertEqual(c.lib_set(0,0,0,0),1)
        self.assertEqual(c.lib_target(0,1,0),35)
        for slot in range(6):self.assertEqual(c.lib_enable(0,slot,1),1)
        process(0,64)

    def test_cabinet_is_a_separate_linear_ir_block_after_every_head(self):
        from chain_host import Chain
        x=ne.sine(220,n=4096,a=.05)
        def render(t,cab):
            c=Chain();c.configure([t,3] if cab else [t])
            c.lib.chain_enable(0,1)
            if cab:
                c.load_ir([.5],slot=1);c.lib.chain_enable(1,1)
                c.lib.chain_set(1,0,ct.c_float(1.0)) # isolate the FIR from its VOL80 default
            c.process([0.0]*4096)
            y=c.process(x);self.assertEqual(c.lib.chain_faults(),0);return y
        for t in HEADS:
            a,b=render(t,False),render(t,True)
            # Bypass wet smoothing settles within a few float ULPs of 1.
            self.assertLess(max(abs(v*.5-w) for v,w in zip(a,b)),
                            max(map(abs,a))*1e-5+1e-8)

    def test_clean_heads_retain_upper_band_for_external_cabinet_ir(self):
        for t in (45,46,47,48):
            low=self.h.gain(t,1000,{0:0,3:100,4:0})
            high=self.h.gain(t,10000,{0:0,3:100,4:0})
            self.assertGreater(high,low*.2)


if __name__=='__main__':unittest.main()
