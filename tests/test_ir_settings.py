"""Home-view persistence through real controller/IR code; simulated IO only."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
class IrSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/ir_settings.dll' if os.name=='nt' else 'build/ir_settings.so'),ROOT/'tests/ir_settings_host.c',includes=[ROOT/'tests/stubs'])))
    def setUp(self):self.c.is_init()
    def pump_ms(self,ms):
        for _ in range(ms//20):self.c.ge_pump(1);self.c.ge_tick_draw()
    def settle(self):
        for _ in range(300):
            self.c.ge_pump(1);self.c.ge_tick_draw()
            if self.c.ge_ready():return
        self.fail('editor never settled')
    def two_presets(self):
        c=self.c;self.settle()
        self.assertEqual(c.ge_flash_save(3),1);self.settle()
        self.assertEqual(c.ge_flash_save(8),1);self.settle()
        self.assertEqual((c.ge_store_preset(3),c.ge_store_preset(8),c.ge_global(6)),(1,1,8))
    def test_landed_preset_survives_a_power_cycle_without_ir(self):
        """beta.11 (user): the preset you land on is the boot preset."""
        c=self.c;self.two_presets();c.is_ir_input(0,0)
        self.assertEqual(c.ge_flash_load(3),1);self.pump_ms(1000)
        self.assertEqual(c.ge_current_preset(),3);self.assertEqual(c.ge_global(6),8)   # held < 2 s: not yet
        self.pump_ms(1500);self.assertEqual(c.ge_global(6),3)
        ops=c.ge_flash_ops();self.pump_ms(3000);self.assertEqual(c.ge_flash_ops(),ops)  # one write
        c.ge_reboot();self.pump_ms(2000);self.assertEqual(c.ge_current_preset(),3)
    def test_browsing_presets_costs_one_write(self):
        c=self.c;self.two_presets();c.is_ir_input(0,0);ops=c.ge_flash_ops()
        for i in (3,8,3):
            self.assertEqual(c.ge_flash_load(i),1);self.pump_ms(600)
        self.assertEqual(c.ge_flash_ops(),ops)
        self.pump_ms(2500);self.assertEqual(c.ge_global(6),3)
        once=c.ge_flash_ops();self.assertGreater(once,ops)
        self.assertEqual(c.ge_flash_load(8),1);self.pump_ms(3000);self.assertEqual(c.ge_global(6),8)
        self.assertEqual(c.ge_flash_load(3),1);self.settle();self.assertEqual(c.ge_flash_load(8),1);self.pump_ms(3000)
        self.assertEqual(c.ge_global(6),8)                                              # back where it was: no write
    def test_save_waits_while_the_ir_input_is_playing(self):
        """A flash write pauses the IR link, so it waits for a quiet input."""
        c=self.c;self.two_presets();c.is_ir_input(1,200000)                          # about -32 dBFS
        self.assertEqual(c.ge_flash_load(3),1);self.pump_ms(6000)
        self.assertEqual(c.ge_global(6),8)
        c.is_ir_input(1,20000);self.pump_ms(1000);self.assertEqual(c.ge_global(6),8)   # quiet, < 1.5 s
        self.pump_ms(800);self.assertEqual(c.ge_global(6),3)
        self.assertEqual([c.is_state(i) for i in (0,1)],[0,0])                         # IR link resumed
    def test_midi_program_change_is_remembered_too(self):
        c=self.c;self.two_presets();c.is_ir_input(0,0)
        c.ge_midi(0x0c,0xc0,3,0);self.pump_ms(3000)
        self.assertEqual((c.ge_current_preset(),c.ge_global(6)),(3,3))
    def test_chain_preset_stomp_changes_persist_with_master_resumed(self):
        c=self.c
        for home in (1,0,2,1,0):
            calls=c.is_state(3);writes=c.is_state(2)
            c.is_home(home)
            self.assertEqual(c.ge_global(5),home)
            self.assertEqual([c.is_state(i) for i in (0,1,4)],[0,0,0])
            self.assertGreater(c.is_state(2),writes)
            self.assertEqual(c.is_state(3)-calls,2*(c.is_state(2)-writes))
            c.ge_reboot();self.assertEqual(c.ge_global(5),home)
    def test_transient_pause_misses_are_retried(self):
        """32q: a flash write asks the IR-link pause again (4 tries) before it is
        refused, so a busy link no longer fails a save; 4 misses still refuse."""
        c=self.c;c.is_home(0);c.is_miss_pause(3);c.is_home(1)
        self.assertEqual(c.ge_global(5),1);self.assertEqual(c.is_state(4),0)
        c.ge_reboot();self.assertEqual(c.ge_global(5),1)
        writes=c.ge_flash_ops();c.is_miss_pause(4);c.is_home(2)
        self.assertEqual(c.ge_flash_ops(),writes);self.assertEqual(c.is_state(4),1)
        c.ge_reboot();self.assertEqual(c.ge_global(5),1)
    def test_failed_pause_keeps_flash_unchanged_and_settings_dirty(self):
        c=self.c;c.is_home(0);writes=c.ge_flash_ops();c.is_fail_pause(1);c.is_home(1)
        self.assertEqual(c.ge_flash_ops(),writes)
        self.assertEqual(c.is_state(4),1)
        self.assertEqual(c.is_state(1),0)
        c.ge_reboot();self.assertEqual(c.ge_global(5),0)
