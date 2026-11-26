"""Preset CTL assignments through real PARA/foot gestures and simulated flash."""
import ctypes as ct
import os
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import settings_nav as N

class CtlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/ctl.dll' if os.name=='nt' else 'build/ctl.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
    def setUp(self):
        self.lib.ge_init();self.settle();self.lib.ge_paint();self.settle()
    def settle(self):
        c=self.lib;stable=0
        for _ in range(400):
            c.ge_pump(1)
            stable=stable+1 if c.ge_ready() else 0
            if stable>=6:return
        self.fail('controller did not settle')
    def select(self,slot):
        c=self.lib
        for _ in range(6):
            if c.ge_selected()==slot:return
            c.ge_turn(1)
        self.fail('slot not found')
    def assign(self,slot,on=1):
        c=self.lib;self.select(slot);c.ge_press()
        c.ge_turn(c.ge_row_total()-4);self.assertEqual(c.ge_row_kind(c.ge_parameter()),7)
        c.ge_press();c.ge_turn(on-((c.ge_ctl_mask()>>slot)&1));self.assertEqual(c.ge_paint(),0)
        c.ge_press();c.ge_foot();self.settle()
    def mode(self,on=1,tap=False,home=None):
        c=self.lib;c.ge_long_press()
        if tap:N.set_value(c,N.TAP_TEMPO,1)
        if home is not None:N.set_value(c,N.HOME,home)
        N.set_value(c,N.FOOTSWITCH,on-c.ge_ctl_mode());N.leave(c);self.settle()
    def states(self):return [self.lib.ge_actual_enabled(i) for i in range(6)]
    def test_default_selected_and_empty_ctl_group(self):
        c=self.lib;self.assertEqual(c.ge_ctl_mask(),0);self.assertEqual(c.ge_ctl_mode(),0)
        before=self.states();c.ge_foot();self.settle();self.assertEqual(self.states(),[v^(i==1) for i,v in enumerate(before)])
        self.mode();before=self.states();c.ge_foot();self.settle();self.assertEqual(self.states(),before)
    def test_mixed_states_toggle_individually_and_unassigned_stay(self):
        c=self.lib;self.assign(0);self.assign(1)
        self.select(1);c.ge_foot();self.settle();before=self.states()
        self.assertEqual(before[:2],[1,0]);self.select(3);self.mode()
        c.ge_foot();self.settle();self.assertEqual(self.states(),[v^(i in (0,1)) for i,v in enumerate(before)])
        c.ge_foot();self.settle();self.assertEqual(self.states(),before)
        self.assertEqual(c.ge_ctl_mask(),3)
    def test_hold_tuner_does_not_toggle_ctl_and_bounce_toggles_once(self):
        c=self.lib;self.assign(0);self.assign(2);self.mode();before=self.states()
        c.ge_foot_hold();self.settle();self.assertEqual(c.ge_view(),14);self.assertEqual(self.states(),before)
        c.ge_foot();self.settle();self.assertEqual(c.ge_view(),0)
        for down,ms in [(1,60),(0,3),(1,3),(0,3),(1,3),(0,80)]:c.ge_foot_edge(down,ms)
        c.ge_tick_draw();self.settle();self.assertEqual(self.states(),[v^(i in (0,2)) for i,v in enumerate(before)])
    def test_assignments_saved_per_patch_and_recalled_after_reboot(self):
        c=self.lib;self.assign(0);self.assign(2);self.mode()
        self.assertEqual(c.ge_flash_save(0),1);self.settle()
        self.assign(0,0);self.assign(1)
        self.assertEqual(c.ge_flash_save(1),1);self.settle();self.assertEqual(c.ge_ctl_mask(),6)
        self.assertEqual(c.ge_flash_load(0),1);self.settle();self.assertEqual(c.ge_ctl_mask(),5)
        self.assertEqual(c.ge_flash_load(1),1);self.settle();self.assertEqual(c.ge_ctl_mask(),6)
        c.ge_reboot();self.settle();c.ge_paint();self.settle()
        self.assertEqual(c.ge_ctl_mode(),1);self.assertEqual(c.ge_ctl_mask(),6)
    def test_ctl_overrides_tap_tempo_in_stomp(self):
        c=self.lib;self.assign(0);self.mode(tap=True,home=2)
        self.assertEqual(c.ge_view(),13);before=self.states();c.ge_foot();self.settle()
        self.assertEqual(self.states(),[v^(i==0) for i,v in enumerate(before)])
        self.assertEqual(c.ge_tap_mode(),0)
    def test_legacy_preset_defaults_to_disabled_and_replacement_clears_ctl(self):
        c=self.lib;self.assertEqual(c.ge_flash_save(0),1);self.settle()
        self.assign(1);self.assertEqual(c.ge_flash_load(0),1);self.settle();self.assertEqual(c.ge_ctl_mask(),0)
        self.assign(1);c.ge_press();c.ge_turn(c.ge_human_count(c.ge_type(1))+2)
        c.ge_press();c.ge_press();self.settle();self.assertEqual(c.ge_type(1),255);self.assertEqual(c.ge_ctl_mask(),0)
    def test_position_keeps_assignment_and_lost_reply_does_not_double_toggle(self):
        c=self.lib;self.assign(0);c.ge_press();c.ge_turn(c.ge_human_count(c.ge_type(0))+1)
        c.ge_press();c.ge_turn(2);c.ge_press();self.settle();c.ge_foot()
        self.assertEqual(c.ge_order(2),0);self.assertEqual(c.ge_ctl_mask(),1)
        self.mode();before=self.states();c.ge_drop(1,1);c.ge_foot();self.settle()
        self.assertEqual(self.states(),[v^(i==0) for i,v in enumerate(before)])
    def test_edit_cancel_preserves_assignment_and_menu_foot_is_back(self):
        c=self.lib;self.assign(0);before=self.states();self.select(0);c.ge_press();c.ge_turn(c.ge_row_total()-4)
        c.ge_press();c.ge_turn(1);c.ge_foot();self.assertEqual(c.ge_view(),1)
        self.assertEqual(c.ge_ctl_mask(),1);c.ge_foot();self.settle();self.assertEqual(self.states(),before)

if __name__=='__main__':unittest.main()
