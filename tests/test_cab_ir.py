import ctypes as ct
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import cab_ir
from host import build

def wav(frames,rate=44100,bits=16,channels=1,tag=1,extensible=False):
    """frames: list of per-frame tuples (or scalars for mono) in -1..1."""
    out=bytearray()
    for f in frames:
        for v in (f if isinstance(f,tuple) else (f,)):
            if tag==3:out+=struct.pack('<f' if bits==32 else '<d',v)
            elif bits==16:out+=struct.pack('<h',max(-32768,min(32767,round(v*32767))))
            elif bits==24:out+=struct.pack('<i',max(-8388608,min(8388607,round(v*8388607))))[:3]
            else:out+=struct.pack('<i',max(-2**31,min(2**31-1,round(v*(2**31-1)))))
    align=channels*bits//8
    if extensible:
        fmt=struct.pack('<HHIIHHHHIH14s',0xFFFE,channels,rate,rate*align,align,bits,22,bits,0,tag,b'\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x008\x9bq')
    else:fmt=struct.pack('<HHIIHH',tag,channels,rate,rate*align,align,bits)
    body=b'WAVE'+b'fmt '+struct.pack('<I',len(fmt))+fmt+b'LIST'+struct.pack('<I',4)+b'INFO'+b'data'+struct.pack('<I',len(out))+bytes(out)
    return b'RIFF'+struct.pack('<I',len(body))+body

def cab(n=2000,delay=40):
    """Synthetic decaying, sign-alternating 'cabinet-like' response with leading silence."""
    return [0.0]*delay+[0.9*math.exp(-i/60)*math.cos(i*0.7) for i in range(n)]

class CabIrTests(unittest.TestCase):
    def test_formats_channels_and_rates_have_calibrated_response_and_bounded_gain(self):
        x=cab()
        cases=[wav(x),wav(x,bits=24),wav(x,bits=32),wav(x,bits=32,tag=3),wav(x,bits=64,tag=3),
               wav([(v,v) for v in x],bits=24,channels=2),wav(x,rate=48000,bits=24),wav(x,bits=24,extensible=True),wav(x,rate=96000,bits=32,tag=3)]
        for data in cases:
            h,info=cab_ir.convert(data)
            self.assertEqual(len(h),128);self.assertLessEqual(sum(abs(v) for v in h),8.0)
            self.assertAlmostEqual(cab_ir.response_gain(h),1.0,delta=.0001)
            self.assertTrue(all(math.isfinite(v) for v in h))
            self.assertLessEqual(info['trimmed_leading_samples'],45)
            self.assertLess(max(range(128),key=lambda i:abs(h[i])),8)   # onset kept near the start
            self.assertLess(abs(h[-1]),1e-3)                             # faded out
    def test_identical_rate_matches_formats_and_resample_preserves_shape(self):
        x=cab()
        a,_=cab_ir.convert(wav(x,bits=32,tag=3));b,_=cab_ir.convert(wav(x,bits=24))
        self.assertLess(max(abs(p-q) for p,q in zip(a,b)),1e-5)
        y=cab_ir.resample([math.sin(2*math.pi*1000*i/48000) for i in range(4800)],48000)
        self.assertEqual(len(y),4410)
        mid=y[1000:3000];self.assertAlmostEqual(max(mid),1.0,delta=0.02)
        self.assertEqual(cab_ir.resample([1.0,2.0],44100),[1.0,2.0])
    def test_short_ir_is_zero_padded_and_header_compiles_in_c(self):
        h,info=cab_ir.convert(wav([0.0,0.5,-0.25,0.125]),'tiny.wav')
        self.assertEqual(h[4:],[0.0]*124);self.assertAlmostEqual(sum(abs(v) for v in h),1.0,places=6)
        with tempfile.TemporaryDirectory() as d:
            inc=Path(d)/'tests'/'fixtures';inc.mkdir(parents=True)
            (inc/'diagnostic_ir.h').write_text(cab_ir.header(h,info))
            src=Path(d)/'ir_host.c'
            src.write_text('#include "tests/fixtures/diagnostic_ir.h"\n#ifdef _WIN32\n#define API __declspec(dllexport)\n#else\n#define API\n#endif\n'
                           'API float ir_at(unsigned i){return frost_test_ir[i];}\nAPI unsigned ir_len(void){return sizeof(frost_test_ir)/sizeof(frost_test_ir[0]);}\n')
            lib=ct.CDLL(str(build(ROOT/('build/cab_ir_host'+('.dll' if os.name=='nt' else '.so')),src,includes=[Path(d)])))
            lib.ir_at.restype=ct.c_float
            self.assertEqual(lib.ir_len(),128)
            for i in range(128):self.assertEqual(lib.ir_at(i),struct.unpack('<f',struct.pack('<f',h[i]))[0])
        self.assertIn('tiny.wav',cab_ir.header(h,info).splitlines()[0])
    def test_bad_files_rejected(self):
        bad=[b'',b'RIFF\x00\x00\x00\x00WAVX',wav([0.0]*100),wav([float('nan')]*4,bits=32,tag=3),wav([0.1]*4,bits=8),
             wav([0.1]*(44100*10+1)),wav([0.1]*4)[:-3]]
        for data in bad:
            with self.subTest(n=len(data)),self.assertRaises(ValueError):cab_ir.convert(data)
    def test_stage_rejects_bad_ir_before_touching_stage(self):
        from prepare_gp5 import stage
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'bad.wav';p.write_bytes(b'nope')
            with self.assertRaises(ValueError):stage(Path(d)/'no-platform',Path(d)/'stage',audio_input='adc',audio_output='line-test',cab_ir=p)
            self.assertFalse((Path(d)/'stage').exists())

if __name__=='__main__':unittest.main()
