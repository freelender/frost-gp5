"""Host differential check: optimized POG grain-alignment search versus the
original search. Bit identity of the chosen restart distance only; ARM
arithmetic/output identity is checked separately in the emulator."""
import ctypes as ct
import os
from pathlib import Path
import random
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build

PCM=2304
UP_START=1026.0
DOWN_START=2.0
SPAN=8*63+60

class OctaveSearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ext='dll' if os.name=='nt' else 'so'
        cls.c=ct.CDLL(str(build(ROOT/f'build/octave_search_host.{ext}',ROOT/'tests/octave_search_host.c')))
        cls.c.os_compare.restype=ct.c_uint
        cls.c.os_compare.argtypes=[ct.POINTER(ct.c_float)]*2+[ct.c_uint]+[ct.POINTER(ct.c_float)]*2

    def check(self,pcm,position,valid,pairs):
        buf=(ct.c_int16*PCM)(*pcm);self.c.os_set(position,valid,buf)
        n=len(pairs);B=(ct.c_float*n)(*[p[0] for p in pairs]);O=(ct.c_float*n)(*[p[1] for p in pairs])
        F=(ct.c_float*n)();R=(ct.c_float*n)()
        bad=self.c.os_compare(B,O,n,F,R)
        self.assertEqual(bad,0,[(pairs[i],F[i],R[i]) for i in range(n) if F[i]!=R[i]][:5])
        return list(F)

    def pairs(self,rng):
        out=[]
        for base in (UP_START,DOWN_START,1.0,0.0,2.7,1026.5):
            for _ in range(6):out.append((base,rng.uniform(0.,2000.)))
            out+= [(base,0.),(base,1.),(base,1100.25),(base,PCM-1.),(base,PCM+5.)]
        # Bases whose window just fits / just exceeds the ring.
        for edge in (PCM-1-SPAN,PCM-SPAN,PCM-SPAN+1):out.append((float(edge),500.))
        return out

    def histories(self,rng):
        yield [rng.randint(-32768,32767) for _ in range(PCM)]           # full-range noise
        yield [rng.choice((-32768,32767)) for _ in range(PCM)]          # rails
        yield [int(9000*__import__('math').sin(i*.031)) for i in range(PCM)] # sine
        yield [0]*PCM                                                    # silence: ties
        yield [rng.randint(-3,3) for _ in range(PCM)]                    # tiny values, ties

    def test_identical_choice_all_positions_and_fill(self):
        rng=random.Random(31)
        hist=list(self.histories(rng));pairs=self.pairs(rng)
        valids=(0,1,59,60,500,SPAN,SPAN+1,SPAN+2,SPAN+1026,SPAN+1027,1700,PCM-1,PCM)
        checked=0
        for h in hist:
            for position in range(0,PCM,37):
                for valid in valids:
                    self.check(h,position,valid,pairs);checked+=1
        # Every wrap offset near position 0 for the fast path.
        for position in range(0,128):self.check(hist[0],position,PCM,pairs);checked+=1
        self.assertGreater(checked,1000)

    def test_fast_path_is_exercised_and_selects_nonzero(self):
        rng=random.Random(5);h=[rng.randint(-32768,32767) for _ in range(PCM)]
        r=self.check(h,700,PCM,[(UP_START,800.),(DOWN_START,300.)])
        self.assertTrue(any(v!=b for v,b in zip(r,(UP_START,DOWN_START))))

if __name__=='__main__':unittest.main()
