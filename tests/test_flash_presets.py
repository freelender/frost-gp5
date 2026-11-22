"""Stage 2 stored presets (docs/Build%20Firmware.md) through the real controller and
DSP code on a simulated controller flash: the PRESETS menu (60 entries), SAVE
to flash (compact capture, copy-on-write group file), LOAD (compact recall),
power-cycle persistence, record format, guard after USB writes, packed AMP
model files and legacy fallback."""
import ctypes as ct,os,struct,sys,unittest,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from host import build
import fcm_bank as B
import fcm_files as F
from test_fcm import model
PRESETS,CONFIRM,CHAIN,PARAMS=3,4,0,1
class FlashPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        c=cls.lib=ct.CDLL(str(build(ROOT/('build/flash_presets.dll' if os.name=='nt' else 'build/flash_presets.so'),ROOT/'tests/slot_editor_host.c',includes=[ROOT/'tests/stubs'])))
        c.ge_host.argtypes=[ct.POINTER(ct.c_uint8),ct.c_uint,ct.POINTER(ct.c_uint8)];c.ge_preset_label.restype=ct.c_char_p
        c.ge_actual_value.restype=ct.c_float;c.ge_preset_record.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8)]
        for f in ('ge_dsp_rx','ge_fcm_ws'):getattr(c,f).restype=ct.c_uint
        c.ge_fcm_label.restype=ct.c_char_p;c.ge_fcm_label.argtypes=[ct.c_int]
    def setUp(self):self.lib.ge_init();self.settle()
    def settle(self,n=300):
        for _ in range(n):
            self.lib.ge_pump(1)
            if self.lib.ge_ready():return
        self.fail('controller did not settle')
    def host(self,q):
        a=(ct.c_uint8*len(q))(*q);o=(ct.c_uint8*100)();n=self.lib.ge_host(a,len(q),o);return bytes(o[:n]) if n else None
    def record(self,i):
        b=(ct.c_uint8*264)();n=self.lib.ge_preset_record(i,b);return bytes(b) if n else None
    def open_presets(self):
        c=self.lib;c.ge_press();c.ge_turn(-1);c.ge_press();self.assertEqual(c.ge_view(),PRESETS)
    def chain(self):
        c=self.lib;return ([c.ge_actual_type(i) for i in range(6)],[c.ge_order(i) for i in range(6)],[c.ge_actual_enabled(i) for i in range(6)],
                           [round(c.ge_actual_value(i,p),6) for i in range(6) for p in range(3)])
    def test_menu_lists_60_presets_wraps_and_paints(self):
        c=self.lib;self.open_presets();self.assertEqual(c.ge_paint(),0)
        self.assertEqual(c.ge_preset_label(0),b'01 ----');self.assertEqual(c.ge_preset_label(59),b'60 ----')
        c.ge_turn(60);self.assertEqual(c.ge_preset_choice(),60);self.assertEqual(c.ge_paint(),0)      # BACK
        c.ge_turn(1);self.assertEqual(c.ge_preset_choice(),0);c.ge_turn(-1);self.assertEqual(c.ge_preset_choice(),60)
        c.ge_turn(-3*61-5);self.assertEqual(c.ge_paint(),0);c.ge_turn(60-c.ge_preset_choice());self.assertEqual(c.ge_preset_choice(),60);c.ge_press();self.assertEqual(c.ge_view(),PARAMS)
    def test_save_load_and_power_cycle_through_the_menu(self):
        c=self.lib;before=c.ge_actual_revision()
        c.ge_queue_raw(1,0,1750);self.settle();saved=self.chain()
        self.open_presets();c.ge_turn(4);c.ge_press();self.assertEqual(c.ge_view(),CONFIRM);self.assertEqual(c.ge_paint(),0)
        c.ge_turn(2);c.ge_press();self.settle()                             # SAVE preset 05
        self.assertEqual(c.ge_capture_error(),0);self.assertEqual(c.ge_store_preset(4),1)
        self.assertEqual(c.ge_preset_label(4),b'05 PRESET 05');self.assertEqual(c.ge_actual_revision(),before+1)  # only the earlier edit
        r=self.record(4);self.assertEqual(r[:3],b'FP\x01');self.assertEqual(struct.unpack_from('<I',r,260)[0],zlib.crc32(r[:260]))
        c.ge_queue_raw(1,0,2250);self.settle();self.assertNotEqual(self.chain(),saved)
        c.ge_reboot();self.settle();self.assertNotEqual(self.chain(),saved)  # power cycle: defaults again
        self.assertEqual(c.ge_store_preset(4),1)                             # ...but the preset is still stored
        self.open_presets();c.ge_turn(4);c.ge_press();c.ge_turn(1);c.ge_press();self.settle()   # LOAD 05
        self.assertEqual(self.chain(),saved)
    def test_confirm_defaults_to_cancel_load_empty_does_nothing_and_foot_is_selected_only(self):
        c=self.lib;rev=c.ge_actual_revision();self.open_presets();c.ge_press();self.assertEqual(c.ge_view(),CONFIRM)
        c.ge_foot();self.settle();self.assertEqual(c.ge_actual_enabled(1),0);self.assertEqual(c.ge_actual_enabled(0),1)
        c.ge_press();self.assertEqual(c.ge_view(),PRESETS)                  # CANCEL is the default
        c.ge_press();c.ge_turn(1);c.ge_press();self.settle()                  # LOAD an empty preset: nothing happens
        self.assertEqual(c.ge_store_preset(0),0);self.assertEqual(c.ge_actual_revision(),rev+1)
    def test_overwrite_keeps_name_and_other_presets_and_survives_many_writes(self):
        c=self.lib
        for i in (0,1,14,15,59):self.assertEqual(c.ge_flash_save(i),1);self.settle()
        for k in range(40):                                                  # 40 rewrites of one group file
            c.ge_queue_raw(1,0,1000+10*k);self.settle();self.assertEqual(c.ge_flash_save(1),1);self.settle();self.assertEqual(c.ge_capture_error(),0)
        self.assertEqual([c.ge_store_preset(i) for i in (0,1,2,14,15,59)],[1,1,0,1,1,1])
        self.assertEqual(c.ge_preset_label(1),b'02 PRESET 02')
        files=F.Pedal(lambda q:self.host(bytes(q))).files();names=sorted(f['name'] for f in files)
        self.assertEqual(names,['glb-0','pst-0','pst-1','pst-3'])                    # one copy per group file, no leaks
    def test_pedal_save_waits_after_a_usb_write(self):
        c=self.lib;self.assertEqual(self.host([0x7e,0x07,*struct.pack('<I',0x15f000)])[2],1)
        self.assertEqual(c.ge_flash_save(0),1);self.settle();self.assertEqual(c.ge_capture_error(),1);self.assertEqual(c.ge_store_preset(0),0)
        c.ge_advance(2500);self.assertEqual(c.ge_flash_save(0),1);self.settle();self.assertEqual(c.ge_store_preset(0),1)
    def test_packed_amp_files_are_used_before_legacy_files(self):
        c=self.lib;old=B.pack_record('OLD',model(gain=1.2));new=B.pack_record('PACKED',model(gain=2.2))
        ped=F.Pedal(lambda q:self.host(bytes(q)));ped.add(3,old,say=lambda *_:None)
        c.ge_advance(3000);self.settle();self.assertEqual(c.ge_fcm_label(3),b'03 OLD')
        data=bytearray(4000);data[3*400:4*400]=new;h=F.header(99,F.FIRM,'amp-0',bytes(data))
        s=20;ped.erase(F.BASE+s*F.SECTOR);ped.program(F.BASE+s*F.SECTOR+64,bytes(data));ped.program(F.BASE+s*F.SECTOR,h[:60])
        self.assertEqual(c.ge_fcm_label(3),b'03 PACKED');self.assertEqual(c.ge_fcm_label(4),b'04 DEFAULT')
    def test_power_cut_at_every_flash_step_leaves_old_or_new_preset(self):
        c=self.lib;c.ge_queue_raw(1,0,1250);self.settle();self.assertEqual(c.ge_flash_save(0),1);self.settle()
        old=self.record(0);c.ge_queue_raw(1,0,2750);self.settle()
        c.ge_flash_cut(0,0);self.assertEqual(c.ge_flash_save(0),1);self.settle();ops=c.ge_flash_ops();new=self.record(0)
        self.assertNotEqual(old,new);self.assertGreaterEqual(ops,3)
        seen=set()
        for cut in range(1,ops+1):
            for part in (0,4,60,2048):
                c.ge_init();self.settle();c.ge_queue_raw(1,0,1250);self.settle();self.assertEqual(c.ge_flash_save(0),1);self.settle()
                self.assertEqual(self.record(0),old)
                c.ge_queue_raw(1,0,2750);self.settle();c.ge_flash_cut(cut,part);c.ge_flash_save(0);self.settle()
                c.ge_flash_cut(0,0);c.ge_reboot();self.settle()
                r=self.record(0);self.assertIn(r,(old,new),(cut,part));seen.add(r==new)
                self.assertEqual(c.ge_flash_save(1),1);self.settle();self.assertEqual(c.ge_store_preset(1),1)   # store still writable
        self.assertEqual(seen,{False,True})
    def test_a_cut_before_the_old_copy_is_erased_does_not_leak_a_sector(self):
        c=self.lib;self.assertEqual(c.ge_flash_save(0),1);self.settle()
        c.ge_flash_cut(0,0);c.ge_queue_raw(1,0,2750);self.settle();c.ge_flash_save(0);self.settle();ops=c.ge_flash_ops()
        c.ge_init();self.settle();self.assertEqual(c.ge_flash_save(0),1);self.settle()
        c.ge_queue_raw(1,0,2750);self.settle();c.ge_flash_cut(ops,0);c.ge_flash_save(0);self.settle();c.ge_flash_cut(0,0)   # cut exactly at the old-copy erase
        ped=F.Pedal(lambda q:self.host(bytes(q)))
        self.assertEqual(len([f for f in ped.files() if f['name']=='pst-0']),2)          # two copies after the cut
        c.ge_reboot();self.settle();self.assertEqual(c.ge_flash_save(0),1);self.settle()
        self.assertEqual(len([f for f in ped.files() if f['name']=='pst-0']),1)          # the next save removes both old ones
if __name__=='__main__':unittest.main()
