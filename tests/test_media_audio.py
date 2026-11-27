"""Native media routing tests. No USB/radio timing or physical qualification."""
import ctypes as ct
import os
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import test_ir_link as ir_tests

class MediaIrLinkTests(ir_tests.IrLinkTests):
    test_silent_commit_block_starts_exchange_before_route_commit=ir_tests.RequiredRemoteTests.test_silent_commit_block_starts_exchange_before_route_commit
    test_upload_scratch_never_changes_mirrored_coefficients=ir_tests.RequiredRemoteTests.test_upload_scratch_never_changes_mirrored_coefficients
    test_bypassed_ir_upload_starts_exchange_before_chain_processing=ir_tests.RequiredRemoteTests.test_bypassed_ir_upload_starts_exchange_before_chain_processing
    test_active_ir_still_waits_for_current_block_input=ir_tests.RequiredRemoteTests.test_active_ir_still_waits_for_current_block_input
    test_empty_chain_also_starts_exchange_early=ir_tests.RequiredRemoteTests.test_empty_chain_also_starts_exchange_early
    @classmethod
    def setUpClass(cls):
        try:
            c=cls.c=ct.CDLL(str(build(ROOT/('build/media_ir'+('.dll' if os.name=='nt' else '.so')),
                ROOT/'tests/ir_link_host.c',includes=[ROOT/'tests/stubs'],
                flags=['-DFROST_USB_AUDIO=1','-DFROST_IR_FFT_PARTS=8u','-DFROST_IR_FFT_SPLIT_SPECTRUM=1'])))
        except subprocess.CalledProcessError as exc:raise RuntimeError(exc.stderr) from exc
        c.ln_level.restype=c.ln_scale.restype=ct.c_float

    def test_latched_dma_error_clears_without_restarting_the_link(self):
        c=self.setup();self.run_blocks([],blocks=700);before=c.ln_media_blocks();lost=c.ln_stat(15);controls=c.ln_flash_state(3)
        c.ln_media_dma_error();self.run_blocks([],blocks=60)
        self.assertGreater(c.ln_media_blocks(),before+20)
        self.assertEqual(c.ln_stat(15),lost)
        self.assertEqual(c.ln_flash_state(3),controls)
    def test_usb_payload_crosses_shared_frame_in_order_and_bad_frame_is_silent(self):
        c=self.setup();c.ln_media_play(0xc0002000);c.ln_media_record.restype=ct.c_uint;c.ln_media_received.restype=ct.c_uint
        self.run_blocks([],blocks=700)
        self.assertGreater(c.ln_media_blocks(),400)
        block=c.ln_stat(4)
        self.assertEqual([c.ln_media_record(i) for i in range(64)],[(block<<16)|i for i in range(64)])
        self.assertEqual(c.ln_media_received(63),0xc0002000)
        c.ln_media_overwrite_rx();self.assertEqual(c.ln_media_received(63),0xc0002000)
        self.assertEqual(c.ln_media_legacy_peer(),0)
        c.ln_set(0,0,0);self.run_blocks([],blocks=6);self.assertEqual(c.ln_media_received(63),0)

class MediaOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.c=ct.CDLL(str(build(ROOT/('build/media_output'+('.dll' if os.name=='nt' else '.so')),
                ROOT/'tests/media_audio_host.c',includes=[ROOT/'tests/stubs'])))
        except subprocess.CalledProcessError as exc:raise RuntimeError(exc.stderr) from exc
        cls.c.media_render.argtypes=[ct.c_float,ct.c_float,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint]
        cls.c.media_recorded.restype=ct.c_uint
    def setUp(self):self.c.line_init()
    def render(self,l=.25,r=-.5,play=0,off=0,valid=1,volume=65536,n=1):
        for _ in range(n):self.c.media_render(l,r,play,off,valid,volume)
    def test_recording_does_not_modify_an_inflight_dma_frame(self):
        self.c.media_dma_word.restype=ct.c_uint
        self.c.media_dma_sentinel();self.render()
        self.assertTrue(all(self.c.media_dma_word(i)==0xdeadbeef for i in range(240)))
        self.assertEqual(self.c.media_recorded(63),0xc0002000)
    def test_recording_is_stereo_before_master_and_excludes_usb_playback(self):
        self.render(play=0x20004000,volume=0)
        self.assertEqual(self.c.media_recorded(0),0xc0002000)
        self.assertEqual(self.c.media_recorded(63),0xc0002000)
        self.assertEqual(self.c.line_sample(63),0)
        self.render(play=0x20004000,n=260)
        self.assertAlmostEqual(self.c.line_sample(63)/2**31,.75,places=6)
        self.assertAlmostEqual(self.c.media_right(63)/2**31,-.25,places=6)
        self.assertEqual(self.c.media_recorded(63),0xc0002000)
    def test_direct_off_keeps_recording_and_playback_and_invalid_frame_restores_direct(self):
        self.render(off=1,play=0x20004000,n=260)
        self.assertAlmostEqual(self.c.line_sample(63)/2**31,.5,places=6)
        self.assertEqual(self.c.media_recorded(63),0xc0002000)
        self.render(off=1,play=0x20004000,valid=0)
        self.assertAlmostEqual(self.c.line_sample(63)/2**31,.25,places=6)
    def test_valid_chain_headroom_plus_playback_does_not_latch(self):
        self.render(l=16,r=-16,play=0x80007fff,volume=0)
        self.assertEqual(self.c.line_value(1),0)
    def test_recording_saturates_headroom_without_invalid_conversions(self):
        self.render(l=4,r=-4)
        self.assertEqual(self.c.media_recorded(63),0x80007fff)
        self.assertEqual(self.c.line_value(1),0)
        self.render(l=float('nan'))
        self.assertNotEqual(self.c.line_value(1),0)
        self.assertTrue(all(self.c.media_recorded(i)==0 and self.c.line_sample(i)==0 for i in range(64)))

if __name__=='__main__':unittest.main()
