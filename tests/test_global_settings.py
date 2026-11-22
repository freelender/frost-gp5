"""Stage 2b global settings and home screens (docs/User%20Guide.md) through
the real controller and DSP code on a simulated controller flash: long press,
the settings menu, persistence, DSP sync (input gain, cabinet override, output
mode), Preset view, Stomp view, reset / factory reset, boot preset."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
CHAIN,PARAMS,SETTINGS,SVALUE,SCONFIRM,PHOME,STOMP=0,1,9,10,11,12,13
IN_GAIN,OUT_LEVEL,OUT_MODE,CAB,BRIGHT,HOME,SETLIST_HOLD,FIRMWARE,RESET,FACTORY,EQ_BASS,EQ_MIDDLE,EQ_TREBLE,BACK=range(14)
class GlobalSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/global_settings.dll' if os.name=='nt' else 'build/global_settings.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_actual_value.restype=ct.c_float;c.ge_preset_label.restype=ct.c_char_p
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=300):
        for _ in range(n):
            self.lib.ge_pump(1);self.lib.ge_tick_draw()
            if self.lib.ge_ready():return
        self.fail('controller did not settle')
    def blocks(self,n=8):self.lib.ge_audio(n)
    def open_settings(self):
        c=self.lib;c.ge_long_press();self.assertEqual(c.ge_view(),SETTINGS);self.assertEqual(c.ge_paint(),0)
    def go_row(self,row):
        c=self.lib;c.ge_turn(row-c.ge_settings_row());self.assertEqual(c.ge_settings_row(),row)
    def set_value(self,row,turns):
        c=self.lib;self.go_row(row);c.ge_press();self.assertEqual(c.ge_view(),SVALUE);c.ge_turn(turns);self.assertEqual(c.ge_paint(),0);c.ge_press();self.assertEqual(c.ge_view(),SETTINGS)
    def leave(self):
        c=self.lib;self.go_row(BACK);c.ge_press();self.settle()
    def test_long_press_opens_settings_short_press_still_edits(self):
        c=self.lib;c.ge_press();self.assertEqual(c.ge_view(),PARAMS)
        c.ge_turn(c.ge_human_count(c.ge_type(c.ge_selected()))+3);c.ge_press();self.assertEqual(c.ge_view(),CHAIN)    # BACK row returns home
        self.open_settings();self.leave();self.assertEqual(c.ge_view(),CHAIN)
    def test_settings_edit_clamp_persist_and_reach_the_dsp(self):
        c=self.lib;self.open_settings()
        self.set_value(IN_GAIN,-6);self.set_value(OUT_LEVEL,-25);self.set_value(OUT_MODE,1);self.set_value(CAB,1)
        self.set_value(BRIGHT,-3);self.assertEqual(c.ge_backlight(),70)
        self.set_value(IN_GAIN,-40);self.assertEqual(c.ge_global(0),-20)    # clamps at -20 dB
        self.set_value(IN_GAIN,14);self.set_value(BRIGHT,-20);self.assertEqual(c.ge_global(4),0);self.assertEqual(c.ge_backlight(),1)  # 0 % keeps a faint light
        self.leave();self.blocks(8);self.settle();self.blocks(8)
        self.assertEqual([c.ge_global(i) for i in range(6)],[-6,75,1,1,0,0])
        self.assertEqual(c.ge_dsp_global(3),-60);self.assertAlmostEqual(c.ge_dsp_global(0),501,delta=1);self.assertEqual(c.ge_dsp_global(1),1);self.assertEqual(c.ge_dsp_global(2),1)
        c.ge_reboot();self.settle();self.blocks(8);self.settle();self.blocks(8)
        self.assertEqual([c.ge_global(i) for i in range(6)],[-6,75,1,1,0,0]);self.assertEqual(c.ge_backlight(),1)
        self.assertEqual(c.ge_dsp_global(3),-60);self.assertEqual(c.ge_dsp_global(1),1)
    def test_cab_override_bypasses_ir_and_returns(self):
        c=self.lib;self.assertEqual(c.ge_actual_type(3),3)                   # default chain holds an IR
        self.open_settings();self.set_value(CAB,1);self.leave();self.blocks(8);self.settle();self.blocks(8)
        self.assertEqual(c.ge_dsp_global(1),1)
        self.open_settings();self.set_value(CAB,1);self.leave();self.blocks(8);self.settle();self.blocks(8)
        self.assertEqual(c.ge_dsp_global(1),0)
    def test_preset_view_para_and_foot_step_through_stored_presets(self):
        c=self.lib
        vals={}
        for i,v in ((2,1200),(5,1500),(9,1900)):
            c.ge_queue_raw(1,0,v);self.settle();vals[i]=c.ge_actual_value(1,0);self.assertEqual(c.ge_flash_save(i),1);self.settle()
        self.assertEqual(len(set(vals.values())),3)
        self.open_settings();self.set_value(HOME,1);self.leave();self.assertEqual(c.ge_view(),PHOME);self.assertEqual(c.ge_paint(),0)
        self.assertEqual(c.ge_current_preset(),9)                               # the last saved
        c.ge_foot();self.settle();self.assertEqual(c.ge_current_preset(),2)     # wraps 10 -> 3
        self.assertEqual(c.ge_actual_value(1,0),vals[2])
        c.ge_turn(1);self.settle();self.assertEqual(c.ge_current_preset(),5);self.assertEqual(c.ge_actual_value(1,0),vals[5])
        c.ge_turn(-1);self.settle();self.assertEqual(c.ge_current_preset(),2);self.assertEqual(c.ge_paint(),0)
        c.ge_press();self.assertEqual(c.ge_view(),CHAIN)                        # short press: edit the sound
        c.ge_advance(16000);c.ge_tick_draw();self.assertEqual(c.ge_view(),PHOME)  # idle: back home
        self.open_settings();self.assertEqual(c.ge_view(),SETTINGS)
    def test_stomp_view(self):
        c=self.lib;self.open_settings();self.set_value(HOME,2);self.leave();self.assertEqual(c.ge_view(),STOMP);self.assertEqual(c.ge_paint(),0)
        first=c.ge_selected();c.ge_turn(1);self.assertNotEqual(c.ge_selected(),first);self.assertEqual(c.ge_paint(),0)
        sel=c.ge_selected();before=c.ge_actual_enabled(sel);c.ge_foot();self.settle();self.assertEqual(c.ge_actual_enabled(sel),1-before)
        c.ge_press();self.assertEqual(c.ge_view(),PARAMS)
    def test_reset_settings_and_factory_reset_asks_twice(self):
        c=self.lib;c.ge_flash_save(0);self.settle();self.assertEqual(c.ge_store_preset(0),1)
        self.open_settings();self.set_value(IN_GAIN,5);self.set_value(HOME,2)
        self.go_row(RESET);c.ge_press();self.assertEqual(c.ge_view(),SCONFIRM);c.ge_press();self.assertEqual(c.ge_view(),SETTINGS)   # CANCEL default
        self.assertEqual(c.ge_global(0),5)
        c.ge_press();c.ge_turn(1);c.ge_press();self.assertEqual([c.ge_global(i) for i in range(6)],[0,100,0,0,100,0]);self.assertEqual(c.ge_store_preset(0),1)
        self.go_row(FACTORY);c.ge_press();c.ge_turn(1);c.ge_press();self.assertEqual(c.ge_view(),SCONFIRM)   # first YES: asks again
        self.assertEqual(c.ge_paint(),0);self.assertEqual(c.ge_store_preset(0),1)
        c.ge_press();self.assertEqual(c.ge_view(),SETTINGS);self.assertEqual(c.ge_store_preset(0),1)          # second ask: CANCEL default
        c.ge_press();c.ge_turn(1);c.ge_press();c.ge_turn(1);c.ge_press();self.assertEqual(c.ge_store_preset(0),0)
        self.go_row(FIRMWARE);c.ge_press();self.assertEqual(c.ge_view(),SETTINGS)
    def test_last_preset_is_loaded_at_boot(self):
        c=self.lib;c.ge_queue_raw(1,0,1600);self.settle();want=c.ge_actual_value(1,0);c.ge_flash_save(7);self.settle()
        c.ge_queue_raw(1,0,1000);self.settle();c.ge_advance(11000);self.settle()   # debounced save of the current preset
        self.assertEqual(c.ge_global(6),7)
        c.ge_reboot();self.settle();self.blocks(8);self.settle()
        self.assertEqual(c.ge_current_preset(),7);self.assertEqual(c.ge_actual_value(1,0),want)
    def test_release_version_is_complete_and_fits_settings_row(self):
        c=self.lib;c.ge_firmware_text.restype=ct.c_char_p
        label=c.ge_firmware_text().decode()
        self.assertEqual(label,'0.1.0-BETA.7')
        self.assertGreaterEqual(153-6*len(label),15+6*len('FIRMWARE')+6)
        self.open_settings();self.go_row(FIRMWARE);self.assertEqual(c.ge_paint(),0)
    def test_output_eq_clamps_persists_and_reconciles_to_dsp(self):
        c=self.lib;self.open_settings()
        self.set_value(EQ_BASS,-50);self.set_value(EQ_MIDDLE,50);self.set_value(EQ_TREBLE,6)
        self.leave();self.blocks();self.settle();self.blocks()
        self.assertEqual([c.ge_global_eq(i) for i in range(3)],[-12,12,6])
        self.assertEqual([c.ge_dsp_eq(i) for i in range(3)],[-12,12,6])
        c.ge_reboot();self.settle();self.blocks();self.settle()
        self.assertEqual([c.ge_global_eq(i) for i in range(3)],[-12,12,6])
        self.assertEqual([c.ge_dsp_eq(i) for i in range(3)],[-12,12,6])

if __name__=='__main__':unittest.main()
