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
import link_tx_meter
from test_link_rx import frame

class LinkTxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/link_tx'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/tx_host.c',includes=[ROOT/'tests/stubs'])))
        for name in ('tx_frame','tx_platform_frame','tx_feed','tx_inplace'):
            getattr(cls.c,name).argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.tx_value.restype=ct.c_uint32
    def setUp(self):self.c.tx_init()
    def enqueue(self,p,platform=False):
        a=(ct.c_uint8*len(p))(*p)
        return self.c.tx_platform_frame(a,len(p)) if platform else self.c.tx_frame(a,len(p))
    def wire(self):return bytes(self.c.tx_byte(i) for i in range(self.c.tx_value(9)))
    def drain(self):
        while self.c.tx_value(0):self.assertGreater(self.c.tx_service(0),0)
    def exchange(self,p):
        a=(ct.c_uint8*len(p))(*p);self.c.protocol_send(0x7f3a,a,len(p))
        return bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
    def test_actual_platform_frames_and_literal_acks_match_original_wire(self):
        p=bytes.fromhex('307f2c00')+bytes(range(40))
        self.enqueue(p,platform=True);self.c.tx_platform_ack();self.enqueue(b'hello',platform=True)
        self.assertEqual(self.wire(),b'');self.drain()
        self.assertEqual(self.wire(),frame(p)+bytes.fromhex('01040014')+frame(b'hello'))
        self.assertEqual((self.c.tx_value(10),self.c.tx_value(11)),(2,1))
    def test_full_or_stuck_fifo_never_spins_and_budget_is_strict(self):
        self.enqueue(bytes(100))
        for used in (16,31,16):
            self.assertEqual(self.c.tx_service(used),0)
            self.assertEqual((self.c.tx_value(7),self.c.tx_value(8)),(1,0))
        self.assertEqual(self.c.tx_service(15),1)
        self.assertEqual((self.c.tx_value(7),self.c.tx_value(8)),(2,1))
        self.assertEqual(self.c.tx_service(0),16)
        self.assertEqual((self.c.tx_value(7),self.c.tx_value(8)),(16,16))
    def test_capacity_failure_and_oversize_do_not_emit_partial_frames(self):
        p=bytes(508);self.assertEqual(self.enqueue(p),1);self.assertEqual(self.enqueue(p),1)
        self.c.tx_platform_ack();self.assertEqual(self.enqueue(bytes(509)),0)
        self.assertEqual(self.c.tx_frame(None,1),0)
        self.assertEqual(self.c.tx_value(0),1024);self.assertEqual(self.c.tx_value(2),3)
        self.drain();self.assertEqual(self.wire(),frame(p)*2)
    def test_ring_wrap_preserves_wire_order_under_backpressure(self):
        rng=random.Random(730);expected=b''
        for i in range(50):
            p=bytes(rng.randrange(256) for _ in range(rng.randrange(1,101)))
            while 1024-self.c.tx_value(0)<len(p)+4:self.c.tx_service(0)
            self.assertEqual(self.enqueue(p),1);expected+=frame(p)
            if i%2:self.c.tx_service(0)
        self.drain();self.assertEqual(self.wire(),expected)
    def test_reservation_fits_maximum_frame_plus_ack_and_defers_mutation(self):
        self.enqueue(bytes(508));self.enqueue(bytes(24)) #540queued;484free <516reserved.
        p=struct.pack('<HH8I',0x7f40,36,1,99,1,1,0,0,0,0x3f000000)
        raw=frame(p);self.c.tx_feed((ct.c_uint8*len(raw))(*raw),len(raw))
        for _ in range(3):self.c.tx_rx_pass()
        self.assertEqual((self.c.tx_value(12),self.c.protocol_commands(),self.c.protocol_revision()),(0,0,1))
        self.assertEqual(self.c.tx_value(13),len(raw))
        self.c.tx_service(0);self.c.tx_service(0);self.c.tx_rx_pass()
        self.assertEqual((self.c.tx_value(12),self.c.protocol_commands(),self.c.protocol_revision()),(1,1,2))
        self.assertEqual(self.c.tx_value(13),0)
        reply=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        self.drain();self.assertEqual(self.wire(),frame(bytes(508))+frame(bytes(24))+frame(reply))
    def test_reserved_capacity_accepts_largest_reply_and_following_ack(self):
        self.enqueue(bytes(504));self.assertEqual(self.c.tx_ready(1),1)
        self.assertEqual(self.enqueue(bytes(508)),1);self.c.tx_platform_ack()
        self.assertEqual((self.c.tx_value(0),self.c.tx_value(2)),(1024,0))
        self.drain();self.assertEqual(self.wire(),frame(bytes(504))+frame(bytes(508))+bytes.fromhex('01040014'))
    def test_snapshot_is_read_only_and_unaligned_inplace_supported(self):
        self.enqueue(bytes(100));before=[self.c.tx_value(i) for i in range(7)]
        p=link_tx_meter.request(23);raw=self.exchange(p);d=link_tx_meter.decode(raw,23)
        self.assertEqual(d['queued_bytes'],104);self.assertEqual(d['enqueued_frames'],1)
        self.assertEqual([self.c.tx_value(i) for i in range(7)],before)
        a=(ct.c_uint8*50)(*[0xcc]*50)
        for i,b in enumerate(p):a[i+1]=b
        self.c.tx_inplace(ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8)),12)
        self.assertEqual(bytes(a[1:49]),raw);self.assertEqual((a[0],a[49]),(0xcc,0xcc))
    def test_counter_saturation_does_not_prevent_transmission_or_deferral(self):
        self.c.tx_saturate();self.enqueue(bytes(508));self.c.tx_platform_ack();self.c.tx_ready(1)
        self.c.tx_service(16);self.c.tx_service(0)
        self.assertEqual(self.c.tx_value(0),500)
        for field in (1,3,4,6):self.assertEqual(self.c.tx_value(field),0xffffffff)
        self.enqueue(bytes(509));self.assertEqual(self.c.tx_value(2),0xffffffff)
    def test_malformed_requests_and_corrupt_telemetry_are_rejected(self):
        p=link_tx_meter.request(3)
        for bad in (p[:-1],p+b'\0',p[:4]+struct.pack('<II',2,3),p[:8]+bytes(4)):
            v=struct.unpack('<HH11I',self.exchange(bad));self.assertNotEqual(v[4],0);self.assertEqual(v[5:],(0,)*8)
        good=self.exchange(p)
        for offset,value in ((0,0),(4,2),(8,4),(16,1025),(36,1025),(44,3)):
            b=bytearray(good);struct.pack_into('<I',b,offset,value)
            with self.assertRaises(ValueError):link_tx_meter.decode(b,3)
        for token in (0,True,-1,0x100000000):
            with self.assertRaises(ValueError):link_tx_meter.request(token)

if __name__=='__main__':unittest.main()
