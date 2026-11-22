"""Empty chain at boot (2026-11-18, user request): with FROST_EMPTY_BOOT_CHAIN
and no saved preset to recall, all six slots are empty after start-up; a
saved preset still loads. Host check of the controller + DSP fixture."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from host import build
EMPTY=255
class EmptyBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ct.CDLL(str(build(ROOT/('build/empty_boot'+('.dll' if os.name=='nt' else '.so')),ROOT/'tests/slot_editor_host.c',
            includes=[ROOT/'tests/stubs'],flags=['-DFROST_EMPTY_BOOT_CHAIN=1'])))
    def boot(self,n=200):
        c=self.c
        for _ in range(n):c.ge_paint();c.ge_pump(1)
    def test_no_saved_preset_boots_empty(self):
        c=self.c;c.ge_init();self.boot()
        self.assertEqual([c.ge_actual_type(i) for i in range(6)],[EMPTY]*6)
    def test_saved_preset_still_loads_at_boot(self):
        c=self.c;c.ge_init();self.boot()
        self.assertEqual(c.ge_type_change(0,13),1);self.boot()
        self.assertEqual(c.ge_actual_type(0),13)
        self.assertEqual(c.ge_flash_save(0),1);self.boot()
        c.ge_advance(3000);self.boot()                        # debounced last-preset save
        c.ge_reboot();self.boot()
        self.assertEqual(c.ge_actual_type(0),13)
        self.assertEqual([c.ge_actual_type(i) for i in range(1,6)].count(EMPTY),5)
if __name__=='__main__':unittest.main()
