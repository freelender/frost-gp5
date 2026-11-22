"""Eight-part split-storage FFT tests. Host results, not pedal qualification."""
import ctypes as ct
import math
import os
from pathlib import Path
import random
import sys
import tempfile
import unittest
import test_ir_fft as base
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
F=ct.c_float

class IRFFT512Tests(base.IRFFTTests):
    library_name='ir_fft512_split_host'
    build_flags=('-DFROST_IR_FFT_PARTS=8u','-DFROST_IR_FFT_SPLIT_SPECTRUM=1')
    def test_partition_count_and_impulse(self):
        c=self.c;r=random.Random(512)
        for taps in (1,64,65,128,256,257,320,384,448,511,512):
            c.k_init(1);h=[r.uniform(-.5,.5)/taps for _ in range(taps)]
            self.assertEqual(self.kload(h),1);self.assertEqual(c.k_parts(),(taps+63)//64)
            y=[]
            for _ in range(10):y+=self.kproc(([1.] if not y else [0.])+[0.]*63,1)[0]
            self.assertLess(max(abs(a-b) for a,b in zip(y,h+[0.]*(len(y)-taps))),1e-7)
            self.assertTrue(c.k_guard())
        self.assertEqual(self.kload([0.001]*513),0);self.assertEqual(c.k_parts(),8)
    def test_workspace_size_fits_large_margin(self):
        # Eight input partitions plus buffers; filter spectra are external.
        self.assertEqual(self.c.c_bytes(),5632+ct.sizeof(ct.c_void_p)+16)
    def test_first_and_last_taps_have_no_added_block_delay(self):
        self.c.k_init(1);h=[.5]+[0.]*510+[.5];self.kload(h);y=[]
        for i in range(10):y+=self.kproc(([1.] if not i else [0.])+[0.]*63,1)[0]
        self.assertAlmostEqual(y[0],.5,delta=1e-7);self.assertAlmostEqual(y[511],.5,delta=1e-7)
        self.assertLess(max(abs(v) for i,v in enumerate(y) if i not in (0,511)),1e-7)
    def test_missing_spectrum_rejected_without_changing_binding_or_ir(self):
        self.c.k_init(1);self.kload([.25]*4);self.assertEqual(self.c.k_missing_spectrum(),1)
    def test_long_ir_random_chunks_wraps_and_resets_match_direct(self):
        r=random.Random(51231)
        for taps in (257,320,511,512):
            self.c.k_init(1);self.kload([r.uniform(-1,1)/taps for _ in range(taps)])
            for step in range(160):
                mode=int(self.c.k_fill()==0 and step%3!=0)
                n=64 if mode else r.choice((1,7,31,64-self.c.k_fill()))
                y,ref=self.kproc([r.uniform(-1,1) for _ in range(n)],mode,step%2)
                self.assertLess(max(abs(a-b) for a,b in zip(y,ref)),4e-7)
                self.assertTrue(self.c.k_history_equal());self.assertTrue(self.c.k_guard())
                if step==80:self.c.k_clear()
    def test_512_accuracy_vs_double_reference(self):
        r=random.Random(51277);h=[F(r.gauss(0,1)*math.exp(-i/80)).value for i in range(512)]
        l1=sum(map(abs,h));h=[F(v/l1).value for v in h];self.c.k_init(1);self.kload(h)
        x=[F(.5*math.sin(i*.05)+.3*r.uniform(-1,1)).value for i in range(2560)];y=[]
        for i in range(0,len(x),64):y+=self.kproc(x[i:i+64],1)[0]
        exact=[math.fsum(h[k]*x[n-k] for k in range(min(n+1,512))) for n in range(len(x))]
        err=math.sqrt(sum((a-b)**2 for a,b in zip(y,exact))/len(x));rms=math.sqrt(sum(v*v for v in exact)/len(x))
        self.assertLess(20*math.log10(err/rms),-125)
    def test_same_256_ir_is_bit_identical_to_four_partition_layout(self):
        ref=ct.CDLL(str(build(ROOT/('build/ir_fft512_reference'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/ir_fft_host.c',includes=[ROOT/'tests/stubs'])))
        ref.k_load.argtypes=[ct.POINTER(F),ct.c_uint];ref.k_process.argtypes=[ct.POINTER(F),ct.POINTER(F),ct.POINTER(F),ct.c_uint,ct.c_uint,ct.c_uint]
        r=random.Random(512256);h=(F*256)(*[r.uniform(-1,1)/256 for _ in range(256)])
        ref.k_init(1);self.c.k_init(1);self.assertEqual(ref.k_load(h,256),1);self.assertEqual(self.c.k_load(h,256),1)
        for _ in range(50):
            x=[r.uniform(-1,1) for _ in range(64)];got=self.kproc(x,1)[0];a=(F*64)();b=(F*64)()
            self.assertEqual(ref.k_process((F*64)(*x),a,b,64,1,0),1);self.assertEqual(got,list(a))

class IR512StagingTests(unittest.TestCase):
    def test_512_requires_compact_profile_before_filesystem_changes(self):
        from prepare_gp5 import stage
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)/'stage'
            with self.assertRaisesRegex(ValueError,'compact-reverb'):
                stage(Path(d)/'missing',target,audio_input='adc',audio_output='line-test',cab_ir=Path(d)/'missing.wav',cab_ir_taps=512)
            self.assertFalse(target.exists())
    def test_512_cab_conversion_keeps_late_response_and_q15_bound(self):
        import cab_ir
        from test_cab_ir import wav
        h,info=cab_ir.convert(wav([1.]+[0.]*299+[.5]+[0.]*299),taps=512)
        q,scale=cab_ir.q15(h);self.assertEqual(info['kept_taps'],512);self.assertNotEqual(q[300],0)
        self.assertLessEqual(max(abs(a-b*scale) for a,b in zip(h,q)),scale*.501)
        self.assertIn('int16_t frost_test_ir_q15[512]',cab_ir.header(h,info,'q15'))

if __name__=='__main__':unittest.main()
