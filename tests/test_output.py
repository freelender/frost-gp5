import ctypes as ct
import math
import os
from pathlib import Path
import random
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import output_meter

def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def request(token=1,version=1):return struct.pack('<HHII',0x7f35,12,version,token)
def decode(data):
    cmd,n,*v=struct.unpack('<HH11I',data)
    assert (cmd,n)==(0x7f35,48)
    return dict(version=v[0],token=v[1],status=v[2],frames=v[3],peak=struct.unpack('<f',struct.pack('<I',v[4]))[0],outside=v[5],rails=v[6],invalid=v[7],faults=v[8],mute=v[9],source=v[10])

class OutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/output.dll' if os.name=='nt' else 'build/output.so'),ROOT/'tests/output_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.output_sample.argtypes=[ct.c_uint32];c.output_sample.restype=ct.c_int32
        c.output_read.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.output_set_counts.argtypes=[ct.c_uint32]
    def setUp(self):self.lib.protocol_init()
    def send(self,p=None):
        if p is None:p=request()
        a=(ct.c_uint8*len(p))(*p)
        self.assertEqual(self.lib.protocol_send(0x7f35,a,len(p)),1)
        self.assertEqual(self.lib.protocol_reply_bytes(),48)
        return decode(bytes(self.lib.protocol_reply(i) for i in range(48)))
    def test_exact_conversion_boundaries_and_separate_unity_rail_counts(self):
        c=self.lib
        for raw,q in [(0,0),(0x80000000,0),(1,0),(0x80000001,0),
                (bits(.5),1073741824),(bits(-.5),-1073741824),
                (0x3f7ffffe,2147483392),(0x3f7fffff,2147483520),
                (bits(1.),2147483520),(bits(-1.),-2147483648),
                (bits(1.00001),2147483520),(bits(-1.00001),-2147483648),
                (bits(16.),2147483520),(bits(-16.),-2147483648)]:
            self.assertEqual(c.output_sample(raw),q,hex(raw))
        r=self.send();self.assertEqual((r['frames'],r['outside'],r['rails'],r['invalid'],r['peak']),(14,4,7,0,16.))
    def test_invalid_boundary_values_are_silent_and_do_not_poison_peak(self):
        c=self.lib
        for raw in (0x7fc00000,0xffc00000,0x7f800000,0xff800000,bits(16.001),bits(-16.001)):
            self.assertEqual(c.output_sample(raw),0)
        r=self.send();self.assertEqual((r['frames'],r['invalid'],r['outside'],r['rails'],r['peak']),(6,6,0,0,0.))
        c.output_sample(bits(.25));self.assertEqual(self.send()['peak'],.25)
    def test_random_binary32_conversion_matches_previous_adapter(self):
        c=self.lib;rng=random.Random(4521)
        for raw in [rng.getrandbits(32) for _ in range(12000)]:
            y=struct.unpack('<f',struct.pack('<I',raw))[0]
            if not math.isfinite(y) or abs(y)>16:q=0
            elif y>=struct.unpack('<f',bytes.fromhex('ffff7f3f'))[0]:q=2147483520
            elif y<=-1:q=-2147483648
            else:q=int(y*2147483648.)
            self.assertEqual(c.output_sample(raw),q,hex(raw))
    def test_counter_saturation_peak_and_init(self):
        c=self.lib;c.output_set_counts(0xfffffffe)
        for raw in (bits(2.),bits(-3.),0x7fc00000,0x7f800000):c.output_sample(raw)
        r=self.send();self.assertEqual((r['frames'],r['outside'],r['rails'],r['invalid']),(0xffffffff,)*4)
        self.assertEqual(r['peak'],3.);c.protocol_init();r=self.send()
        self.assertEqual((r['frames'],r['outside'],r['rails'],r['invalid'],r['peak']),(0,0,0,0,0.))
    def test_reads_are_pure_retriable_and_do_not_consume_mutation_revision(self):
        c=self.lib;c.output_sample(bits(1.25));before=(c.protocol_revision(),c.protocol_commands(),c.protocol_rejected())
        a=self.send();self.assertEqual(self.send(),a)
        self.assertEqual(before,(c.protocol_revision(),c.protocol_commands(),c.protocol_rejected()))
        self.assertEqual((a['version'],a['mute'],a['source']),(1,1,1))
        def exchange(p):
            self.send(p)
            return bytes(c.protocol_reply(i) for i in range(48))
        observation=output_meter.read(exchange,74)
        self.assertEqual((observation['peak'],observation['hard_muted'],observation['source']),(1.25,True,'synthetic'))
        raw=exchange(request(74))
        for offset,value in [(0,0),(4,2),(8,75),(12,2),(20,bits(float('nan'))),(24,2),(40,2),(44,0)]:
            corrupt=bytearray(raw);struct.pack_into('<I',corrupt,offset,value)
            with self.assertRaises(ValueError):output_meter.decode(corrupt,74)
        for token in (0,False,-1,0x100000000):
            with self.assertRaises(ValueError):output_meter.request(token)
        with self.assertRaises(ValueError):output_meter.decode(raw[:-1],74)

    def test_malformed_and_unaligned_inplace_readback(self):
        for p,status in [(request()[:-1],2),(request(version=2),3),(request(token=0),4),(b'\0'*12,2),(request()+b'\0',2)]:
            r=self.send(p);self.assertEqual(r['status'],status)
            self.assertTrue(all(r[k]==0 for k in ('frames','peak','outside','rails','invalid','faults','mute','source')))
        p=request(71);a=(ct.c_uint8*49)();ptr=ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8))
        for i,b in enumerate(p):ptr[i]=b
        self.lib.output_read(ptr,12);self.assertEqual(decode(bytes(a)[1:]),self.send(p))
    def test_actual_muted_adapter_counts_pre_mute_levels_and_ignores_adc(self):
        c=self.lib;self.assertEqual(c.output_audio(20,123),1);a=self.send()
        self.assertEqual((a['frames'],a['invalid'],a['faults']),(1280,0,0));self.assertGreater(a['peak'],0)
        c.protocol_init();self.assertEqual(c.output_audio(20,0x7fffffff),1);self.assertEqual(self.send(),a)
        c.protocol_init();c.output_drive();self.assertEqual(c.output_audio(20,0),1);r=self.send()
        self.assertGreater(r['outside'],0);self.assertGreater(r['rails'],0);self.assertGreater(r['peak'],1.)
    def test_chain_fault_is_distinct_from_invalid_converter_sample_and_recovers(self):
        c=self.lib;c.output_fault();self.assertEqual(c.output_audio(1,0),1);r=self.send()
        self.assertEqual((r['frames'],r['faults'],r['invalid']),(64,1,0))
        self.assertEqual(c.output_audio(4,0),1);self.assertEqual(self.send()['faults'],1)

if __name__=='__main__':unittest.main()
