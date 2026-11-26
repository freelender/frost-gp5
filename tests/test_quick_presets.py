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
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
sys.path.insert(0,str(Path(__file__).resolve().parent));import settings_nav

class QuickPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/quick_presets.dll' if os.name=='nt' else 'build/quick_presets.so'),ROOT/'tests/quick_presets_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_IR_FFT_PARTS=8u'])))
        c.ge_actual_value.restype=ct.c_float
        c.ge_raw.restype=ct.c_uint
        c.ge_ir_coefficient.restype=ct.c_float
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
    def test_selection_is_written_once_after_it_settles(self):
        """beta.11 (user): the selected preset survives a power cycle; one write."""
        c=self.lib;self.load(0);before=c.ge_flash_ops()
        self.load(1);c.ge_tick_draw();self.assertEqual(c.ge_flash_ops(),before)   # not at once
        c.ge_advance(15000);c.ge_tick_draw();self.settle()
        self.assertGreater(c.ge_flash_ops(),before);self.assertEqual(c.ge_global(6),1)
        once=c.ge_flash_ops();c.ge_advance(15000);c.ge_tick_draw();self.settle()
        self.assertEqual(c.ge_flash_ops(),once)
    def test_last_preset_persists_on_save_selection_or_global_exit(self):
        c=self.lib;self.load(0);c.ge_flash_save(0);self.settle();self.assertEqual(c.ge_global(6),0)
        self.load(1);c.ge_tick_draw();c.ge_advance(15000);c.ge_tick_draw();self.settle();self.assertEqual(c.ge_global(6),1)
        c.ge_reboot();self.settle();self.assertEqual(c.ge_current_preset(),1)
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
    def custom_cab(self,index=0,variant=0):
        record=bytearray(1048);record[:8]=b'TEST CAB'
        struct.pack_into('<f',record,16,1.0/32767)
        struct.pack_into('<h',record,20+2*variant,32767)
        struct.pack_into('<I',record,1044,zlib.crc32(record[:1044]))
        self.assertEqual(self.lib.irf_store_write(index,(ct.c_uint8*1048).from_buffer_copy(record)),1)
    def custom_presets(self):
        c=self.lib;self.custom_cab();c.irf_cab(3,1)
        c.ge_pump(350);self.assertEqual(c.irf_busy(),0)
        self.custom_values={}
        for preset in (25,26):
            self.assertEqual(c.ge_queue(0,0,preset-23),1);self.settle()
            self.custom_values[preset]=c.ge_raw(0,0)
            self.assertEqual(c.ge_flash_save(preset),1);self.settle()
        return [c.ge_ir_coefficient(3,i) for i in range(512)]
    def test_same_custom_ir_fast_recall_preserves_coefficients_and_mirror(self):
        c=self.lib;want=self.custom_presets();generation=c.qp_ir_generation();c.qp_probe_again()
        for preset in (25,26,25):
            c.qp_reset_trace();ticks=self.load(preset)
            self.assertLessEqual(ticks,12)
            self.assertEqual((c.qp_count(0),c.qp_count(2),c.qp_count(3)),(1,0,0))
            self.assertEqual(self.packet()[20],1<<3)
            self.assertEqual(c.ge_raw(0,0),self.custom_values[preset])
            c.ge_pump(350)  # no delayed coefficient transaction after ready
            self.assertEqual(c.qp_count(3),0)
            self.assertEqual(c.qp_ir_generation(),generation)
            self.assertEqual([c.ge_ir_coefficient(3,i) for i in range(512)],want)
    def test_replaced_custom_ir_still_reloads_and_then_reuses_it(self):
        c=self.lib;self.custom_presets();self.custom_cab(0,1)
        c.qp_reset_trace();self.load(25);c.ge_pump(350)
        self.assertEqual(c.qp_count(2),0);self.assertGreater(c.qp_count(3),0)
        self.assertEqual(c.ge_ir_coefficient(3,0),0)
        self.assertGreater(c.ge_ir_coefficient(3,1),0.9)
        c.qp_reset_trace();self.load(26);c.ge_pump(350)
        self.assertEqual((c.qp_count(0),c.qp_count(2),c.qp_count(3)),(1,0,0))
    def test_old_quick_dsp_without_retain_capability_uses_established_transfer(self):
        c=self.lib;want=self.custom_presets();c.qp_old_capabilities();c.qp_reset_trace()
        self.load(25);c.ge_pump(350)
        self.assertEqual(c.qp_count(0),0);self.assertGreater(c.qp_count(2),0)
        self.assertGreater(c.qp_count(3),0)
        self.assertEqual([c.ge_ir_coefficient(3,i) for i in range(512)],want)
    def test_first_probe_without_capability_falls_back_before_mutation(self):
        c=self.lib;want=self.custom_presets();c.qp_probe_again();c.qp_reset_trace()
        self.assertEqual(c.ge_flash_load(25),1);c.qp_old_probe_reply();self.settle();c.ge_pump(350)
        self.assertEqual(c.qp_count(0),0);self.assertGreater(c.qp_count(2),0)
        self.assertGreater(c.qp_count(3),0)
        self.assertEqual([c.ge_ir_coefficient(3,i) for i in range(512)],want)
    def test_reused_custom_and_builtin_ir_share_one_fast_recall(self):
        c=self.lib;want=self.custom_presets()
        self.assertEqual(c.ge_type_change(4,3),1);self.settle();c.ge_pump(350)
        self.assertEqual(c.ge_flash_save(27),1);self.settle()
        generation=c.qp_ir_generation();c.qp_reset_trace();self.load(27);c.ge_pump(350)
        self.assertEqual((c.qp_count(0),c.qp_count(2),c.qp_count(3)),(1,0,0))
        self.assertEqual(c.qp_ir_generation(),generation)
        self.assertEqual([c.ge_ir_coefficient(3,i) for i in range(512)],want)
        self.assertEqual(c.ge_actual_type(4),3)
    def two_custom_cabinets(self):
        c=self.lib;a=self.custom_presets();self.custom_cab(1,1)
        c.irf_cab(3,2);c.ge_pump(350)
        self.assertEqual(c.irf_busy(),0)
        b=[c.ge_ir_coefficient(3,i) for i in range(512)]
        self.assertNotEqual(a,b)
        self.assertEqual(c.ge_flash_save(27),1);self.settle()
        return {25:a,26:a,27:b}
    def test_different_irs_use_fast_route_then_upload_the_new_cabinet(self):
        c=self.lib;expected=self.two_custom_cabinets();c.qp_probe_again()
        for preset in (25,27,26,27):
            c.qp_reset_trace();revision=c.ge_actual_revision();self.load(preset);c.ge_pump(350)
            self.assertEqual(c.qp_count(0),1)
            self.assertEqual(c.qp_count(2),0)  # no multi-frame legacy preset transfer
            self.assertGreater(c.qp_count(3),0)  # changed coefficients still transfer
            self.assertEqual(c.ge_actual_revision(),revision+1)
            self.assertEqual([c.ge_ir_coefficient(3,i) for i in range(512)],expected[preset])

    def test_retain_mask_cannot_reuse_a_non_ir_slot(self):
        c=self.lib;self.load(0);q=bytearray(self.packet())
        struct.pack_into('<II',q,8,struct.unpack_from('<I',q,8)[0]+100,c.ge_actual_revision())
        non_ir=next(i for i in range(6) if c.ge_actual_type(i)!=3)
        q[20]=1<<non_ir;revision=c.ge_actual_revision()
        self.assertEqual(self.send(q)[1],13)  # BAD_ROUTE
        self.assertEqual(c.ge_actual_revision(),revision)
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
