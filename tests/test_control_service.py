"""Actual opt-in main-loop include: bounded phases, fairness and capacity fencing."""
import ctypes as ct
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
from prepare_gp5 import stage
from probe_gp5_startup import decode_counters
from test_link_rx import frame


class ControlServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try: cls.c=ct.CDLL(str(build(ROOT/('build/control_service'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/control_service_host.c')))
        except subprocess.CalledProcessError as e: raise RuntimeError(e.stderr) from e
        cls.c.cs_feed.argtypes=cls.c.cs_fifo.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint]
        cls.c.cs_poll.argtypes=[ct.c_uint]*3

    def setUp(self): self.c.cs_init()
    def feed(self,p,fifo=False):
        (self.c.cs_fifo if fifo else self.c.cs_feed)((ct.c_uint8*len(p))(*p),len(p))
    def step(self,report=0,delay=0):
        self.c.cs_poll(100,report,delay)
        self.c.cs_audio()
    def test_single_admission_survives_imposed_between_check_delay(self):
        self.feed(frame(b'\x30\x7f\x04\0'))
        self.c.cs_poll(100,1,20000)
        self.assertEqual((self.c.cs_value(3),self.c.cs_value(0)),(8,0))
        # Taking the token before parsing prevents dispatch on another fast pass.
        self.c.cs_poll(20100,0,0)
        self.assertEqual(self.c.cs_value(0),0)
        self.c.cs_audio();self.step();self.step()
        self.assertEqual((self.c.cs_value(0),self.c.cs_value(1)),(1,1))

    def test_continuous_reports_and_rx_both_make_progress(self):
        request=frame(b'\x40\x7f\x24\0'+bytes(32))
        for _ in range(4):
            self.feed(request)
            for _ in range(12):self.step(report=1)
        self.assertEqual(self.c.cs_value(0),4)
        self.assertGreater(self.c.cs_value(1),10)
        self.assertEqual(self.c.cs_value(10),0)
    def test_only_short_quick_polls_and_prepare_use_the_ram_dispatcher(self):
        for cmd,length,fast in ((0x7f4d,12,1),(0x7f4d,16,1),(0x7f4d,20,0),(0x7f4d,34,0),(0x7f30,12,0),(0x7f40,36,1)):
            self.c.cs_init();self.feed(frame(struct.pack('<HH',cmd,length)+bytes(length-4)))
            for _ in range(12):self.step()
            self.assertEqual(self.c.cs_value(0),1,(cmd,length))
            self.assertEqual(self.c.cs_value(12),fast,(cmd,length))

    def test_small_crc_and_dispatch_use_distinct_audio_epochs(self):
        self.feed(frame(b'\x40\x7f\x24\0'+bytes(32)))
        for progress in (8,16,24,32):
            self.step();self.assertEqual(self.c.cs_value(2),progress)
            self.assertEqual(self.c.cs_value(0),0)
        self.step();self.assertEqual(self.c.cs_value(3),40)
        self.assertEqual(self.c.cs_value(0),0)
        self.step();self.assertEqual(self.c.cs_value(0),1)

    def test_large_frames_keep_original_crc_budget(self):
        self.feed(frame(b'\x40\x7f'+bytes(506)))
        for progress in range(64,449,64):
            self.step();self.assertEqual(self.c.cs_value(2),progress)
        self.step();self.assertEqual(self.c.cs_value(3),512)
        self.step();self.assertEqual(self.c.cs_value(0),1)

    def test_verified_request_reserves_room_before_any_report(self):
        self.feed(frame(b'\x30\x7f\x04\0'));self.step()
        self.c.cs_room(515,1)
        self.step(report=1)
        self.assertEqual((self.c.cs_value(0),self.c.cs_value(1)),(0,0))
        self.assertEqual(self.c.cs_value(5),1)
        self.c.cs_room(516,1)
        self.step()
        self.assertEqual(self.c.cs_value(0),1)
        self.assertEqual(self.c.cs_value(1),0)

    def test_fifo_drains_when_admission_closed_then_reports_expose_progress(self):
        self.feed(b'\0\0\1\1',fifo=True)
        self.c.cs_poll(20000,1,0)
        self.assertEqual((self.c.cs_value(6),self.c.cs_value(4)),(4,4))
        self.assertEqual(self.c.cs_value(5),1)
        self.c.cs_audio();self.step()
        tail=bytes(self.c.cs_report(i) for i in range(24))
        counters=b'\x7e\x01'+struct.pack('<HHHBBBBBBH',1,1,0x4001,0,0,0,0,0,0,0)
        decoded=decode_counters(counters+tail)['rx_progress']
        self.assertEqual((decoded['bytes_seen'],decoded['buffered'],decoded['dispatched']),(4,4,0))
        self.assertEqual(self.c.cs_value(11),1076)

    def test_corrupt_frame_never_dispatches_and_report_flag_survives_closed_window(self):
        bad=bytearray(frame(b'\x30\x7f\x04\0'));bad[0]^=1;self.feed(bad)
        self.c.cs_poll(20000,1,0)
        self.assertEqual(self.c.cs_value(5),1)
        for _ in range(12):self.c.cs_audio();self.c.cs_poll(100,0,0)
        self.assertEqual(self.c.cs_value(0),0)
        self.assertEqual(self.c.cs_value(8),1)
        self.assertEqual(self.c.cs_value(1),1)

    def test_invalid_profile_fails_before_creating_stage(self):
        with tempfile.TemporaryDirectory() as folder,patch('prepare_gp5.subprocess.check_output',side_effect=AssertionError('git must not run')):
            dest=Path(folder)/'absent'
            for kw in (dict(),dict(audio_output='line-test',hot_ram=True),dict(audio_output='line-test',service_window_cycles=32768)):
                with self.assertRaises(ValueError):stage(Path(folder),dest,control_scheduler=True,**kw)
            with self.assertRaises(ValueError):stage(Path(folder),dest,control_scheduler=1)
            self.assertFalse(dest.exists())

    def test_legacy_and_malformed_diagnostics_keep_base_counters(self):
        base=b'\x7e\x01'+struct.pack('<HHHBBBBBBH',7,3,0x4001,0,0,0,0,0,0,0)
        self.assertNotIn('rx_progress',decode_counters(base+bytes(24)))
        result=decode_counters(base+b'\x01\x40\x18\0FRX1')
        self.assertEqual(result['frames'],7)
        self.assertIn('rx_progress_error',result)


if __name__=='__main__':unittest.main()
