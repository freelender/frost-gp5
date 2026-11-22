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
    def test_failed_pause_keeps_flash_unchanged_and_settings_dirty(self):
        c=self.c;c.is_home(0);writes=c.ge_flash_ops();c.is_fail_pause(1);c.is_home(1)
        self.assertEqual(c.ge_flash_ops(),writes)
        self.assertEqual(c.is_state(4),1)
        self.assertEqual(c.is_state(1),0)
        c.ge_reboot();self.assertEqual(c.ge_global(5),0)
