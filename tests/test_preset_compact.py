"""Stage 2 compact presets (version 3, docs/Build%20Firmware.md): capture and upload
of the 77-word body without IR samples through the real 0x7F44/0x7F43
handlers. The DSP keeps the IRs its IR workspaces hold; the recall is otherwise
the ordinary atomic preset recall."""
import ctypes as ct
import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from preset_download import request as read_request
from preset_wire import request as upload_request,decode as upload_decode,checksum,BEGIN,WRITE,COMMIT
import os
import test_preset_read as R
from host import build
from test_native_presets import Snapshot
bits=R.bits
DESCRIBE_COMPACT,READ_COMPACT=2,3
class CompactPresetTests(unittest.TestCase):
    exchange=R.PresetReadTests.exchange;native=R.PresetReadTests.native;audio=R.PresetReadTests.audio
    body=R.PresetReadTests.body;upload=R.PresetReadTests.upload
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/preset_compact.dll' if os.name=='nt' else 'build/preset_compact.so'),
            ROOT/'tests/preset_read_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.read_native_snapshot.argtypes=[ct.POINTER(Snapshot)]
    def setUp(self):self.lib.protocol_init()
    def body_b(self):
        w=self.body();w[27]=bits(0.5)                                      # slot 0 parameter 0
        for i in range(47,47+512):w[i]=bits(((i*13)%71-35)/90001.)          # a different first IR
        return w
    def read_reply(self,p):
        r=self.exchange(p);cmd,n,version,token,status,revision,index,count,*data=struct.unpack('<HH11I',r)
        return status,revision,index,count,data
    def capture_compact(self,token=5000):
        st,rev,_,_,d=self.read_reply(read_request(token,DESCRIBE_COMPACT));self.assertEqual(st,0)
        crc,total=d[0],d[1];self.assertEqual(total,77);words=[]
        for i in range(0,total,5):
            token+=1;st,_,index,count,d=self.read_reply(read_request(token,READ_COMPACT,rev,i))
            self.assertEqual((st,index,count),(0,i,min(5,total-i)));words+=d[:count]
        self.assertEqual(checksum(words),crc);return rev,words
    def upload_compact(self,w,session=2,revision=None,token=2000,expect=15):
        revision=revision or self.capture_compact(token=token+500)[0];crc=checksum(w)
        self.assertEqual(upload_decode(self.exchange(upload_request(token,BEGIN,session,revision,data=[crc])))['status'],0)
        for i in range(0,len(w),5):
            token+=1;self.assertEqual(upload_decode(self.exchange(upload_request(token,WRITE,session,revision,i,w[i:i+5])))['status'],0)
        token+=1;st=upload_decode(self.exchange(upload_request(token,COMMIT,session,revision,len(w),[crc])))['status']
        self.assertEqual(st,expect);return st
    def settle(self):
        for _ in range(8):self.audio(256)
    def test_compact_capture_is_the_full_body_without_ir_samples(self):
        self.upload(self.body());self.settle()
        full=self.native();rev,w=self.capture_compact()
        self.assertEqual(len(w),77);self.assertEqual(w[0],3);self.assertEqual(w[45:47],[0,0])
        self.assertEqual(tuple(w[1:45]),full[1:45])
        self.assertEqual(w[47:],[0]*30)                                    # version-1 body: no extended parameters
    def test_compact_recall_restores_settings_and_keeps_the_held_irs(self):
        self.upload(self.body());self.settle();full_a=self.native();rev,w=self.capture_compact()
        self.upload(self.body_b(),session=3,revision=rev,token=3000);self.settle();full_b=self.native()
        self.assertNotEqual(full_a[27],full_b[27])
        self.upload_compact(w,session=4,token=4000);self.settle();after=self.native()
        self.assertEqual(after[1:45],full_a[1:45])                         # settings from the compact preset
        self.assertEqual(after[45:47+1024],full_b[45:47+1024])             # IRs: the ones the DSP held
    def test_compact_body_with_ir_taps_is_refused_without_change(self):
        self.upload(self.body());self.settle();rev,w=self.capture_compact();before=self.native()
        bad=list(w);bad[45]=512;self.upload_compact(bad,expect=13);self.settle();self.assertEqual(self.native(),before)
if __name__=='__main__':unittest.main()
