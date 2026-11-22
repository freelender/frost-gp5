import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

class StartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles=[]
        for name,flags in [('synthetic',[]),('adc',['-DFROST_AUDIO_ADC=1'])]:
            c=ct.CDLL(str(build(ROOT/('build/startup_'+name+('.dll' if os.name=='nt' else '.so')),
                         ROOT/'tests/startup_host.c',includes=[ROOT/'tests/stubs'],
                         flags=['-finstrument-functions',*flags])))
            c.startup_snapshot.argtypes=[ct.POINTER(ct.c_uint8)]
            c.startup_wet.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
            cls.profiles.append((name,c))
    def snapshot(self,c):
        n=c.startup_snapshot(None);data=(ct.c_uint8*n)();self.assertEqual(c.startup_snapshot(data),n)
        return bytes(data)
    def test_rehearsal_runs_exactly_once_silent_and_restores_initial_state(self):
        for name,c in self.profiles:
            with self.subTest(profile=name):
                c.startup_init();before=self.snapshot(c)
                self.assertEqual(c.startup_calls(),0);self.assertEqual(c.startup_prepare(),1)
                self.assertEqual(c.startup_calls(),1);self.assertEqual(self.snapshot(c),before)
    def test_first_live_blocks_match_cold_initialization_including_meters_and_phase(self):
        for name,c in self.profiles:
            with self.subTest(profile=name):
                c.startup_init();cold=[]
                for seed in (0,100000000,400000000,1000000):
                    self.assertEqual(c.startup_audio(seed),1);cold.append(self.snapshot(c))
                c.startup_init();self.assertEqual(c.startup_prepare(),1)
                for i,seed in enumerate((0,100000000,400000000,1000000)):
                    self.assertEqual(c.startup_audio(seed),1);self.assertEqual(self.snapshot(c),cold[i])
    def test_first_processed_float_stream_is_bit_identical_after_preparation(self):
        for name,c in self.profiles:
            with self.subTest(profile=name):
                x=(ct.c_float*257)(*[((i*37)%211-105)/512 for i in range(257)])
                cold=(ct.c_float*257)();prepared=(ct.c_float*257)()
                c.startup_init();c.startup_wet(x,cold,257)
                c.startup_init();c.startup_prepare();c.startup_wet(x,prepared,257)
                self.assertEqual(bytes(cold),bytes(prepared))

    def test_rehearsal_fault_is_retained_across_state_restoration(self):
        for name,c in self.profiles:
            with self.subTest(profile=name):
                c.startup_init();c.startup_fail_rehearsal();self.assertEqual(c.startup_prepare(),1)
                self.assertEqual(c.startup_faults(),7);self.assertEqual(c.startup_calls(),1)
                self.assertEqual(c.startup_audio(100000000),1)
                self.assertEqual(c.startup_faults(),7)

if __name__=='__main__':unittest.main()
