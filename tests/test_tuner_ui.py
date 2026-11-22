"""Stage 2c tuner through the real controller and DSP code: footswitch hold on
each home screen opens it (and never leaves an effect toggled), the DSP mutes
and measures, PARA sets the reference, a footswitch press exits and the chain
fades back in, the reference persists. The non-ADC host build feeds the DSP a
synthetic 44100/256 Hz sawtooth (172.27 Hz: F3 -23 cents at A440)."""
import ctypes as ct,math,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
CHAIN,PARAMS,PHOME,STOMP,TUNER=0,1,12,13,14
F=44100/256
class TunerUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ct.CDLL(str(build(ROOT/('build/tuner_ui.dll' if os.name=='nt' else 'build/tuner_ui.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=300):
        for _ in range(n):
            self.lib.ge_pump(1);self.lib.ge_tick_draw()
            if self.lib.ge_ready():return
        self.fail('controller did not settle')
    def run_for(self,n):
        for _ in range(n):self.lib.ge_pump(1);self.lib.ge_tick_draw()
    def test_hold_opens_tuner_measures_and_exit_restores(self):
        c=self.lib;sel=c.ge_selected();before=[c.ge_actual_enabled(i) for i in range(6)]
        c.ge_foot_hold();self.assertEqual(c.ge_view(),TUNER);self.assertEqual(c.ge_paint(),0)
        self.run_for(60)
        self.assertEqual([c.ge_actual_enabled(i) for i in range(6)],before)          # the press was undone
        self.assertEqual(c.ge_tuner(3),1);self.assertEqual(c.ge_tuner(6),1)            # DSP tuning (chain skipped, output muted)
        self.run_for(80)
        self.assertEqual(c.ge_tuner(0),1);self.assertAlmostEqual(c.ge_tuner(5)/100,F,delta=0.15)   # within 1.5 cents
        want=12*math.log2(F/440)+69;self.assertEqual(c.ge_tuner(1),round(want))
        self.assertAlmostEqual(c.ge_tuner(2)/100,100*(want-round(want)),delta=1.0);self.assertEqual(c.ge_paint(),0)
        c.ge_turn(5);self.assertEqual(c.ge_tuner(4),440) # locked on entry
        c.ge_press();c.ge_turn(5);self.assertEqual(c.ge_tuner(4),445);c.ge_turn(20);self.assertEqual(c.ge_tuner(4),450)
        c.ge_press();c.ge_turn(-20);self.assertEqual(c.ge_tuner(4),450) # press locks it again
        c.ge_foot();self.assertEqual(c.ge_view(),CHAIN);self.run_for(20)
        self.assertEqual(c.ge_tuner(3),0);self.assertEqual(c.ge_tuner(6),0)            # chain back (fades in)
        self.assertEqual(c.ge_selected(),sel)
        c.ge_reboot();self.settle();self.assertEqual(c.ge_tuner(4),450)                # reference remembered
    def test_reference_relocks_on_reentry_and_clamps_at_both_limits(self):
        c=self.lib;c.ge_foot_hold();c.ge_press();c.ge_turn(-30)
        self.assertEqual(c.ge_tuner(4),430);c.ge_foot();self.run_for(20)
        c.ge_foot_hold();c.ge_turn(20);self.assertEqual(c.ge_tuner(4),430)
        c.ge_press();c.ge_turn(30);self.assertEqual(c.ge_tuner(4),450)
        c.ge_press();c.ge_turn(-30);self.assertEqual(c.ge_tuner(4),450)
    def test_short_footswitch_still_toggles_on_press(self):
        c=self.lib;sel=c.ge_selected();before=c.ge_actual_enabled(sel)
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_enabled(sel),1-before);self.assertEqual(c.ge_view(),CHAIN)
    def test_preset_view_tap_is_next_preset_and_hold_is_tuner(self):
        c=self.lib
        for i in (1,3):self.assertEqual(c.ge_flash_save(i),1);self.settle()
        c.ge_long_press();c.ge_turn(5);c.ge_press();c.ge_turn(1);c.ge_press();c.ge_turn(c.ge_settings_back_row()-5);c.ge_press();self.settle()
        self.assertEqual(c.ge_view(),PHOME);cur=c.ge_current_preset()
        c.ge_foot();self.settle();self.assertNotEqual(c.ge_current_preset(),cur);cur=c.ge_current_preset()
        c.ge_foot_hold();self.assertEqual(c.ge_view(),TUNER);self.assertEqual(c.ge_current_preset(),cur)   # hold: no preset change
        c.ge_foot();self.assertEqual(c.ge_view(),PHOME)
    def test_stomp_view_hold(self):
        c=self.lib;c.ge_long_press();c.ge_turn(5);c.ge_press();c.ge_turn(2);c.ge_press();c.ge_turn(c.ge_settings_back_row()-5);c.ge_press();self.settle()
        self.assertEqual(c.ge_view(),STOMP);c.ge_foot_hold();self.assertEqual(c.ge_view(),TUNER);c.ge_foot();self.assertEqual(c.ge_view(),STOMP)
if __name__=='__main__':unittest.main()
