import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import slot_load_meter as meter

class SlotLoadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/slot_load_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/slot_load_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.trace_record.argtypes=[ct.c_uint32]*5
        cls.c.slot_note.argtypes=[ct.c_uint32]*4
        cls.c.slot_clock.argtypes=[ct.c_uint32]
        cls.c.slot_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
    def setUp(self):self.c.protocol_init()
    def exchange(self,p):
        a=(ct.c_uint8*len(p))(*p)
        self.assertEqual(self.c.protocol_send(meter.COMMAND,a,len(p)),1)
        return bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
    def read(self,i=0):return meter.decode(self.exchange(meter.request(i,7)),i,7)
    def block(self):self.c.trace_record(0,10,1000,1100,0)
    def test_actual_guarded_hooks_physical_ids_clock_wrap_and_fallback(self):
        self.c.slot_clock(0xffffffd0);self.c.slot_reverse();self.c.slot_guarded(0);self.block()
        self.c.slot_guarded(1);self.block()
        for _ in range(254):self.block() # skipped chain (e.g. latch) has no slot measurements
        d=self.read()
        self.assertEqual((d['profiled_blocks'],d['fallback_blocks'],d['slot_mask']),(1,1,63))
        self.assertEqual(d['slot_max_cycles'],[100]*6)
        self.assertEqual(d['slot_types'],[0,1,2,3,255,255])
    def test_maxima_reset_ring_rollover_mixed_types_and_load_alignment(self):
        import load_meter
        for w in range(18):
            for b in range(256):
                self.c.slot_profile_block(0)
                self.c.slot_note(4,9 if b<128 else 6,0,200+b+w)
                self.c.slot_note(1,2,0xfffffffe,3)
                self.block()
        d=self.read();self.assertEqual((d['seq'],d['slot_max_cycles'][4],d['slot_types'][4]),(18,472,0xffffffff))
        self.assertEqual(d['slot_max_cycles'][1],5)
        self.assertEqual(self.read(7)['seq'],11)   # 30a: 8 retained windows (was 16: read(15) -> seq 3)
        with self.assertRaises(ValueError):self.read(8)   # beyond the ring: rejected, not stale data
        p=load_meter.request(0,7);a=(ct.c_uint8*len(p))(*p);self.c.protocol_send(load_meter.COMMAND,a,len(p))
        raw=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        self.assertEqual(load_meter.decode(raw,0,7)['seq'],d['seq'])
        for _ in range(256):self.block()
        self.assertEqual(self.read()['slot_mask'],0)
        self.c.protocol_init();self.assertFalse(self.read()['available'])
    def test_bad_frames_read_only_and_unaligned_inplace(self):
        self.assertFalse(self.read()['available'])
        for _ in range(256):self.block()
        before=tuple(getattr(self.c,f)() for f in ('protocol_revision','protocol_commands','protocol_rejected'))
        good=meter.request(0,7)
        bads=[good[:i] for i in range(16)]+[good+b'\0']
        for offset,value in ((4,2),(8,0),(12,16)):
            p=bytearray(good);struct.pack_into('<I',p,offset,value);bads.append(p)
        for p in bads:
            words=struct.unpack('<HH20I',self.exchange(p))
            self.assertNotEqual(words[4],0);self.assertFalse(any(words[5:]))
        expected=self.exchange(good);buf=(ct.c_uint8*86)(*[0xcc]*86)
        for i,b in enumerate(good):buf[i+1]=b
        self.c.slot_inplace(ct.cast(ct.byref(buf,1),ct.POINTER(ct.c_uint8)),16)
        self.assertEqual(bytes(buf[1:85]),expected);self.assertEqual((buf[0],buf[85]),(0xcc,0xcc))
        self.assertEqual(before,tuple(getattr(self.c,f)() for f in ('protocol_revision','protocol_commands','protocol_rejected')))
    def test_decoder_rejects_corrupt_replies(self):
        for args in ((-1,1),(16,1),(True,1),(0,0),(0,True)):
            with self.assertRaises(ValueError):meter.request(*args)
        for _ in range(256):self.block()
        good=self.exchange(meter.request(0,7))
        for pos,value in ((0,0),(1,48),(2,2),(3,8),(4,1),(5,0),(6,255),(7,257),(9,64),(10,1)):
            words=list(struct.unpack('<HH20I',good));words[pos]=value
            with self.assertRaises(ValueError):meter.decode(struct.pack('<HH20I',*words),0,7)
        with self.assertRaises(ValueError):meter.decode(good[:-1],0,7)
