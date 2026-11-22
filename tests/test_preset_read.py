import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from preset_download import download,request,decode,DESCRIBE,READ
from preset_wire import request as upload_request,decode as upload_decode,checksum,BEGIN,WRITE,COMMIT
from test_native_presets import Snapshot

def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]

class PresetReadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/preset_read.dll' if os.name=='nt' else 'build/preset_read.so'),
            ROOT/'tests/preset_read_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.read_native_snapshot.argtypes=[ct.POINTER(Snapshot)]
        c.read_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_current.restype=ct.c_float
    def setUp(self):self.lib.protocol_init()
    def exchange(self,p):
        a=(ct.c_uint8*len(p))(*p)
        self.assertEqual(self.lib.protocol_send(int.from_bytes(p[:2],'little'),a,len(p)),1)
        return bytes(self.lib.protocol_reply(i) for i in range(self.lib.protocol_reply_bytes()))
    def native(self):
        p=Snapshot();self.assertEqual(self.lib.read_native_snapshot(ct.byref(p)),1)
        return tuple([p.version,p.sample_rate,p.pre_count]+list(p.types)+list(p.order)+list(p.enabled)+list(p.bypass)+
            [bits(v) for row in p.parameters for v in row[:3]]+list(p.ir_taps)+[bits(v) for row in p.ir_coefficients for v in row]+
            ([bits(v) for row in p.parameters for v in row[3:]] if p.version==2 else []))
    def audio(self,n=256,stimulus=False):
        a=(ct.c_float*n)(*([((i*37)%101-50)/512 for i in range(n)] if stimulus else [0]*n));b=(ct.c_float*n)()
        self.lib.protocol_audio(a,b,n);return bytes(b)
    def body(self):
        p=[1.,0,0,20.,.3,.2,.8,.6,.25,.8,0,0,.8,.5,.3,0,0,0]
        return [1,44100,2,3,5,8,3,6,255,3,0,1,4,2,5]+[1]*5+[0]+[0,1,1,0,0,0]+[bits(v) for v in p]+[512,512]+[
            bits(-0. if i==0 else ((i*37)%127-63)/100003.) for i in range(512)]+[
            bits(((i*11)%89-44)/110003.) for i in range(512)]
    def upload(self,w,session=1,revision=1,token=1000):
        crc=checksum(w);r=upload_decode(self.exchange(upload_request(token,BEGIN,session,revision,data=[crc])))
        self.assertEqual(r['status'],0)
        for i in range(0,1071,5):
            token+=1;r=upload_decode(self.exchange(upload_request(token,WRITE,session,revision,i,w[i:i+5])))
            self.assertEqual(r['status'],0)
        token+=1;p=upload_request(token,COMMIT,session,revision,1071,[crc])
        self.assertEqual(upload_decode(self.exchange(p))['status'],15);return p
    def parameter_packet(self,value=1.25,revision=1,token=9000):
        return struct.pack('<HH8I',0x7f40,36,1,token,1,revision,0,0,0,bits(value))
    def test_default_capture_matches_native_targets_and_does_not_mutate(self):
        c=self.lib;before=self.native();calls=[]
        def exchange(p):calls.append(p);self.audio(37,True);return self.exchange(p)
        revision,w=download(exchange)
        self.assertEqual(revision,1);self.assertEqual(w,before);self.assertEqual(w,self.native())
        self.assertEqual(len(calls),217);self.assertEqual(c.protocol_commands(),0)
        self.assertEqual(c.protocol_revision(),1);self.assertEqual(c.transfer_state(),0)
        self.assertTrue(c.protocol_callback_has_no_reply())
    def test_two_full_float_ir_banks_and_reordered_slots_roundtrip_bit_exactly(self):
        c=self.lib;w=self.body();self.assertEqual(len(w),1071);self.upload(w);self.audio()
        revision,got=download(self.exchange)
        self.assertEqual(revision,2);self.assertEqual(got,tuple(w));self.assertEqual(got,self.native())
        self.assertEqual(got[47],0x80000000) # Preserve active signed-zero coefficient.
        first=self.audio(4096,True)
        self.upload(got,session=2,revision=2,token=2000);self.audio()
        self.assertEqual(self.audio(4096,True),first)
        self.assertEqual(download(self.exchange)[1],got);self.assertEqual(c.protocol_revision(),3)
    def test_capture_reads_target_parameters_and_enabled_before_smoothing(self):
        c=self.lib;self.exchange(self.parameter_packet(2.0));self.assertNotEqual(c.protocol_current(0,0),2.)
        _,w=download(self.exchange);self.assertEqual(w[27],bits(2.))
        p=struct.pack('<HH8I',0x7f40,36,1,9001,2,2,0,0,0xffffffff,0);self.exchange(p)
        _,w=download(self.exchange);self.assertEqual(w[15],0)
    def test_changed_revision_rejects_partial_capture_and_final_descriptor(self):
        for point in (2,80,217):
            self.lib.protocol_init();calls=0
            def exchange(p):
                nonlocal calls
                calls+=1
                if calls==point:self.exchange(self.parameter_packet())
                return self.exchange(p)
            with self.assertRaises(ValueError):download(exchange)
            revision,w=download(self.exchange);self.assertEqual(revision,2);self.assertEqual(w[27],bits(1.25))
    def test_busy_route_rejects_describe_and_read_without_data(self):
        route=struct.pack('<HH4I12B',0x7f42,32,1,9000,1,3,0,1,2,3,255,255,1,0,2,3,4,5)
        self.exchange(route)
        for p in (request(1,DESCRIBE),request(2,READ,revision=2)):
            r=decode(self.exchange(p));self.assertEqual(r['status'],12)
            self.assertEqual((r['index'],r['count'],r['data']),(0,0,(0,)*5))
        with self.assertRaises(ValueError):download(self.exchange)
        self.audio();revision,w=download(self.exchange);self.assertEqual(revision,2);self.assertEqual(w[9:15],(1,0,2,3,4,5))
    def test_download_does_not_touch_upload_staging_or_immutable_write_receipt(self):
        c=self.lib;w=self.body();crc=checksum(w)
        self.exchange(upload_request(1000,BEGIN,1,1,data=[crc]))
        p=upload_request(1001,WRITE,1,1,0,w[:5]);self.exchange(p)
        initial=self.native();self.assertEqual(download(self.exchange)[1],initial)
        self.assertEqual(c.transfer_next(),5);self.assertEqual(c.transfer_session(),1)
        self.assertEqual(upload_decode(self.exchange(p))['status'],1)
        for i in range(5,1071,5):self.exchange(upload_request(1002+i,WRITE,1,1,i,w[i:i+5]))
        r=upload_decode(self.exchange(upload_request(3000,COMMIT,1,1,1071,[crc])))
        self.assertEqual(r['status'],15);self.audio();self.assertEqual(download(self.exchange)[1],tuple(w))
    def test_read_does_not_evict_slot_or_commit_receipts_or_advance_token_floor(self):
        c=self.lib;p=self.parameter_packet();self.exchange(p);download(self.exchange,first_token=100000)
        self.assertEqual(struct.unpack_from('<I',self.exchange(p),12)[0],1)
        c.protocol_init();commit=self.upload(self.body());self.audio();download(self.exchange,first_token=100000)
        self.assertEqual(upload_decode(self.exchange(commit))['status'],1);self.assertEqual(c.protocol_revision(),2)
        r=upload_decode(self.exchange(upload_request(1,0)))
        self.assertLess(r['token_floor'],100000)
    def test_read_at_exhausted_revision_and_random_access_retry(self):
        c=self.lib;c.protocol_set_revision(0xffffffff);revision,w=download(self.exchange)
        self.assertEqual(revision,0xffffffff);self.assertEqual(w,self.native())
        for i in (1070,0,559,46,1070):
            p=request(2,READ,revision,i);a=self.exchange(p);self.assertEqual(self.exchange(p),a)
            r=decode(a);n=min(5,1071-i);self.assertEqual(r['data'][:n],w[i:i+n]);self.assertEqual(r['data'][n:],(0,)*(5-n))
    def test_malformed_frames_operations_revisions_offsets_and_inplace_unaligned(self):
        cases=[(request(1,DESCRIBE)[:-1],2),(request(1,DESCRIBE)+b'\0',2),
            (request(1,7),5),(request(0,DESCRIBE),4),(request(1,DESCRIBE,index=1),19),
            (request(1,READ,1,1071),19),(request(1,READ,0),7),(request(1,READ,2),7)]
        wrong=bytearray(request(1,DESCRIBE));wrong[4]=2;cases.append((wrong,3))
        for p,status in cases:
            r=decode(self.exchange(p));self.assertEqual(r['status'],status);self.assertEqual(r['data'],(0,)*5)
        p=request(55,DESCRIBE);storage=(ct.c_uint8*49)();ptr=ct.cast(ct.byref(storage,1),ct.POINTER(ct.c_uint8))
        for i,v in enumerate(p):ptr[i]=v
        self.assertEqual(self.lib.read_inplace(ptr,len(p)),48)
        self.assertEqual(bytes(storage)[1:],self.exchange(p))
    def test_collector_rejects_corrupt_headers_tokens_chunks_padding_crc_and_descriptor(self):
        for turn,offset,value in ((1,0,0),(1,8,900),(1,32,42),(2,20,1),(2,24,4),
                (2,28,99),(216,32,1),(217,28,123)):
            calls=0
            def exchange(p):
                nonlocal calls
                calls+=1;r=bytearray(self.exchange(p))
                if calls==turn:struct.pack_into('<I',r,offset,value)
                return r
            with self.assertRaises(ValueError):download(exchange)
        with self.assertRaises(ValueError):download(lambda p:b'')
        for token in (0,True,0xffffffff,1.5):
            with self.assertRaises(ValueError):download(self.exchange,token)
        revision,w=download(self.exchange,0xffffffff-216);self.assertEqual(revision,1);self.assertEqual(w,self.native())
    def test_reboot_mid_capture_with_same_revision_is_caught_by_crc(self):
        self.upload(self.body());self.audio();self.lib.protocol_set_revision(1);calls=0
        def exchange(p):
            nonlocal calls
            calls+=1
            if calls==100:self.lib.protocol_init()
            return self.exchange(p)
        with self.assertRaises(ValueError):download(exchange)
        self.assertEqual(download(self.exchange)[1],self.native())

if __name__=='__main__':unittest.main()
