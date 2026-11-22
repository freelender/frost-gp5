import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from preset_wire import request,decode,checksum,packet_plan,snapshot_words,WORDS,STATUS,BEGIN,WRITE,COMMIT,ABORT

def body():
    # Fully explicit five-effect snapshot with a real512-tap synthetic asset.
    types=[0,5,8,3,6,255];order=[3,0,1,4,2,5];enabled=[1]*5+[0];policy=[0,1,1,0,0,0]
    p=[.8,0,0,20.,.3,.2,.8,.6,.25,1.,0,0,.8,.5,.3,0,0,0]
    bits=[struct.unpack('<I',struct.pack('<f',v))[0] for v in p]
    ir=[.7]+[.0001*(-1 if i&1 else 1) for i in range(1,512)]
    return [1,44100,2]+types+order+enabled+policy+bits+[512,0]+[struct.unpack('<I',struct.pack('<f',v))[0] for v in ir]+[0]*512

class PresetTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/preset_transfer.dll' if os.name=='nt' else 'build/preset_transfer.so'),ROOT/'tests/preset_transfer_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.protocol_send.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.protocol_audio.argtypes=[ct.POINTER(ct.c_float),ct.POINTER(ct.c_float),ct.c_uint]
        c.transfer_target.restype=c.transfer_ir_coeff.restype=ct.c_float
    def setUp(self):self.lib.protocol_init();self.token=0;self.words=body();self.crc=checksum(self.words)
    def send(self,p,inplace=False):
        a=(ct.c_uint8*max(48,len(p)))(*p)
        if inplace:self.lib.transfer_inplace(a,len(p));return decode(bytes(a[:48]))
        self.assertEqual(self.lib.protocol_send(0x7f43,a,len(p)),1)
        self.assertEqual(self.lib.protocol_reply_bytes(),48)
        return decode(bytes(self.lib.protocol_reply(i) for i in range(48)))
    def packet(self,op,session=1,revision=1,index=0,data=()):
        self.token+=1;return request(self.token,op,session,revision,index,data)
    def begin(self,crc=None,session=1,revision=1):
        p=self.packet(BEGIN,session,revision,data=[self.crc if crc is None else crc]);r=self.send(p)
        self.assertEqual(r['status'],0);return p
    def upload(self,session=1,revision=1):
        self.begin(session=session,revision=revision)
        for i in range(0,WORDS,5):self.assertEqual(self.send(self.packet(WRITE,session,revision,index=i,data=self.words[i:i+5]))['status'],0)
    def audio(self,n):
        a=(ct.c_float*n)(*([.1]*n));b=(ct.c_float*n)();self.lib.protocol_audio(a,b,n);return list(b)
    def slot_write(self,token=9000,revision=1):
        p=struct.pack('<HH8I',0x7f40,36,1,token,1,revision,0,0,0,struct.unpack('<I',struct.pack('<f',1.25))[0])
        a=(ct.c_uint8*len(p))(*p);self.lib.protocol_send(0x7f40,a,len(p))
        return self.lib.protocol_reply(12)
    def test_full_transfer_keeps_live_state_until_silent_commit(self):
        c=self.lib;self.assertEqual(len(self.words),WORDS);self.assertEqual(c.transfer_bytes(),356)
        self.upload();self.assertEqual(c.protocol_revision(),1)
        self.assertEqual([c.transfer_type(i) for i in range(6)],[0,1,2,3,255,255])
        packet=self.packet(COMMIT,index=WORDS,data=[self.crc]);r=self.send(packet)
        self.assertEqual((r['status'],r['revision'],r['state'],r['phase']),(15,2,2,1))
        self.audio(127);self.assertEqual(c.transfer_type(1),1)
        self.assertEqual(self.audio(1),[0.]);self.assertEqual(c.transfer_type(1),5)
        self.assertAlmostEqual(c.transfer_target(0,0),.8,places=6);self.assertEqual(c.transfer_policy(1),1)
        self.assertEqual(c.transfer_ir_taps(3),512);self.assertAlmostEqual(c.transfer_ir_coeff(3,511),-.0001,places=7)
        self.audio(128);r=self.send(self.packet(STATUS,session=1,revision=0))
        self.assertEqual((r['state'],r['phase'],r['committed_revision']),(3,0,2))
        self.assertEqual(c.protocol_commands(),1);self.assertTrue(c.protocol_callback_has_no_reply())
    def test_exact_begin_chunk_and_commit_retries_do_not_rewrite_or_restart(self):
        c=self.lib;p=self.begin();self.assertEqual(self.send(p)['status'],1)
        data=self.packet(WRITE,index=0,data=self.words[:5]);r=self.send(data)
        for _ in range(3):self.assertEqual(self.send(data)['next_word'],5)
        self.assertEqual(r['prefix_crc'],checksum(self.words[:5]))
        for i in range(5,WORDS,5):self.send(self.packet(WRITE,index=i,data=self.words[i:i+5]))
        commit=self.packet(COMMIT,index=WORDS,data=[self.crc]);self.send(commit);self.audio(37)
        r=self.send(commit);self.assertEqual((r['status'],r['phase'],r['revision']),(1,1,2))
        self.audio(91);self.assertEqual(c.transfer_phase(),2) # no restarted fade
        self.audio(128);self.assertEqual(self.send(commit)['state'],3);self.assertEqual(c.protocol_revision(),2)
        changed=bytearray(commit);changed[28]^=1;self.assertEqual(self.send(changed)['status'],4)
    def test_incomplete_missing_reordered_checksum_and_semantic_failures_are_atomic(self):
        c=self.lib;self.begin();self.assertEqual(self.send(self.packet(COMMIT,index=WORDS,data=[self.crc]))['status'],18)
        self.assertEqual(self.send(self.packet(WRITE,index=5,data=self.words[5:10]))['status'],19)
        self.assertEqual(c.transfer_next(),0);self.assertEqual(c.protocol_revision(),1)
        for i in range(0,WORDS,5):self.send(self.packet(WRITE,index=i,data=self.words[i:i+5]))
        self.assertEqual(self.send(self.packet(COMMIT,index=WORDS,data=[self.crc^1]))['status'],17)
        self.assertEqual(c.transfer_phase(),0);self.assertEqual(c.protocol_revision(),1)
        c.protocol_init();self.token=0;self.words[27]=0x7fc00000;self.crc=checksum(self.words);self.upload()
        self.assertEqual(self.send(self.packet(COMMIT,index=WORDS,data=[self.crc]))['status'],13)
        self.assertEqual(c.transfer_type(1),1);self.assertEqual(c.protocol_revision(),1)
    def test_revision_intervention_invalidates_upload_and_evicts_commit_receipt(self):
        c=self.lib;self.begin();self.send(self.packet(WRITE,data=self.words[:5]))
        self.assertEqual(self.slot_write(),0)
        r=self.send(self.packet(STATUS,revision=0));self.assertEqual((r['state'],r['revision']),(4,2))
        self.assertEqual(self.send(self.packet(WRITE,index=5,data=self.words[5:10]))['status'],7)
        c.protocol_init();self.token=0;self.upload();commit=self.packet(COMMIT,index=WORDS,data=[self.crc]);self.send(commit)
        self.audio(256);self.assertEqual(self.slot_write(revision=2),0)
        self.assertEqual(self.send(commit)['status'],7);self.assertEqual(c.protocol_revision(),3)
    def test_abort_restart_monotonic_tokens_sessions_and_cross_namespace_collision(self):
        self.begin();self.assertEqual(self.send(self.packet(BEGIN,session=2,data=[self.crc]))['status'],12)
        abort=self.packet(ABORT);self.assertEqual(self.send(abort)['state'],5);self.assertEqual(self.send(abort)['status'],1)
        self.assertEqual(self.send(self.packet(BEGIN,session=1,data=[self.crc]))['status'],16)
        self.begin(session=2);self.assertEqual(self.send(request(1,WRITE,2,1,0,self.words[:5]))['status'],4)
        self.lib.protocol_init();self.assertEqual(self.slot_write(token=100),0)
        self.assertEqual(self.send(request(100,BEGIN,1,2,data=[self.crc]))['status'],4)
    def test_envelopes_reserved_fields_narrowing_and_inplace_buffers(self):
        for p,status in [(b'',2),(request(0,STATUS),4),(request(1,99),5)]:self.assertEqual(self.send(p)['status'],status)
        p=bytearray(request(2,STATUS));p[4]=2;self.assertEqual(self.send(p)['status'],3)
        self.assertEqual(self.send(request(3,STATUS,revision=1))['status'],2)
        p=self.packet(BEGIN,data=[self.crc]);self.assertEqual(self.send(p,inplace=True)['status'],0)
        bad=self.words[:5];bad[3]=256;self.assertEqual(self.send(self.packet(WRITE,data=bad))['status'],10)
        self.assertEqual(self.lib.transfer_next(),0)
        self.assertEqual(self.send(self.packet(WRITE,index=WORDS))['status'],19)
    def test_prefix_crc_completeness_and_exhaustion_reboot_boundary(self):
        self.upload();r=self.send(self.packet(STATUS,revision=0));self.assertEqual((r['next_word'],r['prefix_crc']),(WORDS,self.crc))
        self.lib.protocol_set_revision(0xffffffff)
        self.assertEqual(self.send(self.packet(BEGIN,session=2,revision=0xffffffff,data=[self.crc]))['status'],11)
        self.lib.protocol_init();self.token=0;self.upload();commit=self.packet(COMMIT,index=WORDS,data=[self.crc]);self.send(commit)
        self.lib.protocol_init();self.assertEqual(self.send(commit)['status'],16)
        r=self.send(self.packet(STATUS,session=0,revision=0));self.assertEqual((r['state'],r['next_word'],r['revision']),(0,0,1))
    def test_last_padding_checksum_mismatch_and_noncontiguous_old_write(self):
        self.begin(crc=self.crc^1)
        for i in range(0,1070,5):self.send(self.packet(WRITE,index=i,data=self.words[i:i+5]))
        p=self.packet(WRITE,index=1070,data=[self.words[-1],1])
        self.assertEqual(self.send(p)['status'],2);self.assertEqual(self.lib.transfer_next(),1070)
        self.send(self.packet(WRITE,index=1070,data=[self.words[-1]]))
        self.assertEqual(self.send(self.packet(COMMIT,index=WORDS,data=[self.crc^1]))['status'],17)
        self.assertEqual(self.send(self.packet(WRITE,index=0,data=self.words[:5]))['status'],19)
        self.assertEqual(self.lib.protocol_revision(),1)
    def test_two_ir_banks_and_host_packet_plan_reach_real_handler(self):
        from preset_io import Preset
        slots=[]
        for i in range(6):slots.append(dict(id=i,type='ir' if i in (1,4) else 'empty',enabled=i in (1,4),bypass='keep',
            parameters={'LEVEL':.8 if i==1 else 1.25} if i in (1,4) else {}))
        p=Preset(dict(slots=slots,order=[4,1,0,2,3,5],pre_count=1),
            {1:tuple([.7]+[.0001]*511),4:tuple([.5]+[-.0002]*511)})
        plan=list(packet_plan(p,1,1));self.assertEqual(len(plan),217)
        self.assertEqual(len(snapshot_words(p)),1071)
        for packet in plan:self.assertIn(self.send(packet)['status'],(0,15))
        self.audio(256)
        self.assertEqual(self.lib.transfer_ir_taps(1),512);self.assertEqual(self.lib.transfer_ir_taps(4),512)
        self.assertAlmostEqual(self.lib.transfer_ir_coeff(1,511),.0001,places=7)
        self.assertAlmostEqual(self.lib.transfer_ir_coeff(4,511),-.0002,places=7)
        self.assertAlmostEqual(self.lib.transfer_target(1,0),.8,places=6)
        self.assertAlmostEqual(self.lib.transfer_target(4,0),1.25,places=6)
    def test_abort_cannot_cancel_accepted_recall_and_new_session_evicts_receipt(self):
        self.upload();commit=self.packet(COMMIT,index=WORDS,data=[self.crc]);self.send(commit)
        self.assertEqual(self.send(self.packet(ABORT))['status'],5)
        self.assertEqual(self.send(self.packet(BEGIN,session=2,revision=2,data=[self.crc]))['status'],12)
        self.audio(256);self.begin(session=2,revision=2)
        self.assertEqual(self.send(commit)['status'],7)
        self.assertEqual(self.lib.transfer_next(),0);self.assertEqual(self.lib.protocol_revision(),2)
    def test_unaligned_requests_and_corrupt_success_replies(self):
        p=request(1,STATUS);a=(ct.c_uint8*49)(0,*p)
        self.assertEqual(self.lib.protocol_send(0x7f43,ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8)),48),1)
        reply=bytearray(self.lib.protocol_reply(i) for i in range(48));self.assertEqual(decode(reply)['status'],0)
        for offset,value in ((24,99),(28,1072),(20,1),(36,3)):
            bad=reply[:];struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):decode(bad)
    def test_last_revision_and_immutable_token_collision(self):
        self.lib.protocol_set_revision(0xfffffffe)
        self.begin(revision=0xfffffffe)
        first=self.packet(WRITE,revision=0xfffffffe,data=self.words[:5]);self.send(first)
        bad=bytearray(first);bad[28]^=1;self.assertEqual(self.send(bad)['status'],4)
        for i in range(5,WORDS,5):self.send(self.packet(WRITE,revision=0xfffffffe,index=i,data=self.words[i:i+5]))
        commit=self.packet(COMMIT,revision=0xfffffffe,index=WORDS,data=[self.crc]);r=self.send(commit)
        self.assertEqual((r['status'],r['revision']),(15,0xffffffff))
        self.audio(256);self.assertEqual(self.send(commit)['status'],1)
    def test_identical_second_recall_is_a_new_fade_not_a_retry(self):
        self.upload();first=self.packet(COMMIT,index=WORDS,data=[self.crc]);self.send(first);self.audio(256)
        self.upload(session=2,revision=2)
        second=self.packet(COMMIT,session=2,revision=2,index=WORDS,data=[self.crc])
        self.assertEqual((self.send(second)['status'],self.lib.protocol_revision()),(15,3))
        self.assertEqual(self.send(second)['status'],1);self.audio(256)
        self.assertEqual(self.lib.transfer_phase(),0)
    def test_status_identity_allows_recovery_after_client_state_loss(self):
        self.begin();self.send(self.packet(WRITE,data=self.words[:5]))
        r=self.send(request(1,STATUS));self.assertEqual((r['session'],r['token_floor'],r['next_word']),(1,2,5))
        self.assertEqual(self.send(request(r['token_floor']+1,ABORT,r['session'],r['revision']))['status'],0)
        r=self.send(request(1,STATUS))
        fresh=request(r['token_floor']+1,BEGIN,r['session']+1,r['revision'],data=[self.crc])
        self.assertEqual(self.send(fresh)['status'],0)
        self.slot_write(token=9000)
        r=self.send(request(1,STATUS));self.assertEqual((r['state'],r['token_floor']),(4,9000))
        self.assertEqual(self.send(request(r['token_floor']+1,BEGIN,r['session']+1,r['revision'],data=[self.crc]))['status'],0)

if __name__=='__main__':unittest.main()
