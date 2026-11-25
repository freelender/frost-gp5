"""32t footswitch tap tempo through the real controller (slot_editor.h) and
DSP fixture. With TAP TEMPO ON, on a tempo block a short press is a tap (sets
the delay time or modulation rate, shows the TAP TEMPO overlay) and a
press-and-hold switches the block on/off (32v); the footswitch LED flashes
yellow on the beat. Host only."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import settings_nav
DIGI,TREM,GREEN=17,27,9
CHAIN,TUNER=0,14
YELLOW=0x01000000|32|(26<<8)
BLUE=0x01000000|5|(20<<8)|(32<<16)
class TapTempoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/tap_tempo.dll' if os.name=='nt' else 'build/tap_tempo.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
    def setUp(self):self.c.ge_init();self.settle();self.c.ge_tap_tempo(1)   # 32u: OFF by default
    def settle(self,n=150):
        for _ in range(n):self.c.ge_pump(1);self.c.ge_tick_draw()
    def put(self,slot,type):
        self.assertEqual(self.c.ge_type_change(slot,type),1);self.settle();self.c.ge_select(slot);self.c.ge_tick_draw()
    def press(self,gap=500,hold=40):
        """A tap exactly `gap` ms after the previous press (settling in between included)."""
        c=self.c;last=getattr(self,'last',None);adv=max(1,(last+gap-c.ge_now()) if last is not None else gap)
        c.ge_foot_edge(1,adv);self.last=c.ge_now();c.ge_tick_draw();c.ge_foot_edge(0,hold);c.ge_tick_draw()
    def hold(self):
        c=self.c;c.ge_foot_edge(1,300);c.ge_advance(800);c.ge_tick_draw();c.ge_foot_edge(0,40);c.ge_tick_draw();self.last=None
    def test_taps_set_the_delay_time_and_hold_switches_the_block(self):
        c=self.c;self.put(4,DIGI);on=c.ge_enabled(4)
        self.press(500);self.settle(5)
        self.assertEqual(c.ge_tap_mode(),1);self.assertEqual(c.ge_view(),CHAIN)       # first tap: overlay, no tuner
        self.assertEqual(c.ge_enabled(4),on)                                            # a tap does not switch it
        for _ in range(4):self.press(500)
        self.settle(20);self.assertEqual(c.ge_value(4,2),500)                           # TIME = one quarter at 120 BPM
        self.assertEqual(c.ge_enabled(4),on)                                            # taps do not switch it
        c.ge_tap_note(1);self.press(500);self.settle(20);self.assertEqual(c.ge_value(4,2),375)   # dotted eighth
        c.ge_advance(2600);c.ge_tick_draw();self.assertEqual(c.ge_tap_mode(),0)       # overlay times out
        self.hold();self.settle(20)
        self.assertEqual(c.ge_enabled(4),1-on);self.assertEqual(c.ge_view(),CHAIN)    # hold = on/off, never the tuner
        self.assertEqual(c.ge_tap_mode(),0);self.assertEqual(c.ge_value(4,2),375)      # the release after a hold is no tap
        self.hold();self.settle(20);self.assertEqual(c.ge_enabled(4),on)
    def test_outlier_restarts_and_slow_taps_reset(self):
        c=self.c;self.put(4,DIGI)
        for gap in (400,400,400,400):self.press(gap)
        self.settle(20);self.assertEqual(c.ge_value(4,2),400)
        self.press(700);self.settle(20);self.assertEqual(c.ge_value(4,2),700)           # outlier: the new tempo wins
        self.press(2100);self.press(300);self.press(300);self.settle(20);self.assertEqual(c.ge_value(4,2),300)   # a >2 s gap starts over
    def test_modulation_rate_follows_and_led_flashes_yellow_on_the_beat(self):
        c=self.c;self.put(4,TREM)
        if not c.ge_enabled(4):self.hold();self.settle(20)
        self.assertEqual(c.ge_enabled(4),1)
        self.press(500);self.assertEqual(c.ge_led(),BLUE)                               # overlay up, no tapped tempo yet: blue
        for _ in range(3):self.press(500)
        self.settle(5);self.assertEqual(c.ge_value(4,0),200)                            # 2.00 Hz (scale 100) = one cycle per beat
        seen=set()
        for _ in range(60):c.ge_advance(10);seen.add(c.ge_led())
        self.assertEqual(seen,{BLUE,YELLOW})
        c.ge_advance(3000);c.ge_tick_draw();self.assertEqual(c.ge_tap_mode(),0)
        seen=set()
        for _ in range(60):c.ge_advance(10);seen.add(c.ge_led())
        self.assertEqual(seen,{1,YELLOW})                                               # on: mint with yellow beats
        c.ge_tempo_light(0);seen=set()
        for _ in range(60):c.ge_advance(10);seen.add(c.ge_led())
        self.assertEqual(seen,{1})                                                      # TEMPO LIGHT OFF
    def test_global_target_sets_every_tempo_block(self):
        c=self.c;self.put(2,TREM);self.put(4,DIGI);before=c.ge_value(2,0)
        for _ in range(4):self.press(500)
        self.settle(20);self.assertEqual(c.ge_value(4,2),500);self.assertEqual(c.ge_value(2,0),before)   # SELECTED: only the delay
        c.ge_tap_target(1);c.ge_advance(2600);c.ge_tick_draw();self.last=None
        for _ in range(4):self.press(400)
        self.settle(20);self.assertEqual(c.ge_value(4,2),400);self.assertEqual(c.ge_value(2,0),250)       # GLOBAL: both, 2.50 Hz
        self.put(4,GREEN);c.ge_foot_edge(1,300);c.ge_advance(800);c.ge_tick_draw()
        self.assertEqual(c.ge_tap_mode(),0);self.assertEqual(c.ge_view(),TUNER)                         # non-tempo block selected: usual footswitch
    def test_non_tempo_block_hold_still_opens_the_tuner(self):
        c=self.c;self.put(4,GREEN);c.ge_foot_edge(1,300);c.ge_advance(800);c.ge_tick_draw()
        self.assertEqual(c.ge_tap_mode(),0);self.assertEqual(c.ge_view(),TUNER)
    def test_tap_tempo_off_by_default_keeps_the_usual_footswitch(self):
        c=self.c;c.ge_init();self.settle();self.assertEqual(c.ge_tap_tempo_on(),0)
        self.put(4,DIGI);on=c.ge_enabled(4)
        seen=set()
        for _ in range(60):c.ge_advance(10);seen.add(c.ge_led())
        self.assertLessEqual(seen,{0,1})                                                # no yellow, no blue
        c.ge_foot_edge(1,300);c.ge_advance(800);c.ge_tick_draw()
        self.assertEqual(c.ge_tap_mode(),0);self.assertEqual(c.ge_view(),TUNER)        # hold = tuner, as before
        c.ge_foot_edge(0,40);self.press(500);self.settle(20);self.assertEqual(c.ge_view(),CHAIN)
        self.press(500);self.settle(20);self.assertEqual(c.ge_enabled(4),1-on)         # a press switches it
    def test_settings_persist(self):
        c=self.c;c.ge_long_press();settings_nav.set_value(c,settings_nav.TAP_NOTE,1)
        settings_nav.set_value(c,settings_nav.TEMPO_LIGHT,1);settings_nav.set_value(c,settings_nav.TAP_TEMPO,1)
        settings_nav.set_value(c,settings_nav.TAP_TARGET,1);settings_nav.leave(c);self.settle()
        self.assertEqual(c.ge_tap_settings(),10);c.ge_reboot();self.settle();self.assertEqual(c.ge_tap_settings(),10)
        self.assertEqual(c.ge_tap_target_on(),1)
        self.assertEqual(c.ge_tap_tempo_on(),0)   # setUp turned it on in RAM only; the saved TAP TEMPO toggle flipped it
if __name__=='__main__':unittest.main()
