"""Fast recall framing, atomicity, retry and absence of delayed flash writes.

Native controller/DSP simulation. Tick counts and UART-pacing arithmetic are
not measured physical switching time or audio-continuity qualification.
"""
import ctypes as ct
import json
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
sys.path.insert(0,str(Path(__file__).resolve().parent));import settings_nav

class QuickPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/quick_presets.dll' if os.name=='nt' else 'build/quick_presets.so'),ROOT/'tests/quick_presets_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_actual_value.restype=ct.c_float
        c.ge_raw.restype=ct.c_uint
        c.qp_packet.argtypes=[ct.POINTER(ct.c_uint8)]
        c.qp_record.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.qp_dispatch.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.qp_fast_dispatch.argtypes=c.qp_dispatch.argtypes
    def settle(self,n=600):
        c=self.lib
        for i in range(n):
            c.qp_step()
            if c.ge_ready():return i+1
        self.fail(f'not ready, state={c.qp_state()}')
    def setUp(self):
        c=self.lib;c.ge_init();c.qp_hide_quick(0);self.settle();self.assertEqual(c.qp_factory(),1);c.qp_reset_trace()
    def load(self,i):
        self.assertEqual(self.lib.ge_flash_load(i),1);self.assertEqual(self.lib.qp_interval(),0);return self.settle()
    def packet(self):
        b=(ct.c_uint8*274)();n=self.lib.qp_packet(b);return bytes(b[:n])
    def send(self,q):
        r=(ct.c_uint8*32)();self.lib.qp_dispatch((ct.c_uint8*len(q))(*q),len(q),r);return struct.unpack_from('<5I',bytes(r),8)
    def test_all_factory_presets_exact_and_no_post_recall_ir_transfer(self):
        c=self.lib;rows=[];rejections=c.qp_rejected()
        for i in range(20):
            c.qp_reset_trace();ticks=self.load(i);b=(ct.c_uint8*264)();c.qp_record(i,b);record=bytes(b)
            self.assertEqual(c.qp_count(0),1);self.assertEqual(c.qp_count(2),0);self.assertEqual(c.qp_count(3),0)
            self.assertEqual(c.qp_rejected(),rejections)
            self.assertLessEqual(ticks,12)
            for slot in range(6):
                o=32+36*slot;t=record[o];self.assertEqual(c.ge_actual_type(slot),t)
                self.assertEqual(c.ge_actual_enabled(slot),record[o+1])
                for p in range(record[o+3]):self.assertEqual(c.ge_raw(slot,p),struct.unpack_from('<I',record,o+4+4*p)[0])
            n=len(self.packet());rows.append(dict(preset=i+1,request_bytes=n,simulated_ticks=ticks,nominal_pacing_idle_max_ms=((n+4+7)//8-1)*3))
        folder=ROOT/'build/quick-presets';folder.mkdir(exist_ok=True)
        (folder/'factory-framing.json').write_text(json.dumps(dict(scope='native simulation and UART pacing arithmetic; NOT physical latency',presets=rows),indent=2)+'\n',encoding='utf-8')
    def test_selection_never_schedules_a_delayed_flash_write(self):
        c=self.lib;self.load(0);before=c.ge_flash_ops()
        self.load(1);c.ge_advance(15000);c.ge_tick_draw();self.settle()
        self.assertEqual(c.ge_flash_ops(),before)
        self.assertEqual(c.ge_global(6),255)
    def test_last_preset_persists_on_explicit_save_or_global_exit(self):
        c=self.lib;self.load(0);c.ge_flash_save(0);self.settle();self.assertEqual(c.ge_global(6),0)
        self.load(1);c.ge_advance(15000);c.ge_tick_draw();self.assertEqual(c.ge_global(6),0)
        c.ge_reboot();self.settle();self.assertEqual(c.ge_current_preset(),0)
        self.load(2);c.ge_long_press();settings_nav.leave(c);self.settle();self.assertEqual(c.ge_global(6),2)
        c.ge_reboot();self.settle();self.assertEqual(c.ge_current_preset(),2)
    def test_lost_acceptance_retries_identical_mutation_once(self):
        c=self.lib;c.ge_drop(2,1);self.load(0) # lost probes must not disable fast recall
        self.assertEqual(c.qp_count(2),0)
        revision=c.ge_actual_revision();c.qp_reset_trace();c.ge_drop(2,1)
        self.load(1);self.assertEqual(c.ge_actual_revision(),revision+1)
        self.assertEqual(c.qp_count(0),3);self.assertEqual(c.qp_count(2),0)
    def test_old_dsp_falls_back_before_any_quick_mutation(self):
        c=self.lib;c.qp_hide_quick(1);self.load(1)
        self.assertEqual(c.qp_count(0),0);self.assertGreater(c.qp_count(2),15)
        self.assertEqual(c.ge_actual_type(1),45)
    def test_identical_cabinet_does_not_invalidate_ir_mirror(self):
        c=self.lib;self.load(1);generation=c.qp_ir_generation();self.load(5)
        self.assertEqual(c.qp_ir_generation(),generation)
    def test_receipt_retry_collision_and_status_do_not_restart_fade(self):
        c=self.lib;self.load(1);q=self.packet();revision=c.ge_actual_revision()
        self.assertEqual(self.send(q)[1],1) # REPLAYED
        bad=bytearray(q);bad[-1]^=1;self.assertEqual(self.send(bad)[1],4) # BAD_REQUEST_ID
        status=bytearray(q[:12]);status[2]=12;status[3]=0;self.assertEqual(self.send(status)[1],1)
        self.assertEqual(c.ge_actual_revision(),revision)
    def test_bad_frame_or_stale_revision_has_no_live_mutation(self):
        c=self.lib;self.load(1);q=bytearray(self.packet());revision=c.ge_actual_revision()
        struct.pack_into('<I',q,8,struct.unpack_from('<I',q,8)[0]+100)
        self.assertEqual(self.send(q)[1],7) # STALE_REVISION
        struct.pack_into('<I',q,12,revision);q[17]=0 # duplicate order entries
        self.assertNotIn(self.send(q)[1],(0,1,15));self.assertEqual(c.ge_actual_revision(),revision)
    def test_float_escape_retains_signed_zero(self):
        c=self.lib;self.load(0);q=bytearray(self.packet());revision=c.ge_actual_revision()
        struct.pack_into('<I',q,8,struct.unpack_from('<I',q,8)[0]+100);struct.pack_into('<I',q,12,revision)
        q[34:35]=b'\xfa'+struct.pack('<I',0x80000000);q[2]=len(q)&255;q[3]=len(q)>>8
        self.assertEqual(self.send(q)[1],15) # ACCEPTED
        c.ge_audio(8);self.assertEqual(struct.pack('<f',c.ge_actual_value(0,0)),struct.pack('<I',0x80000000))
    def test_fast_poll_replies_match_general_handler_and_do_not_mutate(self):
        c=self.lib;self.load(1);q=bytearray(self.packet()[:12]);q[2]=12;q[3]=0
        frames=[bytes(q)]
        for offset,value in ((0,0),(1,0),(2,13),(3,1),(4,2),(8,0),(8,255)):
            b=q.copy();b[offset]=value;frames.append(bytes(b))
        for p in frames:
            normal=(ct.c_uint8*48)();fast=(ct.c_uint8*48)();data=(ct.c_uint8*len(p))(*p)
            revision=c.ge_actual_revision();generation=c.qp_ir_generation()
            c.qp_dispatch(data,len(p),normal)
            # The fast dispatcher only receives the quick opcode; malformed
            # opcode frames remain handled/rejected by the normal dispatcher.
            if p[:2]!=b'\x4d\x7f':continue
            self.assertEqual(c.qp_fast_dispatch(data,len(p),fast),32)
            self.assertEqual(bytes(normal[:32]),bytes(fast[:32]))
            self.assertEqual(c.ge_actual_revision(),revision);self.assertEqual(c.qp_ir_generation(),generation)
        # Check the wire contract independently of the shared implementation.
        probe=q.copy();struct.pack_into('<I',probe,8,0)
        self.assertEqual(self.send(probe)[1],0)
        bad_version=q.copy();struct.pack_into('<I',bad_version,4,2)
        self.assertEqual(self.send(bad_version)[1],3)
        bad_id=q.copy();struct.pack_into('<I',bad_id,8,0xffffffff)
        self.assertEqual(self.send(bad_id)[1],4)
    def test_fast_poll_preserves_phase_and_receipt_during_the_whole_fade(self):
        c=self.lib;self.load(1);q=bytearray(self.packet());revision=c.ge_actual_revision()
        struct.pack_into('<II',q,8,struct.unpack_from('<I',q,8)[0]+100,revision)
        self.assertEqual(self.send(q)[1],15)
        poll=q[:12];poll[2]=12;poll[3]=0;phases=[]
        for _ in range(9):
            normal=(ct.c_uint8*48)();fast=(ct.c_uint8*48)();data=(ct.c_uint8*12)(*poll)
            c.qp_dispatch(data,12,normal);self.assertEqual(c.qp_fast_dispatch(data,12,fast),32)
            self.assertEqual(bytes(normal[:32]),bytes(fast[:32]));phases.append(struct.unpack_from('<I',bytes(fast),20)[0])
            self.assertEqual(c.ge_actual_revision(),revision+1);c.ge_audio(1)
        self.assertIn(1,phases);self.assertIn(2,phases);self.assertEqual(phases[-1],0)
    def test_builtin_resolution_clears_poisoned_unused_assets(self):
        c=self.lib;self.load(1);base=self.packet();token=struct.unpack_from('<I',base,8)[0]+100
        for count in (2,1,0):
            c.qp_poison_pending_assets();q=bytearray(34)
            struct.pack_into('<HHIII',q,0,0x7f4d,34+11*count,1,token,c.ge_actual_revision());token+=1
            q[16]=3;order=sum(i<<(3*i)for i in range(6));q[17:20]=order.to_bytes(3,'little')
            for slot in range(6):q[22+2*slot]=3 if slot<count else 255;q[23+2*slot]=int(slot<count)
            q.extend((b'\xfa'+struct.pack('<f',1.6)+bytes([100])+b'\xfa'+struct.pack('<f',20000.0))*count)   # IR: LEVEL, LOW CUT 0, HIGH CUT off
            self.assertEqual(self.send(q)[1],15);self.assertEqual(c.qp_unused_assets_zero(),1)
            c.ge_audio(8)

if __name__=='__main__':unittest.main()
