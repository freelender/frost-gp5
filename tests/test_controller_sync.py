import ctypes as ct
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
OK,REPLAY,BAD_FRAME,BAD_VERSION,BAD_ID,BAD_OP,BAD_SLOT,STALE,WRONG_TYPE,BAD_PARAMETER,BAD_VALUE,EXHAUSTED,BUSY,BAD_ROUTE,BAD_SETTING=range(15)
UNKNOWN,READY,SYNC_BUSY,LAYOUT,ERROR=range(5)
def setting(token,revision,id,value):return struct.pack('<HH5I',0x7f11,24,1,token,revision,id,value)

class ControllerSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/controller_sync.dll' if os.name=='nt' else 'build/controller_sync.so'),
            ROOT/'tests/chain_connected_host.c',includes=[ROOT/'tests/stubs'])))
        cls.lib.connected_wire.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.connected_route.argtypes=[ct.POINTER(ct.c_uint),ct.POINTER(ct.c_uint),ct.c_uint]
        cls.lib.connected_route_reply.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.lib.connected_set.argtypes=[ct.c_uint32,ct.c_uint32]
        cls.lib.connected_turn.argtypes=[ct.c_int]
    def setUp(self):self.lib.connected_init();self.lib.connected_drain()
    def wire(self,cmd,p):
        a=(ct.c_uint8*(len(p)+1))(0,*p)
        self.assertEqual(self.lib.connected_wire(cmd,ct.cast(ct.byref(a,1),ct.POINTER(ct.c_uint8)),len(p)),1)
        return bytes(self.lib.connected_reply_byte(i) for i in range(self.lib.connected_reply_bytes()))
    def route(self,pre):
        self.assertEqual(self.lib.connected_route((ct.c_uint*6)(0,1,2,3,255,255),(ct.c_uint*6)(*range(6)),pre),OK)
    def pixels(self):
        self.lib.connected_paint()
        return hashlib.sha256(struct.pack('<12800H',*(self.lib.connected_pixel(i) for i in range(12800)))).hexdigest()
    def test_actual_platform_fragment_confirms_only_explicit_results(self):
        c=self.lib;c.connected_disconnect()
        self.assertEqual(c.connected_sync_state(),UNKNOWN)
        self.assertEqual(c.connected_drain(),0)
        for _ in range(11):c.connected_tick();c.connected_advance(20)
        self.assertEqual(c.connected_sync_state(),READY)
        self.assertEqual(c.connected_pending(),0)
        self.assertEqual(c.connected_confirmed(),2047)
        c.connected_force_value(1,1250);before=c.connected_metric(7)
        c.connected_drop(2);c.connected_tick() # DSP committed, but only an ACK reaches controller
        self.assertFalse(c.connected_confirmed() & 2)
        self.assertEqual(c.connected_sync_state(),UNKNOWN)
        self.assertEqual(c.connected_metric(7),before+1)
        c.connected_advance(100);c.connected_tick()
        self.assertTrue(c.connected_confirmed() & 2)
        self.assertEqual(c.connected_get(1),1250)
        self.assertEqual(c.connected_metric(7),before+1) # retry was replayed
    def test_lost_ir_reply_reuses_bytes_and_does_not_reset_again(self):
        c=self.lib;c.connected_force_value(9,144)
        self.assertEqual(c.connected_prepare(),24)
        before=[c.connected_prepared_word(i) for i in range(6)];commands=c.connected_metric(7)
        c.connected_deliver_lost(1)
        self.assertEqual(c.connected_sync_state(),UNKNOWN)
        c.connected_audio(1);c.connected_ir_mark();self.assertEqual(c.connected_ir_marked(),1)
        c.connected_poll();self.assertEqual(c.connected_prepare(),24)
        self.assertEqual([c.connected_prepared_word(i) for i in range(6)],before)
        c.connected_deliver()
        response=bytes(c.connected_reply_byte(i) for i in range(c.connected_reply_bytes()))
        self.assertEqual(struct.unpack_from('<I',response,12)[0],REPLAY)
        self.assertEqual(c.connected_ir_marked(),1) # replay did not reload/reset the IR
        self.assertEqual(c.connected_metric(7),commands+1)
        self.assertEqual(c.connected_pending(),0)
    def test_busy_and_incompatible_routes_pause_edits_then_resync(self):
        c=self.lib;c.connected_foot();self.assertEqual(c.connected_ui_value(2),0)
        self.assertEqual(c.connected_prepare(),24);self.route(0);c.connected_deliver()
        self.assertEqual(c.connected_sync_state(),SYNC_BUSY)
        c.connected_foot();self.assertEqual(c.connected_ui_value(2),0)
        c.connected_turn(1);c.connected_press();c.connected_press();before=c.connected_ui_value(5)
        c.connected_turn(20);self.assertEqual(c.connected_ui_value(5),before)
        busy_pixels=self.pixels()
        c.connected_audio(4);c.connected_poll()
        self.assertEqual(c.connected_sync_state(),LAYOUT)
        self.assertNotEqual(self.pixels(),busy_pixels)
        self.assertEqual(c.connected_drain(),0)
        self.route(3);c.connected_audio(4);c.connected_poll()
        self.assertEqual(c.connected_sync_state(),READY)
        self.assertEqual(c.connected_drain(),11)
        self.assertEqual(c.connected_get(2),0) # the original desired toggle survived
        self.assertTrue(c.connected_muted())
    def test_bad_value_rolls_back_only_the_rejected_intent(self):
        c=self.lib;c.connected_force_value(1,5000)
        self.assertEqual(c.connected_prepare(),24);c.connected_deliver()
        self.assertEqual(c.connected_ui_value(1),1000)
        self.assertFalse(c.connected_pending() & 2)
        error_pixels=self.pixels()
        c.connected_force_value(1,5000);c.connected_prepare()
        c.connected_force_value(1,1250);c.connected_deliver()
        self.assertEqual(c.connected_ui_value(1),1250)
        self.assertTrue(c.connected_pending() & 2)
        self.assertEqual(c.connected_drain(),1)
        self.assertEqual(c.connected_get(1),1250)
        self.assertNotEqual(self.pixels(),error_pixels)
    def test_stale_result_preserves_latest_intent_and_refreshes(self):
        c=self.lib;c.connected_force_value(1,1500);c.connected_prepare()
        c.connected_set(1,1750);c.connected_deliver()
        self.assertEqual(c.connected_sync_state(),UNKNOWN)
        self.assertEqual(c.connected_get(1),1750)
        self.assertEqual(c.connected_ui_value(1),1500)
        c.connected_poll();c.connected_drain()
        self.assertEqual(c.connected_get(1),1500)
        self.assertEqual(c.connected_pending(),0)
    def test_route_status_layout_and_busy_phase_are_read_only(self):
        c=self.lib;revision=c.connected_revision()
        p=struct.pack('<HHII',0x7f41,12,1,123)
        r=self.wire(0x7f41,p);self.assertEqual(len(r),44)
        self.assertEqual(struct.unpack_from('<7I',r,4),(1,123,OK,revision,0,0,3))
        self.assertEqual(struct.unpack_from('<6B',r,32),(0,1,2,3,255,255))
        self.assertEqual(struct.unpack_from('<6B',r,38),tuple(range(6)))
        self.route(0);r=self.wire(0x7f41,p)
        self.assertEqual(struct.unpack_from('<I',r,12)[0],BUSY)
        self.assertEqual(struct.unpack_from('<I',r,20)[0],1)
        c.connected_audio(2);r=self.wire(0x7f41,p)
        self.assertEqual(struct.unpack_from('<I',r,20)[0],2)
        c.connected_audio(2);r=self.wire(0x7f41,p)
        self.assertEqual(struct.unpack_from('<I',r,12)[0],OK)
        self.assertEqual(struct.unpack_from('<I',r,28)[0],0)
        self.assertEqual(c.connected_revision(),revision+1)
    def test_explicit_endpoint_replay_namespace_and_atomic_rejections(self):
        c=self.lib;revision=c.connected_revision();p=setting(100,revision,1,1250)
        r=self.wire(0x7f11,p)
        self.assertEqual(struct.unpack('<HH7I',r),(0x7f11,32,1,100,OK,revision+1,1,1250,1250))
        self.wire(0x7f41,struct.pack('<HHII',0x7f41,12,1,101))
        self.assertEqual(struct.unpack_from('<I',self.wire(0x7f11,p),12)[0],REPLAY)
        bad=[(setting(100,revision+1,1,1500),BAD_ID),(setting(102,revision,1,1500),STALE),
             (setting(103,revision+1,1,4001),BAD_VALUE),(setting(104,revision+1,0xffffffff,1),BAD_SETTING),
             (setting(0,revision+1,1,1500),BAD_ID)]
        for payload,status in bad:
            self.assertEqual(struct.unpack_from('<I',self.wire(0x7f11,payload),12)[0],status)
            self.assertEqual(c.connected_revision(),revision+1)
            self.assertEqual(c.connected_get(1),1250)
        generic=struct.pack('<HH8I',0x7f40,36,1,100,1,revision+1,0,0,0,0x3f800000)
        self.assertEqual(struct.unpack_from('<I',self.wire(0x7f40,generic),12)[0],BAD_ID)
    def test_malformed_wire_and_poisoned_controller_snapshot_do_not_confirm(self):
        c=self.lib;revision=c.connected_revision();p=setting(99,revision,1,1250)
        for n in list(range(24))+[25,32]:
            r=self.wire(0x7f11,(p+b'\0'*32)[:n])
            self.assertEqual(struct.unpack_from('<I',r,12)[0],BAD_FRAME)
        self.assertEqual(c.connected_revision(),revision)
        token=c.connected_begin_poll()
        payload=bytearray(struct.pack('<HH7I12B',0x7f41,44,1,token,OK,revision,0,0,3,
            0,1,2,3,255,255,0,0,2,3,4,5)) # duplicate route position
        a=(ct.c_uint8*len(payload))(*payload)
        self.assertEqual(c.connected_route_reply(a,len(payload)),0)
        self.assertEqual(c.connected_sync_state(),ERROR)
        self.assertEqual(c.connected_drain(),0)
        c.connected_poll();self.assertEqual(c.connected_sync_state(),READY)

    def test_dsp_restart_discards_old_retry_and_resynchronizes_desired_values(self):
        c=self.lib;c.connected_force_value(9,144);c.connected_prepare()
        old_token=c.connected_prepared_word(2);c.connected_deliver_lost(1)
        c.connected_dsp_reset();self.assertEqual(c.connected_revision(),1)
        self.assertEqual(c.connected_drain(),0)
        c.connected_poll();self.assertEqual(c.connected_prepare(),24)
        self.assertNotEqual(c.connected_prepared_word(2),old_token)
        self.assertEqual(c.connected_prepared_word(3),1)
        c.connected_deliver();c.connected_drain()
        self.assertEqual(c.connected_get(9),144)
        self.assertEqual(c.connected_pending(),0)

    def test_route_query_malformed_requests_never_mutate_revision(self):
        c=self.lib;revision=c.connected_revision()
        p=struct.pack('<HHII',0x7f41,12,1,5)
        for n in list(range(12))+[13,24,80]:
            r=self.wire(0x7f41,(p+b'\0'*80)[:n])
            self.assertEqual(len(r),44)
            self.assertEqual(struct.unpack_from('<I',r,12)[0],BAD_FRAME)
        for version,token,status in ((2,5,BAD_VERSION),(1,0,BAD_ID)):
            r=self.wire(0x7f41,struct.pack('<HHII',0x7f41,12,version,token))
            self.assertEqual(struct.unpack_from('<I',r,12)[0],status)
        self.assertEqual(c.connected_revision(),revision)

if __name__=='__main__':unittest.main()
