import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from prepare_gp5 import stage

class LineTestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/line_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/line_host.c',includes=[ROOT/'tests/stubs'])))
        cls.e=ct.CDLL(str(build(ROOT/('build/line_editor'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/line_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.line_float_block.argtypes=[ct.c_float,ct.c_uint32];cls.c.line_bad_tail.argtypes=[ct.c_uint32,ct.c_uint32]
        cls.c.line_block.argtypes=[ct.c_int32,ct.c_uint32,ct.c_uint];cls.c.line_sample.restype=ct.c_int32
        cls.c.protocol_send.argtypes=[ct.c_uint32,ct.POINTER(ct.c_uint8),ct.c_uint]
    def setUp(self):self.c.line_init()
    def block(self,x=536870912,gain=65536,direct=0):
        self.assertEqual(self.c.line_block(x,gain,direct),1);self.assertEqual(self.c.line_equal(),1)
        values=[self.c.line_sample(i) for i in range(64)]
        self.assertLessEqual(max(abs(x) for x in values),2147483648)
        return values
    def test_startup_and_zero_request_are_silent_then_gain_ramps_and_returns_zero(self):
        self.assertEqual(self.block(gain=0),[0]*64)
        self.block();self.assertEqual(self.c.line_value(0),256)
        for _ in range(256):self.block()
        self.assertEqual(self.c.line_value(0),65536)
        for _ in range(256):self.block(gain=0)
        self.assertEqual(self.block(gain=0),[0]*64)
    def test_cap_holds_for_q31_extremes_and_corrupt_levels_latch_zero(self):
        for sample in (-2147483648,2147483647):
            self.c.line_init()
            for _ in range(257):self.block(sample,direct=1)
            self.assertEqual(self.c.line_value(0),65536)
        for level in (65537,0x7fffffff,0x80000000,0xffffffff):
            self.c.line_init();self.block(direct=1)
            self.assertEqual(self.block(gain=level,direct=1),[0]*64)
            self.assertEqual(self.c.line_value(1),4)
            self.assertEqual(self.block(direct=1),[0]*64)
    def test_live_route_type_and_order_edits_fade_and_resume_without_latch(self):
        self.c.line_ir_tap.restype=ct.c_float
        tap0=self.c.line_ir_tap(0)
        for _ in range(257):steady=self.block(x=268435456)
        level=max(abs(v) for v in steady);self.assertGreater(level,0)
        for variant in (1,2,0):
            self.assertEqual(self.c.line_route(variant),1)
            quiet=False
            for _ in range(400):
                v=self.block(x=268435456)
                quiet|=min(abs(q) for q in v)<0.05*level
                if not self.c.line_transition() and any(v):break
            self.assertTrue(quiet,'route change should fade through (near) silence')
            self.assertEqual(self.c.line_value(1),0,'live edit must not latch')
            self.assertTrue(any(self.block(x=268435456)),'sound resumes after the fade')
        self.assertEqual((self.c.line_ir_len(),self.c.line_ir_tap(0)),(128,tap0))   # IR slot kept its coefficients
    def test_ir_longer_than_256_fft_taps_still_latches_route(self):
        # 31a: the IR on the FFT workspace is admitted up to 4 partitions (256 taps).
        for _ in range(20):self.block()
        self.assertEqual(self.c.line_ir_taps(128),1);self.block();self.assertEqual(self.c.line_value(1),0)
        self.assertEqual(self.c.line_ir_taps(256),1);self.block();self.assertEqual(self.c.line_value(1),0)
        self.assertEqual(self.c.line_ir_taps(272),1)
        self.assertEqual(self.block(),[0]*64);self.assertEqual(self.c.line_value(1),8)
        self.c.line_ir_taps(128);self.assertEqual(self.block(),[0]*64)   # latched until reboot
    def test_numeric_route_and_late_faults_cut_whole_blocks_and_stay_latched(self):
        for fault,flag in ((self.c.line_eq_nan,1),(lambda:self.c.line_route_bad(1),8),(lambda:self.c.line_late(128),16)):
            self.c.line_init()
            for _ in range(12):self.block()
            fault();self.assertEqual(self.block(),[0]*64);self.assertTrue(self.c.line_value(1)&flag)
            self.c.line_route_bad(0);self.c.line_late(0)
            self.assertEqual(self.block(),[0]*64)
    def test_finite_clipping_stays_capped_and_recovers_without_clearing_counters(self):
        for sign in (-1,1):
            self.c.line_init();self.c.line_overload()
            for _ in range(300):out=self.block(sign*1610612736)
            self.assertTrue(any(out));self.assertEqual(self.c.line_value(1),0)
            clipped=self.c.line_value(4);self.assertGreater(clipped,0)
            self.c.line_unity_gain()
            for _ in range(200):self.block(sign*268435456)
            previous=self.c.line_value(4);out=self.block(sign*268435456)
            self.assertTrue(any(out));self.assertEqual(self.c.line_value(1),0)
            self.assertEqual(self.c.line_value(4),previous);self.assertGreaterEqual(previous,clipped)
            # Real numeric faults remain fatal even after an ordinary overload.
            self.c.line_eq_nan();self.assertEqual(self.block(),[0]*64)
            self.assertTrue(self.c.line_value(1)&1)
            self.assertEqual(self.block(),[0]*64)
    def test_hot_drive_ir_bypass_and_reenable_never_latch_finite_rails(self):
        for policy in (0,1):
            self.c.line_init();self.assertEqual(self.c.line_drive_ir(policy),1)
            for n in range(300):self.block(1610612736 if n%2 else -1610612736)
            self.assertEqual(self.c.line_value(1),0)
            self.assertEqual(self.c.line_ir_enable(0),1)
            for n in range(160):out=self.block(1610612736 if n%2 else -1610612736)
            self.assertTrue(any(out));self.assertGreater(self.c.line_value(4),0)
            self.assertEqual((self.c.line_value(1),self.c.line_enabled(2),self.c.line_enabled(3)),(0,1,0))
            self.assertEqual(self.c.line_ir_enable(1),1)
            for n in range(160):out=self.block(1610612736 if n%2 else -1610612736)
            previous=self.c.line_value(4);out=self.block(1610612736)
            self.assertTrue(any(out));self.assertEqual(self.c.line_value(4),previous)
            self.assertEqual((self.c.line_value(1),self.c.line_enabled(2),self.c.line_enabled(3)),(0,1,1))
    def test_startup_late_before_open_is_absorbed_but_late_after_open_latches(self):
        self.assertEqual([self.c.line_value(i) for i in (0,1,2,5,6)],[0,0,0,0,0])
        self.c.line_late(1)
        for _ in range(3):self.assertEqual(self.block(gain=0),[0]*64)
        self.assertEqual((self.c.line_value(1),self.c.line_value(5),self.c.line_value(6)),(0,0,1))
        for _ in range(257):self.block()
        self.assertEqual((self.c.line_value(0),self.c.line_value(1),self.c.line_value(5)),(65536,0,1))
        self.assertNotEqual(self.block(),[0]*64)
        self.c.line_late(2);self.assertEqual(self.block(),[0]*64)               # isolated late: one block shed
        self.assertNotEqual(self.block(),[0]*64)                                  # then audio resumes
        self.assertEqual((self.c.line_value(1),self.c.line_value(7),self.c.line_value(10)),(0,1,1))
        self.c.line_late(130);self.assertEqual(self.block(),[0]*64);self.assertEqual(self.c.line_value(1),16)   # burst of 128 latches
        self.assertEqual(self.block(gain=0),[0]*64)
    def test_lates_during_route_fade_and_grace_are_absorbed_not_latched(self):
        for _ in range(257):self.block(x=268435456)
        self.assertEqual(self.c.line_route(1),1);late=0
        while True:
            late+=1;self.c.line_late(late);self.block(x=268435456)      # every fade block late
            if not self.c.line_transition():break
        for _ in range(2):late+=1;self.c.line_late(late);self.block(x=268435456)   # grace blocks
        self.assertEqual((self.c.line_value(1),self.c.line_value(9)),(0,late))
        self.assertTrue(any(self.block(x=268435456)))
        for _ in range(48):self.block(x=268435456)   # grace (48 blocks) expires
        late+=128;self.c.line_late(late);self.assertEqual(self.block(),[0]*64);self.assertEqual(self.c.line_value(1),16)   # steady burst still latches
    def test_fade_backlog_sheds_to_catch_up_without_latching(self):
        """Revision 23f: a slow fade left a DMA backlog; lates after the fade
        latched. Now lates within the grace period shed (silence) instead, the
        fade itself always advances, and playback resumes once caught up."""
        for _ in range(257):self.block(x=268435456)
        self.assertEqual(self.c.line_route(1),1);late=0;fade_blocks=0
        while self.c.line_transition():
            late+=7;self.c.line_late(late);self.block(x=268435456);fade_blocks+=1   # very slow fade blocks
            self.assertLess(fade_blocks,10,'fade must keep advancing')
        shed=[]
        for _ in range(40):late+=1;self.c.line_late(late);shed.append(any(self.block(x=268435456)))  # backlog
        self.assertEqual(shed,[False]*40);self.assertEqual(self.c.line_value(1),0)   # shed, not latched
        self.assertTrue(any(self.block(x=268435456)))                                 # caught up: sound
        self.assertEqual(self.c.line_value(9),late);self.assertGreaterEqual(self.c.line_value(10),40)
    def test_lateness_cascade_is_broken_by_shedding_and_chronic_overload_latches(self):
        for _ in range(257):self.block()
        late=0;out=[]
        for _ in range(40):                       # a would-be cascade: late after every processed block
            late+=1;self.c.line_late(late);out.append(any(self.block()))
        self.assertEqual(out,[False]*40)          # each late sheds; nothing processed while behind
        self.assertEqual(self.c.line_value(1),0)  # 40 < 128: glitch, not silence
        self.assertTrue(any(self.block()))        # catches up and plays
        # One catch-up run counts once (revision 23h: 4 edits -> 61 lates latched).
        for _ in range(100):late+=1;self.c.line_late(late);self.block()
        self.assertEqual(self.c.line_value(1),0)  # a 100-block run is recovery, not chronic
        for _ in range(700):late+=1;self.c.line_late(late);self.block()
        self.assertEqual(self.c.line_value(1),16) # never catching up for >~1 s latches
    def test_many_separate_overruns_within_a_second_still_latch(self):
        for _ in range(257):self.block()
        late=0
        for _ in range(130):late+=1;self.c.line_late(late);self.block();self.block()  # late, then on time
        self.assertEqual(self.c.line_value(1),16) # >=128 separate events within ~1 s
    def test_isolated_lates_spread_over_windows_never_latch(self):
        for _ in range(257):self.block()
        late=0
        for _ in range(5):
            for step in range(127):
                late+=1;self.c.line_late(late);self.assertEqual(self.block(),[0]*64);self.assertNotEqual(self.block(),[0]*64)
            for _ in range(689):self.block()
        self.assertEqual((self.c.line_value(1),self.c.line_value(7),self.c.line_value(10)),(0,635,635))
    def test_open_is_sticky_and_late_counter_reset_after_open_latches(self):
        self.c.line_late(3);self.block(gain=0);self.assertEqual(self.c.line_value(5),0);self.block(gain=771)
        self.assertEqual(self.c.line_value(5),1)
        for _ in range(3):self.block(gain=0)
        self.assertEqual((self.c.line_value(0),self.c.line_value(5),self.c.line_value(1)),(0,1,0))
        self.c.line_late(0);self.assertEqual(self.block(),[0]*64);self.assertEqual(self.c.line_value(1),16)
    def test_level_fault_alone_never_marks_open(self):
        self.block(gain=0x80000000,direct=1)
        self.assertEqual((self.c.line_value(1),self.c.line_value(5)),(4,0))
    def test_controller_vol_sets_level_directly_with_zero_deadband(self):
        e=self.e;e.ge_init()
        for knob,level in ((255,255),(127,127),(3,3),(2,0),(0,0),(300,0),(64,64)):
            e.le_knob(knob);self.assertEqual(e.le_level(),level)
        e.le_reconnect();self.assertEqual(e.le_level(),0)
    def test_unity_master_removes_legacy_twelve_db_loss(self):
        for _ in range(257):self.c.line_float_block(.5,65536)
        self.assertEqual(self.c.line_sample(63),1073741824)
        self.assertEqual(self.c.line_value(0),65536)
        self.assertEqual(self.c.line_value(4),0)
    def test_master_preserves_over_unity_waveform_before_pcm_conversion(self):
        # A 2x peak at quarter master must be .5, not the old clipped .25.
        for _ in range(257):self.c.line_float_block(2.,16384)
        for value in (-3.,-2.,-.5,0.,.5,2.,3.):
            self.c.line_float_block(value,16384)
            self.assertEqual(self.c.line_sample(63),int(value*.25*2147483648))
        self.assertEqual(self.c.line_value(4),0)
        # Real full-scale overload clips, and lowering master restores headroom.
        for _ in range(257):self.c.line_float_block(2.,65536)
        self.assertEqual(self.c.line_sample(63),2147483520)
        rails=self.c.line_value(4);self.assertGreater(rails,0)
        for _ in range(257):self.c.line_float_block(2.,16384)
        rails=self.c.line_value(4);self.c.line_float_block(2.,16384)
        self.assertEqual(self.c.line_sample(63),1073741824)
        self.assertEqual(self.c.line_value(4),rails)
    def test_invalid_float_boundary_cuts_entire_block_even_at_zero_master(self):
        for raw in (0x7fc00000,0x7f800000,0xff800000,0x41800001,0xc1800001):
            for master in (0,65536):
                self.c.line_init()
                for _ in range(257):self.c.line_float_block(.5,master)
                self.c.line_bad_tail(raw,master)
                self.assertEqual([self.c.line_sample(i) for i in range(64)],[0]*64)
                self.assertEqual(self.c.line_value(1),32)
                self.c.line_float_block(.5,master)
                self.assertEqual(self.c.line_sample(63),0)
    def test_invalid_profile_combinations_reject_before_filesystem_or_git(self):
        for kwargs in ({'audio_input':'synthetic'},{'audio_input':'adc','controller':'diagnostic'},
                       {'audio_input':'adc','bank':Path('missing.fbank')}):
            with self.assertRaises(ValueError):stage(Path('missing'),Path('missing-stage'),audio_output='line-test',**kwargs)
    def test_line_status_reports_cap_and_closed_state_read_only(self):
        p=struct.pack('<HHII',0x7f3b,12,1,123)
        self.c.protocol_send(0x7f3b,(ct.c_uint8*12)(*p),12)
        raw=bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
        v=struct.unpack('<HH11I',raw)
        self.assertEqual(v[:5],(0x7f3b,48,1,123,0))
        self.assertEqual(v[5:9],(1,65536,0,0));self.assertEqual(v[11:],(2,1))
    def exchange(self,p):
        self.c.protocol_send(0x7f3b,(ct.c_uint8*len(p))(*p),len(p))
        return bytes(self.c.protocol_reply(i) for i in range(self.c.protocol_reply_bytes()))
    def test_host_codec_tracks_open_ramp_fault_and_rejects_corruption(self):
        import line_meter
        d=line_meter.read(self.exchange,7)
        self.assertEqual((d['mode'],d['gain_q16'],d['closed'],d['flags']),('line-test',0,True,0))
        for _ in range(3):self.block()
        d=line_meter.read(self.exchange,8)
        self.assertEqual((d['gain_q16'],d['closed'],d['last_requested_q16']),(768,False,65536))
        self.c.line_late(128);self.block();self.c.line_late(0)
        d=line_meter.read(self.exchange,9)
        self.assertEqual((d['gain_q16'],d['closed'],d['flag_names']),(0,True,['late']))
        raw=self.exchange(line_meter.request(10))
        for offset,value in ((20,656),(24,65537),(44,0),(28,64)):
            bad=bytearray(raw);bad[offset:offset+4]=struct.pack('<I',value)
            with self.assertRaises(ValueError):line_meter.decode(bytes(bad),10)
        with self.assertRaises(ValueError):line_meter.decode(raw,11)
        with self.assertRaises(ValueError):line_meter.request(0)
        muted=struct.pack('<HH11I',0x7f3b,48,1,5,0,0,0,0,0,0,0,2,1)
        self.assertEqual(line_meter.decode(muted,5)['mode'],'muted')
        with self.assertRaises(ValueError):line_meter.decode(muted[:24]+struct.pack('<I',1)+muted[28:],5)


class LateReleaseTests(unittest.TestCase):
    """31y14 (FROST_LINE_LATE_RELEASE): an overload-only latch releases after N
    blocks without a new late block; numeric faults still latch for good."""
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/line_release_host'+('.dll' if os.name=='nt' else '.so')),
                              ROOT/'tests/line_host.c',includes=[ROOT/'tests/stubs'],flags=['-DFROST_LINE_LATE_RELEASE=20u'])))
        cls.c.line_float_block.argtypes=[ct.c_float,ct.c_uint32];cls.c.line_bad_tail.argtypes=[ct.c_uint32,ct.c_uint32]
        cls.c.line_block.argtypes=[ct.c_int32,ct.c_uint32,ct.c_uint];cls.c.line_sample.restype=ct.c_int32
    def block(self,x=536870912,gain=65536):
        self.c.line_block(x,gain,0);return [self.c.line_sample(i) for i in range(64)]
    def open_output(self):
        self.c.line_init()
        for _ in range(260):self.block()
        self.assertNotEqual(self.block(),[0]*64)
    def test_overload_latch_releases_after_quiet_blocks(self):
        self.open_output()
        self.c.line_late(130);self.assertEqual(self.block(),[0]*64);self.assertEqual(self.c.line_value(1),16)
        for _ in range(19):self.assertEqual(self.block(),[0]*64)
        self.assertEqual(self.c.line_value(1),16)                              # still latched before N quiet blocks
        self.block();self.assertEqual(self.c.line_value(1),0)                  # released
        for _ in range(400):out=self.block()
        self.assertNotEqual(out,[0]*64)                                         # gain ramped back up
    def test_new_lates_restart_the_quiet_count(self):
        self.open_output();self.c.line_late(130);self.block()
        for k in range(30):self.c.line_late(131+k);self.block()
        self.assertEqual(self.c.line_value(1),16)
    def test_numeric_fault_still_latches_for_good(self):
        self.open_output();self.c.line_eq_nan()
        for _ in range(60):self.block()
        self.assertNotEqual(self.c.line_value(1),0)

if __name__=='__main__':unittest.main()
