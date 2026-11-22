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
import link_rx_meter

def crc(data):
    c=0
    for b in data:
        c^=b
        for _ in range(8):c=((c<<1)^7)&255 if c&128 else (c<<1)&255
    return c

def frame(payload):return bytes((crc(payload),))+struct.pack('<H',len(payload)+4)+b'\x01'+payload

def meter(token=1):return struct.pack('<HHII',0x7f37,12,1,token)

class LinkRxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/link_rx.dll' if os.name=='nt' else 'build/link_rx.so'),ROOT/'tests/link_rx_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.rx_value.restype=ct.c_uint32;c.rx_feed.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.c_uint32];c.rx_poll.argtypes=[ct.c_uint32]
        c.rx_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint];c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
    def setUp(self):self.lib.rx_init()
    def feed(self,data,now=100):
        self.lib.rx_feed((ct.c_uint8*len(data))(*data),len(data),now)
    def drain(self,now=100,passes=80):
        result=[]
        for _ in range(passes):
            n=self.lib.rx_poll(now)
            if n:result.append(bytes(self.lib.rx_last(i) for i in range(n)))
            if not self.lib.rx_value(0):break
        self.assertTrue(self.lib.rx_guard());return result
    def test_valid_frames_all_chunk_partitions_dispatch_exactly_once(self):
        payloads=[bytes.fromhex('307f0400'),meter(5),bytes.fromhex('0060080000000000')]
        stream=b''.join(map(frame,payloads))
        for chunk in (1,3,7,16,31,512):
            self.lib.rx_init();got=[]
            for i in range(0,len(stream),chunk):self.feed(stream[i:i+chunk]);got+=self.drain(passes=2)
            got+=self.drain();self.assertEqual(got,payloads);self.assertEqual(self.lib.rx_value(1),3)
    def test_bad_crc_resynchronizes_to_embedded_following_valid_frame(self):
        good=frame(bytes.fromhex('307f0400'));bad=bytes((0,16,0,1))+b'\xff'*4+good
        self.assertNotEqual(crc(bad[4:]),bad[0])
        # Previous parser consumes all16bytes after CRC failure, swallowing good.
        self.feed(bad);self.assertEqual(self.drain(),[bytes.fromhex('307f0400')]);self.assertGreater(self.lib.rx_value(2),0)
    def test_incomplete_plausible_header_expires_and_fresh_request_recovers(self):
        self.feed(bytes((0,0,1,1))+b'\xff'*5);self.assertEqual(self.drain(),[])
        self.assertEqual(self.lib.rx_value(0),9) # Old parser has no idle expiry.
        gap=self.lib.rx_gap();self.assertEqual(self.lib.rx_poll(100+gap-1),0);self.assertEqual(self.lib.rx_value(0),9)
        self.lib.rx_poll(100+gap);self.assertEqual(self.lib.rx_value(0),0);self.assertEqual(self.lib.rx_value(4),1)
        self.feed(frame(meter()),100+gap+1);self.assertEqual(self.drain(100+gap+1),[meter()])
    def test_expiry_wraparound_and_new_byte_does_not_extend_stale_prefix(self):
        start=0xfffffff0;self.feed(b'\0\0\1\1',start);now=(start+self.lib.rx_gap())&0xffffffff
        self.feed(frame(meter()),now);self.assertEqual(self.drain(now),[meter()]);self.assertEqual(self.lib.rx_value(4),1)
    def test_maximum_frame_crc_work_is_incremental_and_buffers_guarded(self):
        payload=b'\x40\x7f'+bytes(506);self.feed(frame(payload))
        for step in range(7):
            self.assertEqual(self.lib.rx_poll(100),0);self.assertEqual(self.lib.rx_value(7),64*(step+1))
        self.assertEqual(self.lib.rx_poll(100),508);self.assertEqual(self.lib.rx_value(0),0);self.assertTrue(self.lib.rx_guard())
    def test_garbage_scan_is_bounded_and_overflow_fails_closed(self):
        self.feed(b'\xff'*512);self.lib.rx_poll(100);self.assertEqual(self.lib.rx_value(0),496)
        self.lib.rx_init();self.feed(b'\xff'*513);self.assertEqual(self.lib.rx_value(5),1)
        self.assertEqual((self.lib.rx_value(0),self.lib.rx_value(6)),(1,512));self.assertTrue(self.lib.rx_guard())
        self.feed(frame(meter()));self.assertEqual(self.drain(),[meter()])
    def test_corrupt_lengths_noise_and_dropped_frame_cannot_mutate_settings(self):
        before=self.lib.protocol_revision()
        self.feed(b'\x00\xff\xff\x01'+b'\xff'*20+frame(meter()))
        self.assertEqual(self.drain(),[meter()]);self.assertEqual(self.lib.protocol_revision(),before)
        self.assertEqual(self.lib.protocol_commands(),0)
    def test_saturating_counters_and_init_reset(self):
        self.lib.rx_saturate();self.feed(frame(meter()));self.drain()
        for i in (1,2,3,4,5):self.assertEqual(self.lib.rx_value(i),0xffffffff)
        self.lib.rx_init();self.assertEqual([self.lib.rx_value(i) for i in range(9)],[0]*9)
    def test_real_diagnostic_read_and_unaligned_inplace_error_payloads(self):
        self.feed(frame(meter(41)));self.drain()
        raw=bytes(self.lib.protocol_reply(i) for i in range(48));v=struct.unpack('<HH11I',raw)
        self.assertEqual(v[:5],(0x7f37,48,1,41,0));self.assertEqual(v[-1],1)
        decoded=link_rx_meter.decode(raw,41);self.assertEqual(decoded['buffered'],16)
        for offset,value in [(8,42),(16,513),(40,513),(44,0)]:
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):link_rx_meter.decode(bad,41)
        a=(ct.c_uint8*49)();ptr=ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8))
        for i,b in enumerate(meter(42)):ptr[i]=b
        self.lib.rx_inplace(ptr,12);self.assertEqual(struct.unpack_from('<I',bytes(a),9)[0],42)
        for p,status in [(meter()[:-1],2),(struct.pack('<HHII',0x7f37,12,2,1),3),(meter(0),4)]:
            a=(ct.c_uint8*48)(*p);self.lib.rx_inplace(a,len(p));v=struct.unpack('<HH11I',bytes(a));self.assertEqual(v[4],status);self.assertFalse(any(v[5:]))
    def test_random_valid_payloads_survive_fragmentation_and_reused_storage(self):
        rng=random.Random(3701)
        for _ in range(60):
            self.lib.rx_init();p=bytes.fromhex('307f')+bytes(rng.randrange(256) for _ in range(rng.randrange(2,400)))
            encoded=frame(p);got=[]
            while encoded:
                n=rng.randrange(1,32);self.feed(encoded[:n]);encoded=encoded[n:];got+=self.drain(passes=1)
            got+=self.drain();self.assertEqual(got,[p])

if __name__=='__main__':unittest.main()
