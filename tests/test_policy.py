import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
import test_preset_read as read_tests
from preset_download import download

def packet(token=1,op=0,revision=1,slot=1,type=1,policy=0xffffffff):
    return struct.pack('<HH7I',0x7f45,32,1,token,op,revision,slot,type,policy)

class PolicyProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/policy.dll' if os.name=='nt' else 'build/policy.so'),ROOT/'tests/policy_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.policy_wet.restype=ct.c_float;c.policy_inplace.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
    exchange=read_tests.PresetReadTests.exchange
    audio=read_tests.PresetReadTests.audio
    upload=read_tests.PresetReadTests.upload
    def setUp(self):self.lib.protocol_init()
    def send(self,p):return struct.unpack('<HH11I',self.exchange(p))[2:]
    def test_get_set_exact_silent_commit_readback_and_noop(self):
        c=self.lib;r=self.send(packet());self.assertEqual(r[2:7],(0,1,1,1,0))
        before=download(self.exchange)[1];p=packet(2,1,policy=1);r=self.send(p)
        self.assertEqual((r[2],r[3],r[6],r[7],r[9]),(15,2,0,1,1));self.audio(127)
        self.assertEqual(c.policy_actual(1),0);self.audio(1);self.assertEqual(c.policy_actual(1),1)
        self.audio(128);self.assertEqual(self.send(packet(3,revision=2))[6],1)
        after=download(self.exchange)[1];self.assertEqual(after[:22]+after[23:],before[:22]+before[23:]);self.assertEqual(after[22],1)
        r=self.send(packet(4,1,revision=2,policy=1));self.assertEqual((r[2],r[3],r[7]),(0,3,0))
        self.assertEqual(c.protocol_commands(),2);self.assertTrue(c.protocol_callback_has_no_reply())
    def test_exact_retry_during_fade_no_restart_and_cache_collision_eviction(self):
        c=self.lib;p=packet(10,1,policy=1);self.send(p);self.audio(37)
        r=self.send(p);self.assertEqual((r[2],r[3],r[8]),(1,2,37))
        self.assertEqual(self.send(packet(10,1,policy=0))[2],4)
        self.audio(91);self.assertEqual(c.transfer_phase(),2);self.audio(128)
        self.send(packet(99,revision=2));self.assertEqual(self.send(p)[2],1) # GET doesn't evict.
        other=packet(11,1,revision=2,slot=2,type=2,policy=1);self.send(other);self.audio()
        self.assertEqual(self.send(p)[2],7);self.assertEqual(c.protocol_revision(),3)
    def test_busy_stale_wrong_type_values_slots_revision_exhaustion(self):
        c=self.lib
        for p,status in ((packet(revision=0),7),(packet(type=0),8),(packet(slot=6),6),
                (packet(op=1,policy=2),10),(packet(op=2),5),(packet(token=0),4),(packet(policy=0),2)):
            self.assertEqual(self.send(p)[2],status)
        self.send(packet(3,1,policy=1));self.assertEqual(self.send(packet(4,revision=2))[2],12)
        self.audio();c.protocol_set_revision(0xffffffff)
        self.assertEqual(self.send(packet(5,revision=0xffffffff))[2],0)
        self.assertEqual(self.send(packet(6,1,revision=0xffffffff,policy=0))[2],11)
    def test_empty_slot_policy_and_route_type_changes_preserve_selected_policy(self):
        c=self.lib;self.assertEqual(self.send(packet(1,1,slot=4,type=255,policy=1))[2],15);self.audio()
        self.assertEqual(c.policy_actual(4),1)
        route=struct.pack('<HH4I12B',0x7f42,32,1,2,2,3,0,1,2,3,0,255,0,1,2,3,4,5)
        self.exchange(route);self.audio();self.assertEqual(c.policy_actual(4),1)
        self.assertEqual(self.send(packet(3,revision=3,slot=4,type=0))[6],1)
    def test_disabled_cut_suspends_and_keep_resumes_kernel_without_changing_enable(self):
        c=self.lib
        self.exchange(struct.pack('<HH8I',0x7f40,36,1,1,2,1,1,1,0xffffffff,0))
        self.send(packet(2,1,revision=2,policy=1));self.audio();self.assertEqual(c.policy_suspended(1),1)
        self.assertEqual(c.policy_wet(1),0.)
        self.send(packet(3,1,revision=3,policy=0));self.audio();self.assertEqual(c.policy_suspended(1),0)
        self.assertEqual(c.policy_wet(1),0.)
    def test_pending_policy_cannot_leak_into_later_route_or_native_preset(self):
        c=self.lib;self.assertEqual(c.policy_native(1,1),1);self.audio()
        self.assertEqual(c.policy_actual(1),1)
        # Native recall with KEEP must not inherit stale pending policy state.
        self.exchange(struct.pack('<HH4I12B',0x7f42,32,1,9,1,0,0,1,2,3,255,255,0,1,2,3,4,5));self.audio()
        self.assertEqual(c.policy_actual(1),1);self.assertEqual(c.policy_native(1,2),0)
        words=read_tests.PresetReadTests.body(None);words[22]=0
        self.upload(words,session=1,revision=2);self.audio();self.assertEqual(c.policy_actual(1),0)
    def test_live_policy_faults_and_chunk_partition_continuity(self):
        c=self.lib;outputs=[]
        for chunk in (37,64):
            c.protocol_init();output=bytearray()
            for start,end,policy in ((0,512,None),(512,1024,1),(1024,2048,0)):
                if policy is not None:self.assertEqual(c.policy_native(1,policy),1)
                for begin in range(start,end,chunk):
                    values=[((i*13)%127-63)/1024 for i in range(begin,min(begin+chunk,end))]
                    if begin<=621<begin+len(values):values[621-begin]=float('nan')
                    a=(ct.c_float*len(values))(*values);b=(ct.c_float*len(values))()
                    c.protocol_audio(a,b,len(a));output.extend(bytes(b))
            self.assertEqual(c.policy_faults(),1);self.assertEqual(c.transfer_phase(),0);outputs.append(output)
        self.assertEqual(outputs[0],outputs[1])
    def test_policy_switch_for_every_large_workspace_preserves_other_settings(self):
        c=self.lib;types=(5,8,6,7,3,4);a=(ct.c_uint*6)(*types)
        c.protocol_configure.argtypes=[ct.POINTER(ct.c_uint),ct.c_uint]
        self.assertEqual(c.protocol_configure(a,6),1);token=100
        def enable(slot,value):
            nonlocal token
            token+=1;self.exchange(struct.pack('<HH8I',0x7f40,36,1,token,2,c.protocol_revision(),slot,types[slot],0xffffffff,value))
        for slot in range(6):enable(slot,1)
        for slot in range(6):
            self.audio(2048,True);before=download(self.exchange)[1];token+=1
            self.assertEqual(self.send(packet(token,1,c.protocol_revision(),slot,types[slot],1))[2],15);self.audio()
            enable(slot,0);self.audio(128);self.assertEqual(c.policy_suspended(slot),1)
            token+=1;self.send(packet(token,1,c.protocol_revision(),slot,types[slot],0));self.audio()
            self.assertEqual(c.policy_suspended(slot),0);enable(slot,1);self.audio(128)
            after=download(self.exchange)[1];self.assertEqual(before,after)
        self.assertEqual(c.policy_faults(),0);self.assertTrue(c.protocol_callback_has_no_reply())
    def test_policy_mutation_invalidates_unfinished_upload_without_using_its_snapshot(self):
        from preset_wire import request,decode,checksum,BEGIN,WRITE,COMMIT
        c=self.lib;words=read_tests.PresetReadTests.body(None);crc=checksum(words)
        self.exchange(request(100,BEGIN,1,1,data=[crc]));self.exchange(request(101,WRITE,1,1,data=words[:5]))
        self.assertEqual(self.send(packet(2,1,policy=1))[2],15)
        r=decode(self.exchange(request(102,WRITE,1,1,5,words[5:10])))
        self.assertEqual(r['state'],4);self.audio();self.assertEqual(c.policy_actual(1),1)
        self.assertEqual(decode(self.exchange(request(103,COMMIT,1,1,1071,[crc])))['status'],7)
        self.assertEqual(c.protocol_revision(),2)
    def test_malformed_unaligned_and_inplace_do_not_corrupt_owner(self):
        c=self.lib
        for p in (packet()[:-1],packet()+b'x'):
            self.assertEqual(self.send(p)[2],2)
        raw=bytearray(packet());raw[4]=2;self.assertEqual(self.send(raw)[2],3)
        storage=(ct.c_uint8*49)();p=packet();ptr=ct.cast(ct.byref(storage,1),ct.POINTER(ct.c_uint8))
        for i,value in enumerate(p):ptr[i]=value
        c.policy_inplace(ptr,32);self.assertEqual(bytes(storage)[1:],self.exchange(p))
        self.assertEqual(c.protocol_revision(),1)

if __name__=='__main__':unittest.main()
