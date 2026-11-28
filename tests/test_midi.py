"""MIDI through the real controller queue, DSP and simulated persistent storage."""
import ctypes as ct
import os
import struct
import subprocess
import shutil
import unittest
import test_live_host as live_tests
from test_live_host import ROOT
from host import build

class MidiTests(live_tests.LiveHostTests):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/midi_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.ge_actual_value.restype=ct.c_float
    def cc(self, cc, value): self.c.ge_midi(11, 0xb0, cc, value)
    def pc(self, preset): self.c.ge_midi(12, 0xc0, preset, 0)
    def test_all_effect_parameters_reach_dsp_at_both_midi_endpoints(self):
        c=self.c;c.ge_to_dsp.argtypes=[ct.c_uint,ct.c_uint,ct.c_int];c.ge_to_dsp.restype=ct.c_float
        for slot in range(6):
            self.assertEqual(self.host([0x7e,0x17,slot,255])[2],1);self.settle(60)
        for effect in range(52):
            self.assertEqual(self.host([0x7e,0x17,0,effect])[2],1);self.settle(60)
            self.assertEqual(c.ge_actual_type(0),effect)
            for param in range(self.slot(0)['count']):
                for midi,field in ((0,0),(127,1)):
                    target=c.ge_human_meta(effect,param,field)
                    self.cc(32+param,midi);self.settle(40)
                    self.assertEqual(self.slot(0)['values'][param],target,(effect,param,midi))
                    self.assertAlmostEqual(c.ge_actual_value(0,param),c.ge_to_dsp(effect,param,target),places=4)
    def test_switches_expression_and_rapid_cc_coalesce(self):
        self.cc(20,0);self.settle();self.assertEqual(self.c.ge_actual_enabled(0),0)
        self.cc(20,127);self.settle();self.assertEqual(self.c.ge_actual_enabled(0),1)
        self.cc(80,0);self.cc(81,0)
        for value in range(128):self.cc(11,value)
        self.settle();self.assertEqual(self.slot(0)['values'][0],100)
        self.cc(7,0);self.assertEqual(self.c.ge_midi_volume(),0)
        self.cc(7,127);self.assertEqual(self.c.ge_midi_volume(),127)
    # External footswitch gestures (channel 16): what a Chocolate Plus set up by the Controller page sends.
    def fsw_press(self,n,tap=None,double=None,hold=None,define=True,pair=None,joins=None):
        """Like the controller: every press repeats the footswitch's definition (the last one given here).
        pair: the action of n and n+1 together; joins: n is in a pair (its own or the previous footswitch's)."""
        defs=self.__dict__.setdefault('fsw_defs',{});pairs=self.__dict__.setdefault('fsw_pairs',{});joined=self.__dict__.setdefault('fsw_joins',{})
        if tap or double or hold:defs[n]=(*(tap or (0,0,0,0)),*(double or (0,0,0,0)),*(hold or (0,0,0,0)))
        if pair is not None:pairs[n]=pair
        if joins is not None:joined[n]=joins
        kind,a,b,c=pairs.get(n,(0,0,0,0))
        if define:
            for i,v in enumerate(defs.get(n,(0,)*12)):self.c.ge_midi(11,0xbf,16+i,v)
            if kind:
                if b:self.c.ge_midi(11,0xbf,30,b)
                if c:self.c.ge_midi(11,0xbf,31,c)
                self.c.ge_midi(11,0xbf,32+kind,a)
        self.c.ge_midi(11,0xbf,38 if kind or joined.get(n) else 28,n)
    def fsw_release(self,n):self.c.ge_midi(11,0xbf,29,n)
    def fsw_wait(self,ms):
        self.c.ge_advance(ms);self.c.ge_tick()
    def test_footswitch_tap_runs_on_the_press_without_double_or_hold(self):
        c=self.c;BYPASS=(3,90,0,0)
        self.assertEqual(c.ge_midi_bypass(),0)
        self.fsw_press(0,tap=BYPASS);self.assertEqual(c.ge_midi_bypass(),1)        # toggled at once
        self.fsw_release(0);self.fsw_wait(500);self.assertEqual(c.ge_midi_bypass(),1)
        self.fsw_press(0,define=False);self.fsw_release(0);self.assertEqual(c.ge_midi_bypass(),0)   # the setup is kept
        self.fsw_press(5,define=False);self.fsw_release(5);self.fsw_wait(3000);self.assertEqual(c.ge_midi_bypass(),0)   # never set up: nothing
    def test_footswitch_double_press_and_late_second_press(self):
        c=self.c;BYPASS=(3,90,0,0);BLOCK1=(3,20,0,0)
        before=c.ge_enabled(0)
        self.fsw_press(1,tap=BYPASS,double=BLOCK1);self.fsw_release(1)
        self.assertEqual(c.ge_midi_bypass(),0)                                         # waits for a possible second press
        self.fsw_wait(401);self.assertEqual(c.ge_midi_bypass(),1);self.assertEqual(c.ge_enabled(0),before)
        self.fsw_press(1);self.fsw_release(1);c.ge_advance(380)                        # a second press within 400 ms
        self.fsw_press(1);self.settle(20);self.assertEqual(c.ge_enabled(0),1-before)   # double press: the effect (once the pedal is idle)
        self.fsw_release(1);self.fsw_wait(500);self.assertEqual(c.ge_midi_bypass(),1)  # and no tap with it
        self.fsw_press(1);self.fsw_release(1);c.ge_advance(500)
        self.fsw_press(1);self.assertEqual(c.ge_midi_bypass(),0)                       # too late (over 400 ms): two taps
        self.fsw_release(1);self.fsw_wait(401);self.assertEqual(c.ge_midi_bypass(),1);self.settle(20);self.assertEqual(c.ge_enabled(0),1-before)
    def test_footswitch_hold_two_seconds_and_a_short_press(self):
        c=self.c;TUNER=14;BYPASS=(3,90,0,0);TUNER_TOGGLE=(3,86,0,0)
        self.fsw_press(2,tap=BYPASS,hold=TUNER_TOGGLE);self.fsw_wait(1500)
        self.assertNotEqual(c.ge_view(),TUNER);self.assertEqual(c.ge_midi_bypass(),0)
        self.fsw_wait(500);self.settle(20);self.assertEqual(c.ge_view(),TUNER)         # 2 s held: the tuner opens
        self.fsw_release(2);self.fsw_wait(100);self.assertEqual(c.ge_midi_bypass(),0)  # no tap on that release
        self.fsw_press(2);self.fsw_wait(2000);self.fsw_release(2);self.settle(20);self.assertNotEqual(c.ge_view(),TUNER)   # toggled back
        self.fsw_press(2);self.fsw_wait(300);self.fsw_release(2);self.assertEqual(c.ge_midi_bypass(),1)    # a short press is the tap
    def test_footswitch_tap_tempo_is_timed_on_the_press(self):
        c=self.c;TAP=(1,85,127,0);BYPASS=(3,90,0,0)
        self.fsw_press(3,tap=TAP,double=BYPASS,hold=BYPASS);c.ge_advance(150);self.fsw_release(3)
        c.ge_advance(350);self.fsw_press(3);self.assertEqual(c.ge_midi_tempo(),500)     # 500 ms between the presses, however long each was held
        self.fsw_release(3);self.fsw_wait(400);self.assertEqual(c.ge_midi_bypass(),0)  # no double press on a tap tempo footswitch
        self.fsw_press(3);self.fsw_wait(2000);self.assertEqual(c.ge_midi_bypass(),1)    # the hold still works
        self.fsw_release(3)
    def test_footswitch_switch_pressed_while_busy_keeps_its_parity(self):
        """Presses that wait for a busy pedal: a switch pressed twice cancels out, three times switches once;
        a fixed value runs once (each run when the pedal is idle again)."""
        c=self.c;BLOCK1=(3,20,0,0);OFF1=(1,20,0,0)
        self.settle(20);self.fsw_press(0,tap=BLOCK1);self.fsw_release(0);self.settle(40)
        for presses in (2,3,1,4):
            start=c.ge_enabled(0)
            c.ge_host_wrote_flash();self.assertEqual(c.ge_midi_editable(),0)
            for _ in range(presses):self.fsw_press(0);self.fsw_release(0)
            self.settle(5);self.assertEqual(c.ge_enabled(0),start)                     # nothing while busy
            self.fsw_wait(2001);self.settle(40);self.assertEqual(c.ge_midi_editable(),1)
            self.assertEqual(c.ge_enabled(0),start^(presses&1),presses)
        c.ge_host_wrote_flash()
        for _ in range(2):self.fsw_press(0,tap=OFF1);self.fsw_release(0)
        self.fsw_wait(2001);self.settle(40);self.assertEqual(c.ge_enabled(0),0)
    def test_footswitch_switches_the_preset_ctl_effects(self):
        c=self.c;CTL=(5,0,0,0)
        before=[c.ge_enabled(i) for i in range(6)]
        c.ge_set_ctl_mask(0b000011)                                                    # this preset's CTL: slots 0 and 1
        self.fsw_press(0,tap=CTL);self.fsw_release(0);self.settle(40)
        self.assertEqual([c.ge_enabled(i) for i in range(6)],[v^(i in (0,1)) for i,v in enumerate(before)])
        self.fsw_press(0);self.fsw_release(0);self.settle(40)
        self.assertEqual([c.ge_enabled(i) for i in range(6)],before)
        c.ge_set_ctl_mask(0);self.fsw_press(0);self.fsw_release(0);self.settle(40)     # a preset without CTL effects: nothing
        self.assertEqual([c.ge_enabled(i) for i in range(6)],before)
    # Two neighbours pressed together: the pair's action rides on the left footswitch (A+B on A).
    def test_footswitch_pair_runs_instead_of_both(self):
        c=self.c;TUNER=14;BYPASS=(3,90,0,0);VOL=(1,7,100,0);TUNER_TOGGLE=(3,86,0,0)
        self.cc(7,0);bypass=c.ge_midi_bypass()
        self.fsw_press(0,tap=BYPASS,hold=BYPASS,pair=TUNER_TOGGLE);self.fsw_wait(30)
        self.fsw_press(1,tap=VOL,joins=True)                                            # B 30 ms after A
        self.assertEqual(c.ge_midi_bypass(),bypass);self.assertEqual(c.ge_midi_volume(),0)   # neither tap
        self.settle(20);self.assertEqual(c.ge_view(),TUNER)                             # but the pair: the tuner
        self.fsw_wait(2500);self.fsw_release(1);self.fsw_release(0);self.fsw_wait(500)
        self.assertEqual(c.ge_midi_bypass(),bypass);self.assertEqual(c.ge_midi_volume(),0)   # no hold, no tap on release
        self.fsw_press(1);self.fsw_wait(60);self.fsw_press(0);self.settle(20)          # the other order: B first
        self.assertNotEqual(c.ge_view(),TUNER)
        self.fsw_release(0);self.fsw_release(1);self.fsw_wait(500)
        self.assertEqual(c.ge_midi_bypass(),bypass);self.assertEqual(c.ge_midi_volume(),0)
    def test_footswitch_in_a_pair_acts_alone_after_100_ms(self):
        c=self.c;TUNER=14;BYPASS=(3,90,0,0);VOL=(1,7,100,0);TUNER_TOGGLE=(3,86,0,0)
        self.cc(7,0)
        self.fsw_press(1,tap=VOL,joins=True);self.fsw_wait(99);self.assertEqual(c.ge_midi_volume(),0)   # still waiting for A
        self.fsw_wait(1);self.settle(2);self.assertEqual(c.ge_midi_volume(),100)       # alone: its own tap
        bypass=c.ge_midi_bypass()
        self.fsw_press(0,tap=BYPASS,hold=BYPASS,pair=TUNER_TOGGLE);self.settle(20)     # B already acted: no pair
        self.assertNotEqual(c.ge_view(),TUNER)
        self.fsw_release(0);self.fsw_release(1);self.assertEqual(c.ge_midi_bypass(),1-bypass)   # A's tap, on release (A has a hold)
        self.cc(7,0);self.fsw_press(1);self.fsw_wait(40);self.fsw_release(1)          # a quick press acts on its release
        self.assertEqual(c.ge_midi_volume(),100)
        self.cc(7,0);self.fsw_press(0);self.fsw_wait(150);self.fsw_press(1)           # B 150 ms after A: no pair
        self.assertEqual(c.ge_midi_volume(),0)                                          # B waits its own 100 ms
        self.fsw_wait(100);self.settle(2);self.assertEqual(c.ge_midi_volume(),100)
        self.settle(20);self.assertNotEqual(c.ge_view(),TUNER)
        self.fsw_release(0);self.fsw_release(1)
    def test_tap_tempo_in_a_pair_keeps_its_spacing(self):
        c=self.c;TAP=(1,85,127,0)
        self.fsw_press(3,tap=TAP,joins=True);self.fsw_wait(30);self.fsw_release(3)     # released before 100 ms
        self.fsw_wait(70)                                                               # its tap: 100 ms after the press
        self.fsw_wait(400);self.fsw_press(3);self.fsw_wait(100)                         # 500 ms later, held longer
        self.assertEqual(c.ge_midi_tempo(),500);self.fsw_release(3)
    def test_led_colours_from_the_website(self):
        """CC 40/41 on channel 16 (the Controller page's colour picker): shown at once, saved 1.5 s after the last change."""
        c=self.c;LED=lambda r,g,b:0x01000000|r|(g<<8)|(b<<16)
        c.ge_midi(11,0xbf,40,6);c.ge_midi(11,0xbf,41,1)                                # RED when on, DARK when off
        self.assertEqual((c.ge_led_choice(0),c.ge_led_choice(1)),(6,1))
        self.assertIn(c.ge_led(),(LED(32,0,0),LED(0,0,0)))                              # the footswitch shows it at once
        c.ge_midi(11,0xbf,40,11);c.ge_midi(11,0xbf,41,5)                              # out of range: ignored
        self.assertEqual((c.ge_led_choice(0),c.ge_led_choice(1)),(6,1))
        self.settle(50);c.ge_reboot();self.settle()                                    # 1 s later: not saved yet
        self.assertEqual((c.ge_led_choice(0),c.ge_led_choice(1)),(0,0))
        c.ge_midi(11,0xbf,40,2);self.settle(40);c.ge_midi(11,0xbf,41,3);self.settle(100)   # BLUE, then DIM WHITE; 2 s later
        c.ge_reboot();self.settle()
        self.assertEqual((c.ge_led_choice(0),c.ge_led_choice(1)),(2,3))                # saved with the global settings
    def test_footswitch_control_alternates_two_values(self):
        c=self.c;MIX=(4,32,127,0)
        self.fsw_press(0,tap=MIX);self.fsw_release(0);self.settle(40);first=self.slot(0)['values'][0]
        self.fsw_press(0);self.fsw_release(0);self.settle(40);second=self.slot(0)['values'][0]
        self.fsw_press(0);self.fsw_release(0);self.settle(40)
        self.assertNotEqual(first,second);self.assertEqual(self.slot(0)['values'][0],first)
    def test_bad_channels_packets_and_unsupported_parameters_are_ignored(self):
        before=self.slot(0)
        for packet in ((11,0xb1,32,127),(27,0xb0,32,127),(12,0xb0,32,127),(11,0xb0,32,255),(11,0xb0,39,127)):
            self.c.ge_midi(*packet)
        self.settle();self.assertEqual(self.slot(0),before)
        self.assertEqual(self.host([0x7e,0x1e]),b'\x7e\x1e\x01FROST_FW:0.1.0\x00')
    def test_scene_is_one_revision_and_replays_after_lost_reply(self):
        before=self.chain()[3]
        expected=[int(t!=255 and bool(21&(1<<s))) for s,t in enumerate(self.chain()[0])]
        self.c.ge_drop(1,1);self.cc(89,21);self.settle()
        self.assertEqual(self.chain()[3],before+1)
        self.assertEqual(self.chain()[2],expected)
        self.assertEqual([self.c.ge_actual_enabled(s) for s in range(6)],expected)
        before=self.chain();self.cc(89,64);self.settle();self.assertEqual(self.chain(),before)
    def test_program_change_mailbox_website_recall_and_navigation_write_flash_once_settled(self):
        """Quick switching never writes flash (a write pauses the IR link); beta.11:
        the preset you stop on is written once after 2 s and is the boot preset."""
        for preset in (2,5,9):
            self.assertEqual(self.c.ge_flash_save(preset),1);self.settle()
        writes=self.c.ge_flash_ops();quick=lambda:self.settle(40)          # 0.8 s per step
        self.pc(2);self.pc(5);quick();self.assertEqual(self.c.ge_current_preset(),5)
        self.assertEqual(self.c.ge_flash_ops(),writes)
        self.pc(127);quick();self.assertEqual(self.c.ge_current_preset(),5)
        self.cc(87,127);quick();self.assertEqual(self.c.ge_current_preset(),9)
        self.cc(87,0);quick();self.assertEqual(self.c.ge_current_preset(),9)
        self.cc(88,127);quick();self.assertEqual(self.c.ge_current_preset(),5)
        self.assertEqual(self.host([0x7e,0x1d,2]),b'\x7e\x1d\x01');quick()
        status=self.host([0x7e,0x1c]);self.assertEqual(status[3:7],bytes([2,255,0,0]))
        self.assertEqual(self.host([0x7e,0x1d,60]),b'\x7e\x1d\x00')
        self.assertEqual(self.c.ge_flash_ops(),writes)
        self.settle();self.assertGreater(self.c.ge_flash_ops(),writes);self.assertEqual(self.c.ge_global(6),2)
        once=self.c.ge_flash_ops();self.settle();self.assertEqual(self.c.ge_flash_ops(),once)
        self.c.ge_reboot();self.settle();self.assertEqual(self.c.ge_current_preset(),2)
    def test_tuner_bypass_and_preserved_effect_states(self):
        before=self.chain()[2]
        self.cc(90,127);self.settle();self.assertEqual(self.c.ge_dsp_global_flags()&2,2)
        self.assertEqual(self.chain()[2],before)
        self.cc(86,127);self.settle();self.assertEqual(self.c.ge_view(),14)
        self.cc(86,0);self.settle();self.assertNotEqual(self.c.ge_view(),14)
        self.cc(90,0);self.settle();self.assertEqual(self.c.ge_dsp_global_flags()&2,0)
        self.assertEqual(self.chain()[2],before)
        self.c.ge_bypass_sample.argtypes=[ct.c_uint,ct.c_float,ct.c_float];self.c.ge_bypass_sample.restype=ct.c_float
        self.assertAlmostEqual(self.c.ge_bypass_sample(1,.25,.75),.25,places=5)
        self.assertAlmostEqual(self.c.ge_bypass_sample(0,.25,.75),.75,places=5)
    def test_global_bypass_precedes_unsent_expression_edits(self):
        self.cc(11,0);self.cc(90,127);self.c.ge_pump(1)
        self.assertEqual(self.c.ge_dsp_global_flags()&2,2)
    def test_tap_and_clock_control_delay_and_modulation(self):
        for slot,effect in ((4,5),(5,6)):
            self.assertEqual(self.host([0x7e,0x17,slot,effect])[2],1);self.settle()
        self.cc(85,127);self.c.ge_advance(500);self.cc(85,127);self.settle()
        self.assertEqual(self.slot(4)['values'][0],500)
        self.assertEqual(self.slot(5)['values'][0],200)
        self.cc(91,127)
        for _ in range(25):self.c.ge_advance(21);self.c.ge_midi(15,0xf8,0,0)
        self.settle();self.assertEqual(self.c.ge_midi_tempo(),504)
        self.c.ge_midi(15,0xfc,0,0)
        for _ in range(25):self.c.ge_advance(12);self.c.ge_midi(15,0xf8,0,0)
        self.assertEqual(self.c.ge_midi_tempo(),504)
        self.cc(91,0);self.c.ge_midi(15,0xfa,0,0)
        self.assertEqual(self.c.ge_midi_tempo(),504)
    def test_tap_tempo_settings_and_tap_note(self):
        c=self.c
        for slot,effect in ((4,5),(5,6)):
            self.assertEqual(self.host([0x7e,0x17,slot,effect])[2],1);self.settle()
        self.cc(92,127);self.cc(94,127);self.cc(93,1)
        self.assertEqual((c.ge_tap_tempo_on(),c.ge_tap_target_on(),c.ge_tap_note_value()),(1,1,1))
        self.assertEqual(c.ge_tap_saved(),0)                           # overrides only: the saved settings are untouched
        self.cc(85,127);c.ge_advance(500);self.cc(85,127);self.settle()
        self.assertEqual(self.slot(4)['values'][0],375)                # dotted eighth of a 500 ms beat
        self.assertEqual(self.slot(5)['values'][0],267)                # one cycle per 375 ms
        self.cc(93,3);self.assertEqual(c.ge_tap_note_value(),1)        # out of range: ignored
        self.cc(93,2);self.cc(85,127);c.ge_advance(400);self.cc(85,127);self.settle()
        self.assertEqual(self.slot(4)['values'][0],200)                # eighth of a 400 ms beat
        self.cc(92,0);self.cc(94,0);self.cc(93,0)
        self.assertEqual((c.ge_tap_tempo_on(),c.ge_tap_target_on(),c.ge_tap_note_value()),(0,0,0))
        self.cc(85,127);c.ge_advance(500);self.cc(85,127);self.settle()
        self.assertEqual(self.slot(4)['values'][0],500)                # MIDI tap works with TAP TEMPO OFF
        self.cc(92,127);c.ge_reboot();self.settle();self.assertEqual(c.ge_tap_tempo_on(),0)   # restart drops the override

class WebsiteRecallTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'),'Node.js needed')
    def test_recall_completion_errors_and_non_repetition(self):
        subprocess.run(['node',str(ROOT/'tests/js/midi_presets.mjs')],cwd=ROOT,check=True,capture_output=True,text=True)

class MidiAudioSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/midi_audio'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/line_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.line_sample.restype=ct.c_int32
    def bypass(self):
        p=struct.pack('<HH6I',0x7f4a,28,1,10,1,0,0,2)
        self.c.protocol_send(0x7f4a,(ct.c_uint8*len(p))(*p),len(p))
        self.assertEqual(self.c.protocol_reply(12),0)
    def block(self):
        self.assertEqual(self.c.line_block(268435456,65536,0),1)
        return [self.c.line_sample(i) for i in range(64)]
    def setUp(self):
        self.c.line_init();self.bypass()
        for _ in range(257):self.block()
    def test_global_bypass_is_dry_and_numeric_fault_stays_muted(self):
        self.assertEqual(self.block(),[268435456]*64)
        self.c.line_eq_nan();self.assertEqual(self.block(),[0]*64)
        self.assertEqual(self.block(),[0]*64)
    def test_bypass_does_not_defeat_overload_shedding(self):
        self.c.line_late(1);self.assertEqual(self.block(),[0]*64)

if __name__=='__main__':unittest.main()
