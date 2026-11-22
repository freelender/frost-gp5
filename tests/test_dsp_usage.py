"""Passive DSP meter codec, cadence and controller rendering; host checks only."""
import ctypes as ct
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from host import build

def report(cycles=None):
    code=0 if cycles is None else min(511,((cycles+1024)>>11)+1)
    return struct.pack('<HH5I',0x4001,24,0x31585246,123,456,((code>>7)<<30)|17|(3<<10)|(40<<20),(code&127)|(2<<7)|(13<<10)|(9<<18))

class DSPUsageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/dsp_usage_host'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/dsp_usage_host.c',includes=[ROOT/'tests/stubs'])))
        cls.c.du_report.argtypes=[ct.c_char_p,ct.c_uint,ct.c_uint]
        cls.c.du_ui_report.argtypes=cls.c.du_report.argtypes
        cls.c.du_text.restype=ct.c_char_p;cls.c.du_ui_pixels.restype=ct.POINTER(ct.c_uint16)
    def send(self,cycles,now):
        p=report(cycles);self.c.du_report(p,len(p),now)
    def test_refreshes_every_2500ms_and_uses_peak(self):
        c=self.c;c.du_init(0);self.send(100000,100);self.send(234000,2000)
        self.assertEqual(c.du_text(),b'DSP --%');self.assertEqual(c.du_tick(2499),0)
        self.assertEqual(c.du_tick(2500),1);self.assertEqual(c.du_text(),b'DSP 84%')
        self.send(100000,3000);self.assertEqual(c.du_tick(4999),0);self.assertEqual(c.du_tick(5000),1)
        self.assertEqual(c.du_text(),b'DSP 36%')
    def test_stale_or_unavailable_is_unknown_not_zero(self):
        c=self.c;c.du_init(0);self.send(200000,100);c.du_tick(2500);self.assertEqual(c.du_text(),b'DSP 72%')
        c.du_tick(5000);self.assertEqual(c.du_text(),b'DSP 72%');c.du_tick(7500);self.assertEqual(c.du_text(),b'DSP --%')
        c.du_init(0);self.send(None,100);c.du_tick(2500);self.assertEqual(c.du_text(),b'DSP --%')
    def test_fault_invalidates_and_malformed_frames_are_ignored(self):
        c=self.c;c.du_init(0);self.send(200000,100);c.du_tick(2500)
        for p in (b'',report(1000)[:-1],b'\x02'+report(1000)[1:]):c.du_report(p,len(p),2600)
        c.du_tick(5000);self.assertEqual(c.du_text(),b'DSP 72%')
        p=struct.pack('<HH6I',0x4001,28,0x31544c46,0,0,0,0,0);c.du_report(p,len(p),5100)
        c.du_tick(7500);self.assertEqual(c.du_text(),b'DSP --%')
    def test_clock_wrap_and_over_budget_display(self):
        c=self.c;start=0xffffff00;c.du_init(start);self.send(301000,start)
        self.assertEqual(c.du_tick((start+2500)&0xffffffff),1);self.assertEqual(c.du_text(),b'DSP 108%')
    def test_real_zero_differs_from_unavailable(self):
        c=self.c;c.du_init(0);self.send(0,50);c.du_tick(2500);self.assertEqual(c.du_text(),b'DSP 0%')
    def test_wire_extension_preserves_all_existing_diagnostic_fields(self):
        for cycles in (None,0,100000,278494,1044480,0xffffffff):
            p=report(cycles);rx,service=struct.unpack_from('<II',p,16)
            self.assertEqual((rx&1023,(rx>>10)&1023,(rx>>20)&1023),(17,3,40))
            self.assertEqual(((service>>7)&7,(service>>10)&255,service>>18),(2,13,9))
    def test_header_only_redraw_and_no_effect_enable_changes(self):
        c=self.c;c.du_ui_init();c.du_ui_draw(0)
        before=list(c.du_ui_pixels()[:160*80]);enabled=[c.du_ui_enabled(i) for i in range(6)]
        c.du_ui_reset_blits();p=report(234000);c.du_ui_report(p,len(p),100);c.du_ui_draw(2499)
        self.assertEqual(c.du_ui_blits(),0);c.du_ui_draw(2500)
        after=list(c.du_ui_pixels()[:160*80]);self.assertEqual(before[16*160:],after[16*160:])
        self.assertNotEqual(before[:16*160],after[:16*160]);self.assertEqual(c.du_ui_blits(),2)
        self.assertEqual(c.du_ui_max_y(),16);self.assertFalse(c.du_ui_bad_blit())
        self.assertEqual(enabled,[c.du_ui_enabled(i) for i in range(6)])
        c.du_ui_foot(2,3000);self.assertEqual(c.du_ui_enabled(2),1-enabled[2])
        self.assertEqual([c.du_ui_enabled(i) for i in (0,1,3,4,5)],[enabled[i] for i in (0,1,3,4,5)])
    def test_callback_patch_is_opt_in_and_preserves_receive_copy(self):
        from platform_compat import patch_dsp_link
        source='#include "dsp_link.h"\n    gp5_uart0_send(tx, len, 100);           /* 404 bytes take 9 ms at 460800 baud */\n    for (i = 0; i < dsp_unsol_len; i++) dsp_unsol[i] = pl[i];'
        self.assertNotIn('fw_dsp_report',patch_dsp_link(source))
        result=patch_dsp_link(source,True);self.assertIn('fw_dsp_report(pl,n);',result)
        self.assertIn('dsp_unsol[i] = pl[i]',result)
    def test_invalid_stage_option_fails_before_filesystem_work(self):
        from prepare_gp5 import stage
        with self.assertRaisesRegex(ValueError,'dsp_usage requires'):
            stage(Path('missing'),Path('unused'),dsp_usage=True)

if __name__=='__main__':unittest.main()
