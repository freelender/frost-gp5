"""Acoustic behavior/regressions; synthetic host evidence, not AC-3 matching."""
import json
import ctypes as ct
import math
import os
import random
import unittest
import test_new_effects as ne


class AcousticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h = ne.NewEffectsTests()
        # Separate fixture: Windows cannot rebuild a DLL loaded by another
        # test class later in the full discovery run.
        cls.h.c = ct.CDLL(str(ne.build(ne.ROOT/('build/acoustic_tests.dll' if os.name == 'nt'
                                               else 'build/acoustic_tests.so'),
                                      ne.ROOT/'tests/new_effects_host.c')))
        cls.h.c.ne_set.argtypes = [ct.c_uint, ct.c_float]
        cls.h.c.ne_render.argtypes = [ct.POINTER(ct.c_float), ct.POINTER(ct.c_float), ct.c_uint]

    def test_body_adds_two_local_resonances_instead_of_a_bass_shelf(self):
        for mode, peaks in ((0, (110, 235)), (1, (95, 190))):
            for f in peaks:
                lo = self.h.gain(ne.ACOUSTIC, f, {0: 0, 2: mode})
                hi = self.h.gain(ne.ACOUSTIC, f, {0: 100, 2: mode})
                self.assertGreater(20*math.log10(hi/lo), 4.0)
            # BODY should not act as broadband makeup gain.
            lo = self.h.gain(ne.ACOUSTIC, 6400, {0: 0, 2: mode})
            hi = self.h.gain(ne.ACOUSTIC, 6400, {0: 100, 2: mode})
            self.assertLess(abs(20*math.log10(hi/lo)), 0.1)

    def test_standard_scoops_pickup_midrange_and_jumbo_moves_body_down(self):
        g = lambda f, m: self.h.gain(ne.ACOUSTIC, f, {2: m})
        self.assertLess(g(800, 0), g(235, 0)*0.4)
        self.assertGreater(g(95, 1), g(95, 0)*1.25)
        self.assertGreater(g(235, 0), g(235, 1)*1.15)
        self.assertLess(g(30, 0), g(110, 0)*0.4)

    def test_piezo_body_is_subdued_and_existing_mode_numbers_are_preserved(self):
        g = lambda m: self.h.gain(ne.ACOUSTIC, 235, {0: 100, 2: m})
        self.assertLess(g(2), g(0)*0.65)
        d = json.loads((ne.ROOT/'web/effects.json').read_text())
        self.assertEqual(d['effects']['44']['params'][2]['labels'],
                         ['STANDARD', 'JUMBO', 'PIEZO', 'ENHANCE'])

    def test_top_emphasizes_upper_range_without_boosting_body(self):
        for m in range(4):
            g = lambda f, t: self.h.gain(ne.ACOUSTIC, f, {1: t, 2: m})
            self.assertGreater(g(3200, 100), g(3200, 0)*2)
            self.assertLess(abs(20*math.log10(g(110, 100)/g(110, 0))), 1.0)

    def test_enhance_has_a_stronger_onset_relative_to_sustain(self):
        x = [0.0]*4410 + ne.sine(3200, n=22050, a=.15)
        ys = [self.h.render(ne.ACOUSTIC, x, {2: m}) for m in (0, 3)]
        ratios = [ne.rms(y[4450:4850])/ne.rms(y[-4410:]) for y in ys]
        self.assertGreater(ratios[1], ratios[0]*1.08)

    def test_top_generates_gentle_upper_harmonics_with_no_dc(self):
        # Integer periods avoid measuring leakage as harmonic generation.
        n = 22050
        x = ne.sine(1000, n=n*2, a=.3)
        def harmonic(y):
            y = y[-n:]
            return 2/n*abs(sum(v*complex(math.cos(2*math.pi*2000*i/ne.FS),
                                        -math.sin(2*math.pi*2000*i/ne.FS))
                               for i, v in enumerate(y)))
        dark = self.h.render(ne.ACOUSTIC, x, {1: 0})
        bright = self.h.render(ne.ACOUSTIC, x, {1: 100})
        self.assertLess(harmonic(dark), 1e-5)
        self.assertGreater(harmonic(bright), 2e-4)
        self.assertLess(harmonic(bright), .02)
        self.assertLess(abs(sum(bright[-n:])/n), 1e-5)

    def test_silence_reset_decay_and_no_long_reverb_workspace(self):
        for m in range(4):
            self.assertEqual(max(map(abs, self.h.render(ne.ACOUSTIC, [0.0]*5000, {2: m}))), 0)
            y = self.h.render(ne.ACOUSTIC, [.5]+[0.0]*22049, {0: 100, 2: m})
            self.assertLess(max(map(abs, y[-4410:])), 1e-7)
            self.assertEqual(max(map(abs, self.h.render(ne.ACOUSTIC, [0.0]*5000))), 0)

    def test_rapid_mode_control_edits_stay_bounded(self):
        rng = random.Random(44)
        self.h.render(ne.ACOUSTIC, [0.0])
        for i in range(160):
            x = [rng.uniform(-.7, .7) for _ in range(64)]
            p = {0: 100*(i % 2), 1: 100*((i//2) % 2), 2: i % 4, 4: 100}
            y = self.h.render(ne.ACOUSTIC, x, p, reset=False)
            self.assertTrue(all(math.isfinite(v) and abs(v) < 16 for v in y))


if __name__ == '__main__':
    unittest.main()
