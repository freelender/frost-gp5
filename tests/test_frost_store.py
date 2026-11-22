"""tools/frost_store.py (Python twin of web/store.js) against the real controller
C code on a simulated flash: migration, AMP put/remove, preset rename/delete,
backup/restore, and the storage version in the 7E 0D reply."""
import ctypes as ct,os,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import fcm_bank as B
import fcm_files as F
import frost_store as S
from test_fcm import model
class FrostStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/frost_store_editor.dll' if os.name=='nt' else 'build/frost_store_editor.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)]
        c.ge_fcm_label.restype=ct.c_char_p;c.ge_fcm_label.argtypes=[ct.c_int];c.ge_preset_label.restype=ct.c_char_p
    def setUp(self):
        self.lib.ge_init()
        for _ in range(300):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():break
        self.pedal=F.Pedal(self.host);self.st=S.Store(self.pedal)
    def host(self,q):
        q=bytes(q);a=(ct.c_uint8*len(q))(*q);o=(ct.c_uint8*100)();n=self.lib.ge_host(a,len(q),o);return bytes(o[:n]) if n else None
    def settle(self):
        for _ in range(300):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
    def test_storage_version_migration_and_models(self):
        c=self.lib;self.assertEqual(self.pedal.storage(),2)
        self.pedal.add(2,B.pack_record('OLD',model()),say=lambda *_:None)
        self.assertEqual(self.st.migrate(),1);self.assertEqual(c.ge_fcm_label(2),b'02 OLD')
        self.st.amp_put(63,B.pack_record('LAST',model(gain=2.0)));self.assertEqual(self.st.amps(),{2:'OLD',63:'LAST'})
        self.assertEqual(c.ge_fcm_label(63),b'63 LAST');self.st.amp_remove(2);self.assertEqual(c.ge_fcm_label(2),b'02 DEFAULT')
    def test_presets_rename_delete_backup_restore(self):
        c=self.lib
        for i in (0,59):
            c.ge_advance(3000);self.assertEqual(c.ge_flash_save(i),1);self.settle()
        self.assertEqual(self.st.presets(),{0:'PRESET 01',59:'PRESET 60'})
        self.st.preset_rename(59,'Last One');self.assertEqual(c.ge_preset_label(59),b'60 LAST ONE')
        bak=self.st.backup();self.st.preset_delete(0);self.assertEqual(c.ge_store_preset(0),0)
        self.st.restore(bak);self.assertEqual(self.st.presets(),{0:'PRESET 01',59:'Last One'});self.assertEqual(c.ge_store_preset(0),1)
if __name__=='__main__':unittest.main()
