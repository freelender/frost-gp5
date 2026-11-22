import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from preset_wire import request,checksum,BEGIN,WRITE,COMMIT,STATUS

def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]

class PresetClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/preset_client.dll' if os.name=='nt' else 'build/preset_client.so'),
            ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c=cls.lib;c.ge_turn.argtypes=[ct.c_int];c.ge_queue_raw.argtypes=[ct.c_uint,ct.c_uint,ct.c_int]
        c.ge_wire.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_uint]
        c.ge_reply.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        c.ge_actual_value.restype=c.ge_ir_coefficient.restype=ct.c_float
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=400):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail(f'client did not settle: state {self.lib.ge_preset_state()} error {self.lib.ge_preset_error()}')
    def until(self,state,n=400):
        for _ in range(n):
            if self.lib.ge_preset_state()==state:return
            self.lib.ge_pump(1)
        self.fail(f'expected state {state}, got {self.lib.ge_preset_state()}')
    def wire(self,p):
        a=(ct.c_uint8*len(p))(*p);self.assertEqual(self.lib.ge_wire(int.from_bytes(p[:2],'little'),a,len(p)),1)
    def words(self,profile):return [self.lib.ge_bank_word(profile,i) for i in range(1071)]
    def prepared(self):return tuple(self.lib.ge_prepared_word(i) for i in range(12))
    def open_bank(self):
        c=self.lib;c.ge_press();c.ge_turn(-1);c.ge_press();self.assertEqual(c.ge_view(),3)
    def test_all_profiles_reach_real_dsp_assets_policies_parameters_and_mute(self):
        c=self.lib
        for profile,types in enumerate(([0,1,2,3,255,255],[0,6,5,8,3,255],[0,1,4,7,3,255])):
            before=c.ge_actual_revision();commands=c.ge_commands()
            self.assertEqual(c.ge_preset_start(profile),1)
            self.settle();self.assertFalse(c.ge_preset_error())
            self.assertEqual(c.ge_actual_revision(),before+1);self.assertEqual(c.ge_commands(),commands+1)
            self.assertEqual([c.ge_type(i) for i in range(6)],types)
            self.assertEqual([c.ge_actual_type(i) for i in range(6)],types)
            self.assertEqual([c.ge_order(i) for i in range(6)],list(range(6)))
            for slot,t in enumerate(types):
                self.assertEqual(c.ge_enabled(slot),t!=255)
                if t==3:
                    self.assertEqual(c.ge_ir_taps(slot),4)
                    self.assertEqual([c.ge_ir_coefficient(slot,j) for j in range(4)],[.5,.25,.125,.0625])
            self.assertAlmostEqual(c.ge_actual_value(0,0),.75)
            if profile==1:
                self.assertEqual([c.ge_policy(i) for i in range(6)],[0,1,1,1,0,0])
                self.assertEqual(c.ge_actual_value(2,0),80)
            self.assertTrue(c.ge_muted());self.assertEqual(c.ge_faults(),0)
        self.assertEqual(c.ge_preset_bytes(),352 if ct.sizeof(ct.c_void_p)==8 else 344) # lossless quick frame + codec state
    def test_bank_words_have_explicit_canonical_unused_fields(self):
        for p in range(3):
            w=self.words(p)
            self.assertEqual(w[:2],[1,44100]);self.assertEqual(w[9:15],list(range(6)))
            self.assertEqual(w[45:51],[4,0,bits(.5),bits(.25),bits(.125),bits(.0625)])
            self.assertEqual(w[51:],[0]*1020)
            self.assertEqual(w[27],bits(.75));self.assertNotEqual(checksum(w),0)
    def test_no_live_mutation_before_commit_and_completion_waits_for_audio(self):
        c=self.lib;self.assertEqual(c.ge_preset_start(1),1);self.until(5)
        self.assertEqual(c.ge_transfer_next(),1071);self.assertEqual(c.ge_actual_revision(),1)
        self.assertEqual(c.ge_actual_type(1),1);self.assertFalse(c.ge_ready())
        self.assertEqual(c.ge_prepare(),48);self.assertEqual(self.prepared()[3],COMMIT);c.ge_deliver()
        self.assertEqual(c.ge_actual_revision(),2);self.assertEqual(c.ge_actual_type(1),1)
        c.ge_advance(20);c.ge_tick();self.assertEqual(c.ge_preset_state(),6)
        for _ in range(8):c.ge_advance(20);c.ge_tick()
        self.assertFalse(c.ge_ready());self.assertEqual(c.ge_actual_type(1),1)
        c.ge_audio(4);self.settle();self.assertEqual(c.ge_actual_type(1),6)
    def test_exact_retry_at_each_mutating_step_for_lost_request_reply_and_token(self):
        c=self.lib
        for state in (3,4,5):
            for mode in (1,2,3):
                c.ge_init();self.settle();c.ge_preset_start(1);self.until(state)
                self.assertEqual(c.ge_prepare(),48);saved=self.prepared();c.ge_drop(1,mode);c.ge_deliver()
                self.assertEqual(c.ge_prepare(),48);self.assertEqual(self.prepared(),saved)
                c.ge_deliver();self.settle()
                self.assertFalse(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),2)
                self.assertEqual(c.ge_commands(),1);self.assertTrue(c.ge_muted())
    def test_malformed_progress_crc_session_and_phase_are_not_adopted(self):
        c=self.lib
        for offset,value in ((20,999),(24,8),(28,10),(36,3),(44,0xdeadbeef)):
            c.ge_init();self.settle();c.ge_preset_start(1);self.until(4);c.ge_prepare();saved=self.prepared()
            token=saved[2];session=saved[4]
            w=self.words(1)
            reply=bytearray(struct.pack('<HH11I',0x7f43,48,1,token,0,1,session,1,5,token,0,0,checksum(w[:5])))
            struct.pack_into('<I',reply,offset,value)
            a=(ct.c_uint8*48)(*reply);self.assertEqual(c.ge_reply(a,48),0)
            self.assertEqual(c.ge_preset_next(),0);self.assertEqual(c.ge_prepare(),48)
            self.assertEqual(self.prepared(),saved);c.ge_deliver();self.settle()
            self.assertEqual(c.ge_actual_revision(),2)
    def test_discovery_aborts_orphan_upload_above_remote_token_floor(self):
        c=self.lib
        self.wire(request(9000,BEGIN,session=40,revision=1,data=[checksum(self.words(0))]))
        self.wire(request(9001,WRITE,session=40,revision=1,data=self.words(0)[:5]))
        c.ge_preset_start(1);self.until(2)
        self.assertEqual(c.ge_prepare(),48);saved=self.prepared()
        self.assertEqual((saved[2],saved[3],saved[4]),(9002,4,40))
        c.ge_drop(1,1);c.ge_deliver();c.ge_prepare();self.assertEqual(self.prepared(),saved);c.ge_deliver()
        self.settle();self.assertFalse(c.ge_preset_error());self.assertGreater(c.ge_token(),9002)
        self.assertEqual(c.ge_actual_revision(),2);self.assertEqual(c.ge_actual_type(1),6)
    def test_discovery_accepts_empty_transfer_with_existing_global_mutation_floor(self):
        c=self.lib
        self.wire(struct.pack('<HH8I',0x7f40,36,1,9000,1,1,0,0,0,bits(1.25)))
        c.ge_pump(15);self.settle();c.ge_preset_start(0);self.settle()
        self.assertFalse(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),3)
        self.assertGreater(c.ge_token(),9000)
    def test_remote_mutation_invalidates_upload_and_never_commits_old_body(self):
        c=self.lib;c.ge_preset_start(1);self.until(4);c.ge_pump(10)
        self.wire(struct.pack('<HH8I',0x7f40,36,1,9000,1,1,0,0,0,bits(1.25)))
        self.settle();self.assertTrue(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),2)
        self.assertEqual(c.ge_actual_type(1),1);self.assertEqual(c.ge_actual_value(0,0),1.25)
        c.ge_preset_start(1);self.settle();self.assertFalse(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),3)
    def test_remote_mutation_after_acceptance_is_reported_without_retrying_recall(self):
        c=self.lib;c.ge_preset_start(1);self.until(6);c.ge_audio(4)
        self.wire(struct.pack('<HH8I',0x7f40,36,1,9000,1,2,0,0,0,bits(1.25)))
        self.settle();self.assertTrue(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),3)
        self.assertEqual(c.ge_actual_value(0,0),1.25)
    def test_reconnect_discards_transfer_client_and_next_recall_recovers_orphan(self):
        c=self.lib;c.ge_preset_start(1);self.until(4);c.ge_pump(12);c.ge_reconnect();self.settle()
        self.assertEqual(c.ge_actual_revision(),1);self.assertEqual(c.ge_preset_state(),0)
        c.ge_preset_start(2);self.settle();self.assertEqual(c.ge_actual_type(2),4)
        c.ge_preset_start(1);self.until(5);c.ge_dsp_reset();self.settle()
        self.assertEqual(c.ge_actual_revision(),1);self.assertEqual(c.ge_actual_type(1),1)
        c.ge_preset_start(1);self.settle();self.assertEqual(c.ge_actual_revision(),2)
    def test_discovery_waits_for_foreign_accepted_recall_without_aborting_it(self):
        c=self.lib;w=self.words(0);crc=checksum(w);token=9000
        self.wire(request(token,BEGIN,session=8,revision=1,data=[crc]))
        for i in range(0,1071,5):
            token+=1;self.wire(request(token,WRITE,session=8,revision=1,index=i,data=w[i:i+5]))
        token+=1;self.wire(request(token,COMMIT,session=8,revision=1,index=1071,data=[crc]))
        self.assertEqual(c.ge_preset_start(1),1)
        c.ge_prepare();c.ge_deliver();self.assertEqual(c.ge_preset_state(),1)
        self.assertEqual(c.ge_actual_revision(),2);self.assertEqual(c.ge_transition(),1)
        self.settle();self.assertFalse(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),3)
        self.assertEqual(c.ge_commands(),2);self.assertEqual(c.ge_actual_type(1),6)
    def test_reconnect_after_lost_commit_ack_imports_once_without_replaying_old_intent(self):
        c=self.lib;c.ge_preset_start(1);self.until(5);c.ge_prepare();c.ge_drop(1,1);c.ge_deliver()
        self.assertEqual(c.ge_actual_revision(),2);c.ge_reconnect();self.settle()
        self.assertEqual(c.ge_actual_revision(),2);self.assertEqual(c.ge_commands(),1)
        self.assertEqual(c.ge_actual_type(1),6);self.assertFalse(c.ge_preset_error())
        c.ge_preset_start(2);self.settle();self.assertEqual(c.ge_actual_revision(),3)
    def test_pending_edits_drain_before_load_and_never_rebase_over_preset(self):
        c=self.lib;self.assertEqual(c.ge_queue_raw(0,0,1500),1)
        self.assertEqual(c.ge_preset_start(1),0);c.ge_prepare()
        self.assertEqual(c.ge_preset_start(1),0);c.ge_deliver();self.settle()
        self.assertEqual(c.ge_preset_start(1),1);self.assertEqual(c.ge_queue_raw(0,0,2000),0)
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_value(0,0),.75)
        self.assertEqual([c.ge_actual_enabled(i) for i in range(6)],[1,1,1,1,1,0])
    def test_counter_exhaustion_fails_closed_without_wrap(self):
        c=self.lib;c.ge_set_token(0xfffffff0);self.assertEqual(c.ge_preset_start(0),1)
        c.ge_pump(2);self.assertTrue(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),1)
        self.assertEqual(c.ge_transfer_state(),0)
        c.ge_init();self.settle()
        self.wire(request(9000,BEGIN,session=0xffffffff,revision=1,data=[checksum(self.words(0))]))
        c.ge_preset_start(0);self.settle();self.assertTrue(c.ge_preset_error());self.assertEqual(c.ge_actual_revision(),1)

if __name__=='__main__':unittest.main()
