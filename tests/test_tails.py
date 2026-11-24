"""Beta.10 tails: TAIL OFF (default) cuts a switched-off delay/reverb and rests it
(no DSP work); TAIL ON lets echoes and the reverb ring out with the input faded
out, then rests. Host chain (engine/effect.c frost_tail_process)."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
DELAY,REVERB,DIGI,BBD,SPACE,SPRING,TAPE,GAIN=5,8,17,18,19,20,42,0
REST=1
class TailTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.c=ct.CDLL(str(build(ROOT/('build/tails.dll' if os.name=='nt' else 'build/tails.so'),ROOT/'tests/chain_host.c')))
        c.chain_wet.restype=ct.c_float;c.chain_parameter_name.restype=ct.c_char_p;c.chain_set.argtypes=[ct.c_uint,ct.c_uint,ct.c_float]
    def setup(self,t,tail):
        c=self.c;c.chain_init();a=(ct.c_uint*1)(t);self.assertEqual(c.chain_configure(a,1),1)
        n=c.chain_parameter_count(t)
        if t not in (DELAY,REVERB):
            self.assertEqual(c.chain_parameter_name(t,n-1),b'TAIL');self.assertEqual(c.chain_set(0,n-1,float(tail)),1)
        self.assertEqual(c.chain_enable(0,1),1)
    def run_blocks(self,blocks,signal=0.0):
        c=self.c;x=(ct.c_float*64)();y=(ct.c_float*64)();peak=0.0
        for b in range(blocks):
            for i in range(64):x[i]=signal*(1.0 if (b*64+i)%37<18 else -1.0)
            self.assertEqual(c.chain_process_block(x,y,64),1)
            peak=max(peak,max(abs(y[i]-x[i]) for i in range(64)))   # wet part only
        return peak
    def test_tail_off_cuts_and_rests(self):
        for t in (DIGI,BBD,TAPE,SPACE,SPRING,DELAY,REVERB):
            with self.subTest(t=t):
                c=self.c;self.setup(t,0)
                self.run_blocks(200,0.3)                     # play
                c.chain_enable(0,0);self.run_blocks(40)      # fade (~0.05 s)
                self.assertEqual(c.chain_suspended(0),REST)  # resting: no DSP work
                self.assertLess(self.run_blocks(400),1e-6)   # silence: no tail at all
                c.chain_enable(0,1);self.run_blocks(5);self.assertEqual(c.chain_suspended(0),0)
                self.assertGreater(self.run_blocks(400,0.3),1e-3)   # works again
                self.assertEqual(c.chain_faults(),0)
    def test_tail_on_rings_out_then_rests(self):
        for t in (DIGI,BBD,TAPE,SPACE,SPRING):
            with self.subTest(t=t):
                c=self.c;self.setup(t,1)
                self.run_blocks(400,0.3)                               # 0.58 s: longer than the longest delay time
                c.chain_enable(0,0);self.run_blocks(2,0.3)
                self.assertGreater(self.run_blocks(300),1e-3)          # the tail keeps playing after off
                self.assertNotEqual(c.chain_suspended(0),REST)
                for _ in range(400):                                   # up to ~37 s for the longest tails
                    self.run_blocks(64)
                    if c.chain_suspended(0)==REST:break
                self.assertEqual(c.chain_suspended(0),REST)
                self.assertLess(self.run_blocks(50),1e-6)
                c.chain_enable(0,1);self.assertGreater(self.run_blocks(400,0.3),1e-3)
                self.assertEqual(c.chain_faults(),0)
    def test_tail_on_does_not_pass_new_input_to_the_effect(self):
        c=self.c;self.setup(DIGI,1)
        self.run_blocks(10)                                             # nothing played yet
        c.chain_enable(0,0);self.run_blocks(20)                         # input fades out within 23 ms
        self.assertLess(self.run_blocks(400,0.3),1e-6)                  # playing now adds no echoes
    def test_other_effects_are_untouched(self):
        c=self.c;self.setup(GAIN,0) if False else None
        c.chain_init();a=(ct.c_uint*1)(GAIN);c.chain_configure(a,1);c.chain_enable(0,1);self.run_blocks(10,0.3)
        c.chain_enable(0,0);self.run_blocks(50,0.3);self.assertEqual(c.chain_suspended(0),0)
if __name__=='__main__':unittest.main()
