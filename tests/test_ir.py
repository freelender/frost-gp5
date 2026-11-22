import ctypes as ct
import math
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from chain_host import Chain
from convert_ir import coefficients,emit
from chain_wav import run
from host import build
import os

class IRTests(unittest.TestCase):
    def setUp(self):
        self.c=Chain();self.lib=self.c.lib
        self.lib.ir_load.argtypes=[ct.POINTER(ct.c_float),ct.c_uint]
        self.lib.ir_process.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        self.lib.ir_guard_init()
    def load(self,h):return self.lib.ir_load((ct.c_float*len(h))(*h),len(h))
    def process(self,x):
        y=(ct.c_float*len(x))();self.lib.ir_process((ct.c_float*len(x))(*x),y,len(x));self.assertTrue(self.lib.ir_guard());return list(y)
    def test_impulse_and_dense_reference_wrap(self):
        r=random.Random(21)
        for taps in (1,17,128,512):
            h=[ct.c_float(r.uniform(-.5,.5)/taps).value for _ in range(taps)]
            self.assertTrue(self.load(h))
            self.assertEqual(self.process([1.]+[0.]*(taps+9)),h+[0.]*10)
            self.load(h);x=[ct.c_float(r.uniform(-.5,.5)).value for _ in range(1100)]
            expected=[sum(h[k]*x[n-k] for k in range(min(n+1,taps))) for n in range(len(x))]
            actual=[]
            for i in range(0,len(x),37):actual+=self.process(x[i:i+37])
            self.assertLess(max(abs(a-b) for a,b in zip(expected,actual)),2e-7)
    def test_invalid_load_is_atomic(self):
        self.load([.5,.25]);self.assertEqual(self.process([1.]),[.5])
        for h in ([],[float('nan')],[float('inf')],[5.,5.],[0.]*513):self.assertEqual(self.load(h),0)
        self.assertEqual(self.process([0.]),[.25])
        self.assertTrue(self.lib.ir_guard())
    def test_unrolled_matches_scalar_all_spans(self):
        scalar=ct.CDLL(str(build(ROOT/('build/ir_scalar.dll' if os.name=='nt' else 'build/ir_scalar.so'),
            ROOT/'tests/chain_host.c',flags=['-DFROST_IR_SCALAR'])))
        scalar.ir_load.argtypes=self.lib.ir_load.argtypes;scalar.ir_process.argtypes=self.lib.ir_process.argtypes
        scalar.ir_guard_init();r=random.Random(67)
        for taps in (1,2,3,4,5,7,17,63,128,255,512):
            h=[r.uniform(-.25,.25)/taps for _ in range(taps)]
            x=[r.uniform(-.5,.5) for _ in range(1101)]
            self.load(h);scalar.ir_load((ct.c_float*taps)(*h),taps)
            actual=self.process(x);expected=(ct.c_float*len(x))()
            scalar.ir_process((ct.c_float*len(x))(*x),expected,len(x))
            self.assertEqual(actual,list(expected));self.assertTrue(scalar.ir_guard())
    def test_block_outputs_match_scalar_across_seams_chunks_and_aliasing(self):
        scalar=ct.CDLL(str(build(ROOT/('build/ir_block_reference.dll' if os.name=='nt' else 'build/ir_block_reference.so'),
            ROOT/'tests/chain_host.c',flags=['-DFROST_IR_SCALAR'])))
        scalar.ir_load.argtypes=self.lib.ir_load.argtypes;scalar.ir_process.argtypes=self.lib.ir_process.argtypes
        self.lib.ir_process_block.argtypes=self.lib.ir_process.argtypes
        self.lib.ir_guard_init();scalar.ir_guard_init();r=random.Random(2501)
        for taps in (1,2,3,7,8,9,15,16,17,63,64,65,127,128,129,255,447,448,449,450,512):
            h=[r.uniform(-.5,.5)/taps for _ in range(taps)]
            x=[r.uniform(-.5,.5) for _ in range(3001)]+[0.]*(taps+3)
            self.assertTrue(self.load(h));scalar.ir_load((ct.c_float*taps)(*h),taps)
            expected=(ct.c_float*len(x))();scalar.ir_process((ct.c_float*len(x))(*x),expected,len(x))
            actual=[];pos=0
            while pos<len(x):
                n=min(r.choice((1,2,3,9,16,37,64,65)),len(x)-pos)
                a=(ct.c_float*n)(*x[pos:pos+n]);b=(ct.c_float*n)()
                if pos%2:self.lib.ir_process_block(a,a,n);actual.extend(a)
                else:self.lib.ir_process_block(a,b,n);actual.extend(b)
                pos+=n
            self.assertEqual(actual,list(expected),taps)
            self.assertTrue(self.lib.ir_guard());self.assertTrue(scalar.ir_guard())
    def test_paired_outputs_match_scalar_across_ring_seams_and_inplace_chunks(self):
        scalar=ct.CDLL(str(build(ROOT/('build/ir_pair_reference.dll' if os.name=='nt' else 'build/ir_pair_reference.so'),
            ROOT/'tests/chain_host.c',flags=['-DFROST_IR_SCALAR'])))
        scalar.ir_load.argtypes=self.lib.ir_load.argtypes;scalar.ir_process.argtypes=self.lib.ir_process.argtypes
        self.lib.ir_process_pairs.argtypes=self.lib.ir_process.argtypes
        scalar.ir_guard_init();r=random.Random(1820)
        for taps in (1,2,3,4,5,7,17,63,128,255,512):
            h=[r.uniform(-.5,.5)/taps for _ in range(taps)]
            x=[r.uniform(-.5,.5) for _ in range(4201)]+[0.]*(taps+3)
            self.load(h);scalar.ir_load((ct.c_float*taps)(*h),taps)
            expected=(ct.c_float*len(x))();scalar.ir_process((ct.c_float*len(x))(*x),expected,len(x))
            actual=[];pos=0
            while pos<len(x):
                n=min(r.choice((1,2,3,16,37,64,127)),len(x)-pos)
                a=(ct.c_float*n)(*x[pos:pos+n]);b=(ct.c_float*n)()
                if pos%2:self.lib.ir_process_pairs(a,a,n);actual.extend(a)
                else:self.lib.ir_process_pairs(a,b,n);actual.extend(b)
                pos+=n
            self.assertEqual(actual,list(expected),f'paired FIR differs at taps={taps}')
            self.assertTrue(self.lib.ir_guard() and scalar.ir_guard())
    def test_converter_and_wav_tail(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
            ir=Path(d)/'ir.wav';src=Path(d)/'src.wav';out=Path(d)/'out.wav';header=Path(d)/'ir.h'
            def wav(path,values,rate=44100):
                with wave.open(str(path),'wb') as w:
                    w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(struct.pack('<'+'h'*len(values),*values))
            wav(ir,[16384,-8192,4096]);values=coefficients(ir);self.assertEqual(values,[.5,-.25,.125])
            emit(values,header);self.assertIn('frost_cabinet_ir',header.read_text())
            self.assertIn((.5).hex()+'f',header.read_text())
            wav(src,[2000]*129);run(src,out,ir=ir)
            with wave.open(str(out),'rb') as w:self.assertEqual(w.getnframes(),131)
            wav(ir,[0]*513)
            with self.assertRaises(ValueError):coefficients(ir)
            wav(ir,[1],48000)
            with self.assertRaises(ValueError):coefficients(ir)

if __name__=='__main__':unittest.main()
