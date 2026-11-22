"""Bounded stack observation, protocol rejection and reset integration checks."""
import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from stack_meter import request,decode,read,BOTTOM,TOP

class StackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/stack_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/stack_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.stack_read.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        cls.c.stack_inplace.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.stack_word.restype=ct.c_uint32
    def setUp(self):self.c.stack_init()
    def exchange(self,p,sp=TOP):
        src=(ct.c_uint8*len(p))(*p);out=(ct.c_uint8*50)(*[0xcc]*50)
        self.c.stack_read(sp,src,len(p),ct.cast(ct.byref(out,1),ct.POINTER(ct.c_uint8)))
        self.assertEqual((out[0],out[49]),(0xcc,0xcc));self.assertEqual(self.c.stack_guard(),1)
        return bytes(out[1:49])
    def test_watermark_tracks_deepest_written_word_without_repainting(self):
        self.assertEqual(read(self.exchange)['touched_bytes'],0)
        self.c.stack_touch(1000)
        self.assertEqual(read(self.exchange)['touched_bytes'],96)
        self.c.stack_touch(800)
        for _ in range(3):
            v=read(self.exchange);self.assertEqual((v['touched_bytes'],v['untouched_prefix_bytes']),(896,3200))
        self.assertEqual(self.c.stack_word(800),0)
        self.assertEqual(self.c.stack_word(799),0xa55aa55a)
        self.assertTrue(v['bottom_guard_intact']);self.assertTrue(v['hard_muted'])
        self.assertEqual(v['source'],'platform-mono')
    def test_bottom_guard_damage_is_reported_and_not_cleared(self):
        for word in range(4):
            self.c.stack_init();self.c.stack_touch(word);v=read(self.exchange)
            self.assertFalse(v['bottom_guard_intact']);self.assertEqual(v['untouched_prefix_bytes'],word*4)
            self.assertEqual(self.c.stack_word(word),0)
        self.c.stack_init();self.c.stack_touch(4)
        self.assertTrue(read(self.exchange)['bottom_guard_intact'])
    def test_current_msp_range_and_alignment_are_independent_of_watermark(self):
        for sp,valid in [(BOTTOM,True),(TOP,True),(TOP-8,True),(TOP-4,False),(BOTTOM-8,False),(TOP+8,False)]:
            v=decode(self.exchange(request(7),sp),7)
            self.assertEqual(v['msp_in_range'],valid);self.assertEqual(v['current_msp'],sp)
            self.assertEqual(v['touched_bytes'],0)
    def test_malformed_requests_return_status_without_observation(self):
        valid=request(7)
        cases=[valid[:n] for n in range(12)]+[valid+b'\0',b'\0'+valid[1:],valid[:2]+b'\x0b\0'+valid[4:],
               valid[:4]+struct.pack('<II',2,7),valid[:8]+bytes(4)]
        for p in cases:
            fields=struct.unpack('<HH11I',self.exchange(p));self.assertNotEqual(fields[4],0)
            self.assertEqual(fields[5:],(0,)*8)
        out=(ct.c_uint8*48)();self.c.stack_read(TOP,None,12,out)
        self.assertNotEqual(struct.unpack('<HH11I',bytes(out))[4],0)
    def test_unaligned_inplace_reply_and_maximum_token(self):
        p=request(0xffffffff);expected=self.exchange(p)
        buf=(ct.c_uint8*50)(*[0xcc]*50)
        for i,b in enumerate(p):buf[i+1]=b
        self.c.stack_inplace(TOP,ct.cast(ct.byref(buf,1),ct.POINTER(ct.c_uint8)),12)
        self.assertEqual(bytes(buf[1:49]),expected);self.assertEqual((buf[0],buf[49]),(0xcc,0xcc))
    def test_host_dispatch_rejects_unavailable_hardware_and_leaves_audio_muted(self):
        p=request(8);a=(ct.c_uint8*len(p))(*p)
        before=(self.c.protocol_revision(),self.c.protocol_commands(),self.c.protocol_rejected())
        self.assertEqual(self.c.protocol_send(0x7f38,a,len(p)),1)
        reply=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        self.assertNotEqual(struct.unpack('<HH11I',reply)[4],0)
        self.assertEqual(before,(self.c.protocol_revision(),self.c.protocol_commands(),self.c.protocol_rejected()))
        self.assertEqual(self.c.protocol_callback_has_no_reply(),1)
    def test_codec_rejects_corruption_and_invalid_tokens(self):
        for token in (0,-1,True,1.5,0x100000000):
            with self.assertRaises(ValueError):request(token)
        good=self.exchange(request(9))
        for field,value in [(0,0),(1,47),(2,2),(3,10),(4,1),(5,4),(6,4092),(8,BOTTOM+4),
                            (9,TOP+4),(10,7),(10,2),(11,2),(12,3)]:
            v=list(struct.unpack('<HH11I',good));v[field]=value
            with self.assertRaises(ValueError):decode(struct.pack('<HH11I',*v),9)
        for p in (good[:-1],good+b'\0'):
            with self.assertRaises(ValueError):decode(p,9)

if __name__=='__main__':unittest.main()
