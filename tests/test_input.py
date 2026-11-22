import ctypes as ct
import os
from pathlib import Path
import random
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import input_meter
import output_meter

class InputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.libs=[]
        for mode in ('synthetic','adc'):
            c=ct.CDLL(str(build(ROOT/('build/input-'+mode+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/input_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_AUDIO_ADC=1'] if mode=='adc' else [])))
            c.input_sample.argtypes=[ct.c_int32];c.input_sample.restype=ct.c_float
            c.input_audio.argtypes=[ct.POINTER(ct.c_int32),ct.c_uint]
            c.input_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
            c.input_counts.argtypes=[ct.c_uint32]
            c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
            cls.libs.append(c)
    def setUp(self):
        for c in self.libs:c.protocol_init()
    def exchange(self,c,p):
        a=(ct.c_uint8*len(p))(*p);cmd=int.from_bytes(p[:2],'little')
        self.assertEqual(c.protocol_send(cmd,a,len(p)),1)
        return bytes(c.protocol_reply(i) for i in range(c.protocol_reply_bytes()))
    def read(self,c):return input_meter.read(lambda p:self.exchange(c,p))
    def test_q31_conversion_edges_and_random_values_match_binary32_reference(self):
        c=self.libs[1];rng=random.Random(3641)
        values=[-2147483648,-2147483647,-1073741824,-1,0,1,1073741824,2147483646,2147483647]
        values.extend(rng.randint(-2147483648,2147483647) for _ in range(10000))
        for q in values:
            rounded=struct.unpack('<f',struct.pack('<f',q))[0]
            self.assertEqual(c.input_sample(q),rounded/2147483648.)
        r=self.read(c);self.assertEqual((r['minimum'],r['maximum'],r['absolute_peak']),(-2147483648,2147483647,2147483648))
        self.assertEqual(r['raw_rail_frames'],2)
    def test_source_selection_and_actual_callback_keep_literal_stereo_silence(self):
        for mode,c in zip(('synthetic','platform-mono'),self.libs):
            c.input_empty();samples=(ct.c_int32*64)(*([536870912,-536870912]*32))
            self.assertEqual(c.input_audio(samples,2),1)
            r=self.read(c);out=output_meter.read(lambda p:self.exchange(c,p))
            self.assertEqual((r['frames'],r['minimum'],r['maximum'],r['nonzero_frames']),(128,-536870912,536870912,128))
            self.assertEqual(r['chain_source'],mode);self.assertEqual(out['source'],mode)
            self.assertEqual(out['peak'],.25 if mode=='platform-mono' else ct.c_float(.128).value)
            self.assertTrue(r['hard_muted'] and out['hard_muted'])
    def test_adc_zero_is_not_replaced_by_synthetic_stimulus(self):
        c=self.libs[1];c.input_empty();self.assertEqual(c.input_audio((ct.c_int32*64)(),8),1)
        self.assertEqual(self.read(c)['nonzero_frames'],0)
        self.assertEqual(output_meter.read(lambda p:self.exchange(c,p))['peak'],0.)
    def test_adc_extremes_stay_muted_with_pre_pcm_rails_visible(self):
        c=self.libs[1];c.input_empty();samples=(ct.c_int32*64)(*([-2147483648,2147483647]*32))
        self.assertEqual(c.input_audio(samples,1),1);r=self.read(c)
        out=output_meter.read(lambda p:self.exchange(c,p))
        self.assertEqual((r['raw_rail_frames'],out['pcm_rail_frames'],out['invalid_boundary_frames'],out['chain_faults']),(64,64,0,0))
    def test_counters_saturate_and_init_resets_all_observations(self):
        c=self.libs[1];c.input_counts(0xfffffffe)
        for q in (-2147483648,2147483647):c.input_sample(q)
        r=self.read(c);self.assertEqual((r['frames'],r['raw_rail_frames'],r['nonzero_frames']),(0xffffffff,)*3)
        c.protocol_init();r=self.read(c)
        self.assertEqual((r['frames'],r['minimum'],r['maximum'],r['absolute_peak']),(0,0,0,0))
    def test_reads_are_pure_and_malformed_unaligned_inplace_frames_are_bounded(self):
        c=self.libs[1];c.input_sample(-123)
        before=(c.protocol_revision(),c.protocol_commands(),c.protocol_rejected());p=input_meter.request(91)
        first=self.exchange(c,p);self.assertEqual(self.exchange(c,p),first)
        self.assertEqual(before,(c.protocol_revision(),c.protocol_commands(),c.protocol_rejected()))
        a=(ct.c_uint8*49)();ptr=ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8))
        for i,b in enumerate(p):ptr[i]=b
        c.input_inplace(ptr,12);self.assertEqual(bytes(a)[1:],first)
        for p,status in [(p[:-1],2),(struct.pack('<HHII',0x7f36,12,2,1),3),(struct.pack('<HHII',0x7f36,12,1,0),4),(p+b'0',2)]:
            raw=self.exchange(c,p);v=struct.unpack('<HH11I',raw);self.assertEqual(v[4],status);self.assertFalse(any(v[5:]))
    def test_decoder_rejects_corrupt_ranges_counters_source_and_tokens(self):
        c=self.libs[1];c.input_sample(123);raw=self.exchange(c,input_meter.request())
        for offset,value in [(8,2),(20,124),(28,1),(32,2),(40,2),(44,0)]:
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):input_meter.decode(bad,1)
        with self.assertRaises(ValueError):input_meter.decode(raw[:-1],1)
        for token in (True,0,-1,1<<32):
            with self.assertRaises(ValueError):input_meter.request(token)

if __name__=='__main__':unittest.main()
