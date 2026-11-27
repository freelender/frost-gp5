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
